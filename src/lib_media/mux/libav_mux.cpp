#include "libav_mux.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/tools.hpp"
#include "../common/ffpp.hpp"
#include "../common/metadata.hpp"
#include <cassert>
#include <string>
#include <sstream>

extern "C" {
#include <libavformat/avformat.h> // AVOutputFormat
#include <libavformat/avio.h> // avio_open2
}

using namespace Modules;
using namespace std;

namespace {

class LibavMux : public ModuleDynI {
	public:

		LibavMux(IModuleHost* host, MuxConfig cfg)
			: m_host(host), m_formatCtx(avformat_alloc_context()), optionsDict(typeid(*this).name(), cfg.options) {
			if (!m_formatCtx)
				throw error("Format context couldn't be allocated.");
			m_formatCtx->flags &= ~AVFMT_FLAG_AUTO_BSF;

			auto const of = av_guess_format(cfg.format.c_str(), nullptr, nullptr);
			if (!of) {
				formatsList();
				throw error("Couldn't guess output format. Check list above for supported ones.");
			}
			m_formatCtx->oformat = of;

			std::string fileName = cfg.baseName;
			std::stringstream formatExts(of->extensions); //get the first extension recommended by ffmpeg
			std::string fileNameExt;
			std::getline(formatExts, fileNameExt, ',');
			if (fileName.find("://") == std::string::npos) {
				fileName += "." + fileNameExt;
			}
			if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) { /* open the output file, if needed */
				if (avio_open2(&m_formatCtx->pb, fileName.c_str(), AVIO_FLAG_READ_WRITE, nullptr, &optionsDict) < 0) {
					avformat_free_context(m_formatCtx);
					throw error(format("could not open %s, disable output.", cfg.baseName));
				}
			}

			m_formatCtx->url = av_strdup(fileName.c_str());

			av_dict_set(&m_formatCtx->metadata, "service_provider", "GPAC Licensing Signals", 0);
			av_dump_format(m_formatCtx, 0, cfg.baseName.c_str(), 1);

			if (!cfg.format.compare(0, 5, "mpegts") || !cfg.format.compare(0, 3, "hls")) {
				m_inbandMetadata = true;
			}
		}

		~LibavMux() {
			assert(m_formatCtx);
			{
				if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
					if (!(m_formatCtx->flags & AVFMT_FLAG_CUSTOM_IO)) {
						avio_close(m_formatCtx->pb); //close output file
					}
				}
				for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
					avcodec_parameters_free(&m_formatCtx->streams[i]->codecpar);
				}

				avformat_free_context(m_formatCtx);
			}
		}

		void process() override {

			int inputIdx;
			auto data = popAny(inputIdx);
			auto prevInputMeta = inputs[inputIdx]->getMetadata();
			if (inputs[inputIdx]->updateMetadata(data)) {
				if (prevInputMeta) {
					if(!(*prevInputMeta == *inputs[inputIdx]->getMetadata()))
						m_host->log(Error, format("input #%s: updating existing metadata. Not supported but continuing execution.", inputIdx).c_str());
				} else {
					assert(!m_headerWritten);
					declareStream(data, inputIdx);
				}
			}

			if (m_formatCtx->nb_streams < (size_t)getNumInputs() - 1) {
				m_host->log(Warning, "Some inputs didn't declare their streams yet, dropping input data");
				return;
			}

			// if stream is declared statically, there's no data to process.
			if (isEvent(data))
				return;

			ensureHeader();

			auto pkt = getFormattedPkt(data);
			assert(pkt->pts != (int64_t)AV_NOPTS_VALUE);
			auto const pktTimescale = safe_cast<const MetadataPkt>(data->getMetadata())->timeScale;
			const AVRational inputTimebase = { (int)pktTimescale.den, (int)pktTimescale.num };
			auto const avStream = m_formatCtx->streams[inputIdx2AvStream[inputIdx]];
			pkt->dts = av_rescale_q(pkt->dts, inputTimebase, avStream->time_base);
			pkt->pts = av_rescale_q(pkt->pts, inputTimebase, avStream->time_base);
			pkt->duration = (int64_t)av_rescale_q(pkt->duration, inputTimebase, avStream->time_base);
			pkt->stream_index = avStream->index;

			if (av_interleaved_write_frame(m_formatCtx, pkt.get()) != 0) {
				m_host->log(Warning, "can't write frame.");
				return;
			}
		}

		void flush() override {
			if (m_headerWritten) {
				av_write_trailer(m_formatCtx); //write the trailer if any
			}

			if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
				avio_flush(m_formatCtx->pb);
			}
		}

	private:
		IModuleHost* const m_host;
		AVFormatContext* const m_formatCtx;
		std::map<size_t, size_t> inputIdx2AvStream;
		ffpp::Dict optionsDict;
		bool m_headerWritten = false;
		bool m_inbandMetadata = false;

		Data popAny(int& inputIdx) {
			Data data;
			inputIdx = 0;
			while (!inputs[inputIdx]->tryPop(data)) {
				inputIdx++;
			}
			return data;
		}

		void declareStream(Data data, size_t inputIdx) {
			if(!data->getMetadata())
				throw error("Can't declare stream without metadata");

			auto const metadata = safe_cast<const MetadataPkt>(data->getMetadata().get());

			auto const codec = avcodec_find_decoder_by_name(metadata->codec.c_str());
			if (!codec)
				throw error(format("Codec not found: '%s'.", metadata->codec));

			auto stream = avformat_new_stream(m_formatCtx, codec);
			if (!stream)
				throw error("Stream creation failed.");

			auto codecpar = stream->codecpar;

			codecpar->codec_id = codec->id;

			if(auto info = dynamic_cast<const MetadataPktVideo*>(metadata)) {
				codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
				codecpar->width  = info->resolution.width;
				codecpar->height = info->resolution.height;
			} else if(auto info = dynamic_cast<const MetadataPktAudio*>(metadata)) {
				codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
				codecpar->sample_rate = info->sampleRate;
				codecpar->channels = info->numChannels;
				codecpar->frame_size = info->frameSize;
			} else {
				// anything to do for subtitles?
			}

			auto& extradata = metadata->codecSpecificInfo;
			codecpar->extradata_size = extradata.size();
			codecpar->extradata = (uint8_t*)av_malloc(extradata.size());
			if(extradata.size())
				memcpy(codecpar->extradata, extradata.data(), extradata.size());

			stream->time_base = {1, IClock::Rate};
			inputIdx2AvStream[inputIdx] = m_formatCtx->nb_streams - 1;
		}

		bool isEvent(Data data) {
			auto refData = std::dynamic_pointer_cast<const DataBaseRef>(data);
			return refData && !refData->getData();
		}

		void ensureHeader() {
			if (!m_headerWritten) {
				if (avformat_write_header(m_formatCtx, &optionsDict) != 0)
					throw error("can't write the container header");

				optionsDict.ensureAllOptionsConsumed();
				m_headerWritten = true;
			}
		}

		shared_ptr<AVPacket> getFormattedPkt(Data data) {
			auto newPkt = av_packet_alloc();
			av_init_packet(newPkt);

			auto meta = data->getMetadata().get();
			auto videoMetadata = dynamic_cast<const MetadataPkt*>(meta);
			bool insertHeaders = m_inbandMetadata && videoMetadata && (data->flags & DATA_FLAGS_KEYFRAME);

			auto const& headers = insertHeaders ? videoMetadata->codecSpecificInfo : std::vector<uint8_t>();
			auto const outSize = data->data().len + headers.size();
			av_new_packet(newPkt, (int)outSize);
			if(headers.size())
				memcpy(newPkt->data, headers.data(), headers.size());
			memcpy(newPkt->data + headers.size(), data->data().ptr, data->data().len);
			newPkt->size = (int)outSize;
			newPkt->pts = data->getMediaTime();
			newPkt->dts = safe_cast<const DataPacket>(data)->getDecodingTime();

			return shared_ptr<AVPacket>(newPkt, &my_av_packet_deleter);
		}

		static void my_av_packet_deleter(AVPacket* pkt) {
			av_packet_free(&pkt);
		}

		void formatsList() {
			g_Log->log(Warning, "Output formats list:");
			void* iterator = nullptr;
			while (auto fmt = av_muxer_iterate(&iterator))
				g_Log->log(Warning, format("fmt->name=%s, fmt->mime_type=%s, fmt->extensions=%s", fmt->name ? fmt->name : "", fmt->mime_type ? fmt->mime_type : "", fmt->extensions ? fmt->extensions : "").c_str());
		}

};

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto config = va_arg(va, MuxConfig*);
	enforce(host, "LibavMux: host can't be NULL");
	enforce(config, "LibavMux: config can't be NULL");

	avformat_network_init();

	return Modules::create<LibavMux>(host, *config).release();
}

auto const registered = Factory::registerModule("LibavMux", &createObject);
}
