#include "libav_mux.hpp"
#include "lib_utils/tools.hpp"
#include "../common/libav.hpp"
#include <cassert>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

namespace Modules {

namespace {
auto g_InitAv = runAtStartup(&av_register_all);
auto g_InitAvcodec = runAtStartup(&avcodec_register_all);
auto g_InitAvLog = runAtStartup(&av_log_set_callback, avLog);
}

namespace Mux {

LibavMux::LibavMux(const std::string &baseName, const std::string &fmt, const std::string &options)
	: optionsDict(typeid(*this).name(), "options", options) {
	/* setup container */
	ffpp::Dict formatDict(typeid(*this).name(), "format", "-format " + fmt);
	AVOutputFormat *of = av_guess_format(formatDict.get("format")->value, nullptr, nullptr);
	if (!of)
		throw error("couldn't guess container from file extension");
	av_dict_free(&formatDict);

	/* output format context */
	m_formatCtx = avformat_alloc_context();
	if (!m_formatCtx)
		throw error("format context couldn't be allocated.");
	m_formatCtx->oformat = of;

	std::stringstream fileName;
	fileName << baseName;
	std::stringstream formatExts(of->extensions); //get the first extension recommended by ffmpeg
	std::string fileNameExt;
	std::getline(formatExts, fileNameExt, ',');
	fileName << "." << fileNameExt;

	/* open the output file, if needed */
	if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
		if (baseName.compare(0, 7, "http://")) {
			m_avio = uptr(new ffpp::AvIO);
			m_formatCtx->pb = m_avio->get();
			m_formatCtx->flags = AVFMT_FLAG_CUSTOM_IO;
		} else {
			if (avio_open(&m_formatCtx->pb, fileName.str().c_str(), AVIO_FLAG_READ_WRITE) < 0) {
				avformat_free_context(m_formatCtx);
				throw error(format("could not open %s, disable output.", baseName));
			}
		}
	}
	strncpy(m_formatCtx->filename, fileName.str().c_str(), sizeof(m_formatCtx->filename));

	if (!fmt.compare(0, 5, "mpegts") || !fmt.compare(0, 3, "hls")) {
		m_inbandMetadata = true;
	}
}

LibavMux::~LibavMux() {
	if (m_formatCtx) {
		if (m_headerWritten) {
			av_write_trailer(m_formatCtx); //write the trailer if any
		}

		if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
			avio_flush(m_formatCtx->pb);
			if (!(m_formatCtx->flags & AVFMT_FLAG_CUSTOM_IO)) {
				avio_close(m_formatCtx->pb); //close output file
			}
		}

		for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
			avcodec_close(m_formatCtx->streams[i]->codec);
		}

		avformat_free_context(m_formatCtx);
	}
}

void LibavMux::declareStream(Data data) {
	auto const metadata_ = data->getMetadata();
	auto metadataVideo = std::dynamic_pointer_cast<const MetadataPktLibavVideo>(metadata_);
	auto metadataAudio = std::dynamic_pointer_cast<const MetadataPktLibavAudio>(metadata_);
	std::shared_ptr<const MetadataPktLibav> metadata;
	if (metadataVideo) {
		metadata = metadataVideo;
	} else if (metadataAudio) {
		metadata = metadataAudio;
	} else {
		throw error("Stream creation failed: unknown type.");
	}

	AVStream *avStream = avformat_new_stream(m_formatCtx, metadata->getAVCodecContext()->codec);
	if (!avStream)
		throw error("Stream creation failed.");
	if (avcodec_parameters_from_context(avStream->codecpar, metadata->getAVCodecContext()) < 0)
		throw error("Stream parameters copy failed.");
	avStream->codec->time_base = metadata->getAVCodecContext()->time_base;

	auto input = addInput(new Input<DataAVPacket>(this));
	if (metadataVideo) {
		input->setMetadata(new MetadataPktLibavVideo(metadataVideo->getAVCodecContext()));
	} else if (metadataAudio) {
		input->setMetadata(new MetadataPktLibavAudio(metadataAudio->getAVCodecContext()));
	}
}

void LibavMux::ensureHeader() {
	if (!m_headerWritten) {
		if (avformat_write_header(m_formatCtx, &optionsDict) != 0) {
			log(Warning, "fatal error: can't write the container header");
			for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
				if (m_formatCtx->streams[i]->codec && m_formatCtx->streams[i]->codec->codec) {
					log(Debug, "codec[%s] is \"%s\" (%s)", i, m_formatCtx->streams[i]->codec->codec->name, m_formatCtx->streams[i]->codec->codec->long_name);
					if (!m_formatCtx->streams[i]->codec->extradata)
						throw error("Bitstream format is not raw. Check your encoder settings.");
				}
			}
		} else {
			optionsDict.ensureAllOptionsConsumed();
			m_headerWritten = true;
		}
	}
}

AVPacket * LibavMux::getFormattedPkt(Data data) {
	auto pkt = safe_cast<const DataAVPacket>(data)->getPacket();
	auto videoMetadata = std::dynamic_pointer_cast<const MetadataPktLibavVideo>(data->getMetadata()); //video only ATM
	if (m_inbandMetadata && videoMetadata && (pkt->flags & AV_PKT_FLAG_KEY)) {
		auto const eSize = videoMetadata->getAVCodecContext()->extradata_size;
		auto const outSize = pkt->size + eSize;
		auto newPkt = av_packet_alloc();
		av_init_packet(newPkt);
		av_new_packet(newPkt, outSize);
		memcpy(newPkt->data, videoMetadata->getAVCodecContext()->extradata, eSize);
		memcpy(newPkt->data + eSize, pkt->data, pkt->size);
		newPkt->size = outSize;
		newPkt->flags = pkt->flags;
		newPkt->dts = pkt->dts;
		newPkt->pts = pkt->pts;
		return newPkt;
	} else {
		return av_packet_clone(pkt);
	}
}

void LibavMux::process() {
	//FIXME: reimplement with multiple inputs
	Data data = inputs[0]->pop();
	if (inputs[0]->updateMetadata(data))
		declareStream(data);

	ensureHeader();
	auto pkt = getFormattedPkt(data);

	/* timestamps */
	assert(pkt->pts != (int64_t)AV_NOPTS_VALUE);
	AVStream *avStream = m_formatCtx->streams[0]; //FIXME: fixed '0' for stream num: this is not a mux yet ;)
	pkt->dts = av_rescale_q(pkt->dts, avStream->codec->time_base, avStream->time_base);
	pkt->pts = av_rescale_q(pkt->pts, avStream->codec->time_base, avStream->time_base);
	pkt->duration = (int64_t)av_rescale_q(pkt->duration, avStream->codec->time_base, avStream->time_base);

	/* write the compressed frame to the container output file */
	pkt->stream_index = avStream->index;
	if (av_interleaved_write_frame(m_formatCtx, pkt) != 0) {
		log(Warning, "can't write video frame.");
		return;
	}
	av_packet_free(&pkt);
}

}
}
