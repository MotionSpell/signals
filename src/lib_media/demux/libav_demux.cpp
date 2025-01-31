#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "libav_demux.hpp"
#include "../transform/restamp.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/os.hpp"
#include "lib_utils/queue_lockfree.hpp"
#include "lib_utils/clock.hpp" // rescale
#include "../common/ffpp.hpp"
#include "../common/libav.hpp"
#include "../common/attributes.hpp"
#include <algorithm> //std::max
#include <atomic>
#include <thread>
#include <vector>

extern "C" {
#include <libavdevice/avdevice.h> // avdevice_register_all
#include <libavformat/avformat.h> // av_find_input_format
#include <libavcodec/avcodec.h> // av_parser_init
}

#define PKT_QUEUE_SIZE 256

#define AV_PKT_FLAG_RESET_DECODER (1 << 30)

static const int avioCtxBufferSize = 1024 * 1024;

using namespace Modules;
using namespace std::chrono;

namespace {

constexpr
const char* webcamFormat() {
#ifdef _WIN32
	return "dshow";
#elif __linux__
	return "v4l2";
#elif __APPLE__
	return "avfoundation";
#else
#error "unknown platform"
#endif
}

static
bool startsWith(std::string s, std::string prefix) {
	return s.compare(0, prefix.size(), prefix) == 0;
}

struct LibavDemux : Module {
	LibavDemux(KHost* host, DemuxConfig const& config)
		: m_host(host),
		  loop(config.loop), done(false), packetQueue(PKT_QUEUE_SIZE), m_read(config.func) {
		if (!(m_formatCtx = avformat_alloc_context()))
			throw error("Can't allocate format context");

		// declare ourselves as a source
		m_host->activate(true);

		avformat_network_init();

		auto& url = config.url;

		avdevice_register_all();
		const std::string device = url.substr(0, url.find(":"));
		if (device == "webcam") {
			if (url == device || !webcamOpen(url.substr(url.find(":") + 1))) {
				webcamList();
				clean();
				throw error("Webcam init failed.");
			}

			m_streams.resize(m_formatCtx->nb_streams);
			for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
				m_streams[i].restamper = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::ClockSystem); /*some webcams timestamps don't start at 0 (based on UTC)*/
			}
		} else {
			ffpp::Dict dict(typeid(*this).name(), "-rw_timeout " + std::to_string(config.ioTimeoutInMs*1000) + " -buffer_size 1M -fifo_size 1M -probesize 10M -analyzeduration 10M -overrun_nonfatal 1 -protocol_whitelist file,udp,rtp,http,https,tcp,tls,rtmp -rtsp_flags prefer_tcp -correct_ts_overflow 1 " + config.avformatCustom);

			if (m_read) {
				m_avioCtx = avio_alloc_context((unsigned char*)av_malloc(avioCtxBufferSize), avioCtxBufferSize, 0, this, &LibavDemux::read, nullptr, nullptr);
				if (!m_avioCtx)
					throw error("AvIO allocation failed");
				m_formatCtx->pb = m_avioCtx;
				m_formatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
			}

			const AVInputFormat* avInputFormat = nullptr;

			if(!config.formatName.empty()) {
				avInputFormat = av_find_input_format(config.formatName.c_str());
				if(!avInputFormat) {
					clean();
					throw error(format("Error when opening input '%s': unsupported input format '%s'", url, config.formatName));
				}
			}

			int err = avformat_open_input(&m_formatCtx, url.c_str(), avInputFormat, &dict);
			if (err) {
				clean();
				throw error(format("Error when opening input '%s': %s", url, avStrError(err)));
			}

			if(!avInputFormat) {
				m_host->log(Info, format("Using input format '%s'", m_formatCtx->iformat->name).c_str());
			}

			if (config.seekTimeInMs) {
				if (avformat_seek_file(m_formatCtx, -1, INT64_MIN, rescale(config.seekTimeInMs, 1000, AV_TIME_BASE), INT64_MAX, 0) < 0) {
					clean();
					throw error(format("Couldn't seek to time %sms", config.seekTimeInMs));
				}

				m_host->log(Info, format("Successful initial seek to %sms", config.seekTimeInMs).c_str());
			}

			//if you don't call you may miss the first frames
			if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
				clean();
				throw error("Couldn't get additional video stream info");
			}
			m_streams.resize(m_formatCtx->nb_streams);

			initRestamp();

			av_dict_free(&dict);
		}

		for (unsigned i = 0; i<m_formatCtx->nb_streams; i++) {
			auto const st = m_formatCtx->streams[i];
			auto const parser = av_parser_init(st->codecpar->codec_id);
			
			// Create codec context from parameters
			auto codecCtx = shptr(avcodec_alloc_context3(nullptr));
			if (avcodec_parameters_to_context(codecCtx.get(), st->codecpar) < 0) {
				m_host->log(Debug, "Failed to copy codec params to codec context");
				continue;
			}

			// Set time base and framerate
			codecCtx->time_base = st->time_base;
			if (!codecCtx->framerate.num) {
				codecCtx->framerate = st->avg_frame_rate;
			}

			// Create metadata based on stream type
			std::shared_ptr<const IMetadata> m;
			switch (st->codecpar->codec_type) {
				case AVMEDIA_TYPE_AUDIO: m = createMetadataPktLibavAudio(codecCtx.get()); break;
				case AVMEDIA_TYPE_VIDEO: m = createMetadataPktLibavVideo(codecCtx.get()); break;
				case AVMEDIA_TYPE_SUBTITLE: m = createMetadataPktLibavSubtitle(codecCtx.get()); break;
				default: break;
			}

			// Container-specific codec string assignment
			{
				auto meta = safe_cast<MetadataPkt>(const_cast<IMetadata*>(m.get()));
				auto const container = std::string(m_formatCtx->iformat->name);
				if(container.substr(0, 4) == "mov,") {
					switch(st->codecpar->codec_id) {
						case AV_CODEC_ID_H264: meta->codec = "h264_avcc"; break;
						case AV_CODEC_ID_HEVC: meta->codec = "hevc_avcc"; break;
						case AV_CODEC_ID_AAC: meta->codec = "aac_raw"; break;
						default: break;
					}
				} else if(container == "mpegts") {
					switch(st->codecpar->codec_id) {
						case AV_CODEC_ID_H264: meta->codec = "h264_annexb"; break;
						case AV_CODEC_ID_HEVC: meta->codec = "hevc_annexb"; break;
						case AV_CODEC_ID_AAC: meta->codec = "aac_adts"; break;
						case AV_CODEC_ID_AAC_LATM: meta->codec = "aac_latm"; break;
						default: break;
					}
				}
			}

			// Set output and metadata
			m_streams[i].output = addOutput();
			m_streams[i].output->setMetadata(m);
			av_dump_format(m_formatCtx, i, url.c_str(), 0);

			if (parser) {
				av_parser_close(parser);
			}
		}
	}

	~LibavDemux() {
		clean();
	}

	void process() override {
		if(!workingThread.joinable())
			workingThread = std::thread(&LibavDemux::inputThread, this);

		if(!declarationSent) {
			declareStreams();
			declarationSent = true;
		}

		AVPacket pkt;
		if (!packetQueue.read(pkt)) {
			if (done) {
				m_host->log(Info, "End of stream.");
				m_host->activate(false); // stop source
				return ;
			}
			std::this_thread::sleep_for(10ms);
			return ;
		}

		if (!rectifyTimestamps(pkt)) {
			av_packet_unref(&pkt);
			return ;
		}

		if (!dispatchable(&pkt)) {
			av_packet_unref(&pkt);
			return ;
		}

		dispatch(&pkt);
		av_packet_unref(&pkt);
	}

	void declareStreams() {
		for(auto& stream : m_streams) {
			auto output = stream.output;
			auto data = make_shared<DataBase>();
			data->setMetadata(output->getMetadata());
			output->post(data);
		}
	}

	int readFrame(AVPacket* pkt) {
		for(;;) {
			int status = av_read_frame(m_formatCtx, pkt);
			if (status != (int)AVERROR(EAGAIN))
				return status;

			m_host->log(Debug, format("Stream asks to try again later. Sleeping for a short period of time.").c_str());
			std::this_thread::sleep_for(10ms);
		}
	}

	void clean() {
		done = true;
		if (workingThread.joinable()) {
			workingThread.join();
		}

		if (m_formatCtx) {
			avformat_close_input(&m_formatCtx);
			avformat_free_context(m_formatCtx);
		}

		AVPacket p;
		while (packetQueue.read(p)) {
			av_packet_unref(&p);
		}

		if(m_avioCtx)
			av_free(m_avioCtx->buffer);
		av_free(m_avioCtx);
	}

	void webcamList() {
		m_host->log(Warning, "Webcam list:");
		ffpp::Dict dict(typeid(*this).name(), "-list_devices true");
		auto const level = getGlobalLogLevel();
		setGlobalLogLevel(Info);
		avformat_open_input(&m_formatCtx, "dummy", av_find_input_format(webcamFormat()), &dict);
		setGlobalLogLevel(level);
		m_host->log(Warning, "Webcam example: webcam:video=\"Integrated Webcam\":audio=\"Microphone (Realtek High Defini\"");
	}

	bool webcamOpen(const std::string &options) {
		auto avInputFormat = av_find_input_format(webcamFormat());
		if (avformat_open_input(&m_formatCtx, options.c_str(), avInputFormat, nullptr)) {
			avformat_free_context(m_formatCtx);
			m_formatCtx = nullptr;
			return false;
		}
		return true;
	}

	void initRestamp() {
		for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
			const std::string format(m_formatCtx->iformat->name);
			const std::string url = m_formatCtx->url;
			if (format == "rtsp" || url == "rtp" || format == "sdp" || startsWith(url, "rtp:") || startsWith(url, "udp:")) {
				highPriority = true;
				m_streams[i].restamper = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::IgnoreFirstAndReset);
			} else {
				m_streams[i].restamper = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Reset);
			}

			if (format == "rtsp" || format == "rtp" || format == "mpegts" || format == "rtmp" || format == "flv") { //TODO: evaluate why this is not the default behaviour
				auto stream = m_formatCtx->streams[i];
				startPTSIn180k = std::max<int64_t>(startPTSIn180k, timescaleToClock(stream->start_time*stream->time_base.num, stream->time_base.den));
			} else {
				startPTSIn180k = 0;
			}
		}
	}

	void seekToStart() {
		if (av_seek_frame(m_formatCtx, -1, m_formatCtx->start_time, AVSEEK_FLAG_ANY) < 0)
			throw error(format("Couldn't seek to start time %s", m_formatCtx->start_time));

		for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
			m_streams[i].offsetIn180k += timescaleToClock(m_formatCtx->duration, AV_TIME_BASE);
		}
	}

	bool rectifyTimestamps(AVPacket &pkt) {
		auto const stream = m_formatCtx->streams[pkt.stream_index];
		auto & demuxStream = m_streams[pkt.stream_index];
		auto const maxDelayInSec = 5;
		auto const thresholdInBase = (maxDelayInSec * stream->time_base.den) / stream->time_base.num;

		if (pkt.dts != AV_NOPTS_VALUE) {
			pkt.dts += clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den);
			if (demuxStream.lastDTS && pkt.dts < demuxStream.lastDTS
			    && (1LL << stream->pts_wrap_bits) - demuxStream.lastDTS < thresholdInBase && pkt.dts + (1LL << stream->pts_wrap_bits) > demuxStream.lastDTS) {
				demuxStream.offsetIn180k += timescaleToClock((1LL << stream->pts_wrap_bits) * stream->time_base.num, stream->time_base.den);
				m_host->log(Warning, format("Stream %s: overflow detecting on DTS (%s, last=%s, timescale=%s/%s, offset=%s).",
				        pkt.stream_index, pkt.dts, demuxStream.lastDTS, stream->time_base.num, stream->time_base.den, clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den)).c_str());
			}
		}

		if (pkt.pts != AV_NOPTS_VALUE) {
			pkt.pts += clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den);
			if (pkt.pts < pkt.dts && (1LL << stream->pts_wrap_bits) + pkt.pts - pkt.dts < thresholdInBase) {
				auto const localOffsetIn180k = timescaleToClock((1LL << stream->pts_wrap_bits) * stream->time_base.num, stream->time_base.den);
				m_host->log(Warning, format("Stream %s: overflow detecting on PTS (%s, new=%s, timescale=%s/%s, offset=%s).",
				        pkt.stream_index, pkt.pts, pkt.pts + localOffsetIn180k, stream->time_base.num, stream->time_base.den, clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den)).c_str());
				pkt.pts += localOffsetIn180k;
			}
		}

		/*dts repetition*/
		if (pkt.dts == m_streams[pkt.stream_index].lastDTS) {
			m_streams[pkt.stream_index].lastDTS = pkt.dts = m_streams[pkt.stream_index].lastDTS + 1;
		}

		/*pts is mandatory since output as the media time*/
		if (pkt.pts != AV_NOPTS_VALUE) {
			if (pkt.dts != AV_NOPTS_VALUE) {
				if (pkt.pts < pkt.dts) {
					m_host->log(Error, format("Stream %s: pts < dts (%s < %s)", pkt.stream_index, pkt.pts, pkt.dts).c_str());
					return false;
				}
			}
		} else {
			if (pkt.dts == AV_NOPTS_VALUE) {
				m_streams[pkt.stream_index].lastDTS = pkt.dts = m_streams[pkt.stream_index].lastDTS + 1;
				m_host->log(Debug, format("Missing pts and dts, inferring to dts+1=%s.", pkt.dts).c_str());
			} else {
				m_host->log(Debug, format("Missing pts, inferring to dts (%s).", pkt.dts).c_str());
			}
			pkt.pts = pkt.dts;
		}

		return true;
	}

	void inputThread() {
		if(highPriority && !setHighThreadPriority())
			m_host->log(Warning, format("Couldn't change reception thread priority to realtime.").c_str());

		bool nextPacketResetFlag = false;
		while (!done) {
			AVPacket pkt;
			av_init_packet(&pkt);

			int status = readFrame(&pkt);

			if (status < 0) {
				av_packet_unref(&pkt);

				if (status == (int)AVERROR_EOF || (m_formatCtx->pb && m_formatCtx->pb->eof_reached)) {
					m_host->log(Info, format("End of stream detected - %s", loop ? "looping" : "leaving").c_str());
					if (loop) {
						seekToStart();
						nextPacketResetFlag = true;
						continue;
					}
				} else if (m_formatCtx->pb && m_formatCtx->pb->error) {
					m_host->log(Error, format("Stream contains an irrecoverable error (%s) - leaving", status).c_str());
				}
				done = true;
				return;
			}

			if (pkt.stream_index >= (int)m_streams.size()) {
				m_host->log(Warning, format("Detected stream index %s that was not initially detected (adding streams dynamically is not supported yet). Discarding packet.", pkt.stream_index).c_str());
				av_packet_unref(&pkt);
				continue;
			}

			if (nextPacketResetFlag) {
				pkt.flags |= AV_PKT_FLAG_RESET_DECODER;
				nextPacketResetFlag = false;
			}

			while (!packetQueue.write(pkt)) {
				if (done) {
					av_packet_unref(&pkt);
					return;
				}
				m_host->log(m_formatCtx->pb && !m_formatCtx->pb->seekable ? Warning : Debug,
				    "Dispatch queue is full - regulating.");
				std::this_thread::sleep_for(10ms);
			}
		}
	}

	void setTimestamp(AVPacket* pkt, std::shared_ptr<DataRaw> data) {
		if (!pkt->duration) {
			pkt->duration = pkt->dts - m_streams[pkt->stream_index].lastDTS;
		}
		m_streams[pkt->stream_index].lastDTS = pkt->dts;
		auto const base = m_formatCtx->streams[pkt->stream_index]->time_base;
		auto const time = timescaleToClock(pkt->pts * base.num, base.den);
		data->setMediaTime(time - startPTSIn180k);
		auto const decodingTime = timescaleToClock(pkt->dts * base.num, base.den);
		data->set(DecodingTime { decodingTime - startPTSIn180k });
		if (!startPTSIn180k) {
			data->setMediaTime(m_streams[pkt->stream_index].restamper->restamp(data->get<PresentationTime>().time)); //restamp by pid only when no start time
		}
	}

	bool dispatchable(AVPacket * const pkt) {
		if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
			m_host->log(Error, format("Corrupted packet received (DTS=%s).", pkt->dts).c_str());
		}
		if (pkt->dts == AV_NOPTS_VALUE) {
			pkt->dts = m_streams[pkt->stream_index].lastDTS;
			m_host->log(Debug, format("No DTS: setting last value %s.", pkt->dts).c_str());
		}
		if (!m_streams[pkt->stream_index].lastDTS) {
			auto stream = m_formatCtx->streams[pkt->stream_index];
			auto minPts = clockToTimescale(startPTSIn180k*stream->time_base.num, stream->time_base.den);
			if(pkt->pts < minPts) {
				return false;
			}
		}
		return true;
	}

	void dispatch(AVPacket *pkt) {
		auto output = m_streams[pkt->stream_index].output;
		auto out = output->allocData<DataRaw>(pkt->size);
		memcpy(out->buffer->data().ptr, pkt->data, pkt->size);

		CueFlags flags {};
		if(pkt->flags & AV_PKT_FLAG_RESET_DECODER)
			flags.discontinuity = true;
		if(pkt->flags & AV_PKT_FLAG_KEY)
			flags.keyframe = true;
		out->set(flags);

		setTimestamp(pkt, out);
		output->post(out);
		sparseStreamsHeartbeat(pkt);
	}

	void sparseStreamsHeartbeat(AVPacket const * const pkt) {
		int64_t curDTS = 0;

		// signal clock from audio to sparse streams
		// (should be the PCR but libavformat doesn't give access to it)
		if (m_formatCtx->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			auto const base = m_formatCtx->streams[pkt->stream_index]->time_base;
			auto const time = timescaleToClock(pkt->dts * base.num, base.den);
			if (time > curDTS) {
				curDTS = time;
			}
		}
		if (curDTS > curTimeIn180k) {
			curTimeIn180k = curDTS;
			for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
				if ((int)i == pkt->stream_index) {
					continue;
				}
				auto const st = m_formatCtx->streams[i];
				if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
					auto sparse = m_streams[i].output->allocData<DataRaw>(0);
					sparse->setMediaTime(curTimeIn180k);
					m_streams[i].output->post(sparse);
				}
			}
		}
	}

	struct Stream {
		OutputDefault* output = nullptr;
		uint64_t offsetIn180k = 0;
		int64_t lastDTS = std::numeric_limits<int64_t>::min();
		std::unique_ptr<Transform::Restamp> restamper;
	};

	KHost* const m_host;

	std::vector<Stream> m_streams;

	const bool loop;
	bool highPriority = false;
	bool declarationSent = false;
	std::thread workingThread;
	std::atomic_bool done;
	QueueLockFree<AVPacket> packetQueue;
	AVFormatContext* m_formatCtx;
	AVIOContext* m_avioCtx = nullptr;
	const DemuxConfig::ReadFunc m_read;
	int64_t curTimeIn180k = 0, startPTSIn180k = std::numeric_limits<int64_t>::min();

	static int read(void* user, uint8_t* data, int size) {
		auto pThis = (LibavDemux*)user;
		return pThis->m_read(data, size);
	}
};

Modules::IModule* createObject(KHost* host, void* va) {
	auto config = (DemuxConfig*)va;
	enforce(host, "LibavDemux: host can't be NULL");
	enforce(config, "LibavDemux: config can't be NULL");
	return createModuleWithSize<LibavDemux>(384, host, *config).release();
}

auto const registered = Factory::registerModule("LibavDemux", &createObject);

}
