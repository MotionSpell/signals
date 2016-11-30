#include "libav_decode.hpp"
#include "../common/pcm.hpp"
#include "lib_utils/tools.hpp"
#include "lib_ffpp/ffpp.hpp"
#include <cassert>

namespace Modules {

namespace {
auto g_InitAv = runAtStartup(&av_register_all);
auto g_InitAvcodec = runAtStartup(&avcodec_register_all);
auto g_InitAvLog = runAtStartup(&av_log_set_callback, avLog);
}

namespace Decode {

LibavDecode::LibavDecode(const MetadataPktLibav &metadata)
	: codecCtx(avcodec_alloc_context3(nullptr)), avFrame(new ffpp::Frame) {
	avcodec_copy_context(codecCtx, metadata.getAVCodecContext());

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO: break;
	case AVMEDIA_TYPE_AUDIO: break;
	default: throw error(format("codec_type %s not supported. Must be audio or video.", codecCtx->codec_type));
	}

	//find an appropriate decode
	auto codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec)
		throw error(format("Decoder not found for codecID(%s).", codecCtx->codec_id));

	//force single threaded as h264 probing seems to miss SPS/PPS and seek fails silently
	ffpp::Dict dict(typeid(*this).name(), "decoder", "-threads 1 -flags2 showall");
	if (avcodec_open2(codecCtx, codec, &dict) < 0)
		throw error("Couldn't open stream.");

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO: {
		auto input = addInput(new Input<DataAVPacket>(this));
		input->setMetadata(new MetadataPktLibavVideo(codecCtx));
		videoOutput = addOutput<OutputPicture>(new MetadataRawVideo);
		break;
	}
	case AVMEDIA_TYPE_AUDIO: {
		auto input = addInput(new Input<DataAVPacket>(this));
		input->setMetadata(new MetadataPktLibavAudio(codecCtx));
		audioOutput = addOutput<OutputPcm>(new MetadataRawAudio);
		break;
	}
	default:
		throw error("Invalid output type.");
	}
}

LibavDecode::~LibavDecode() {
	avcodec_close(codecCtx);
	auto codecCtxCopy = codecCtx;
	avcodec_free_context(&codecCtxCopy);
}

bool LibavDecode::processAudio(const DataAVPacket *data) {
	AVPacket *pkt = data->getPacket();
	int gotFrame;
	if (avcodec_decode_audio4(codecCtx, avFrame->get(), &gotFrame, pkt) < 0) {
		log(Warning, "Error encoutered while decoding audio.");
		return false;
	}
	if (gotFrame) {
		auto out = audioOutput->getBuffer(0);
		PcmFormat pcmFormat;
		libavFrame2pcmConvert(avFrame->get(), &pcmFormat);
		out->setFormat(pcmFormat);
		for (uint8_t i = 0; i < pcmFormat.numPlanes; ++i) {
			out->setPlane(i, avFrame->get()->data[i], avFrame->get()->nb_samples * pcmFormat.getBytesPerSample() / pcmFormat.numPlanes);
		}
		out->setTime(cumulatedDuration * codecCtx->time_base.num, codecCtx->time_base.den);
		cumulatedDuration += avFrame->get()->nb_samples;
		audioOutput->emit(out);
		return true;
	}

	return false;
}

namespace {
//FIXME: this function is related to DataPicture and libav and should not be in a module (libav.xpp) + we can certainly avoid a memcpy here
void copyToPicture(AVFrame const* avFrame, DataPicture* pic) {
	for (size_t comp=0; comp<pic->getNumPlanes(); ++comp) {
		auto const subsampling = comp == 0 ? 1 : 2;
		auto const bytePerPixel = pic->getFormat().format == YUYV422 ? 2 : 1;
		auto src = avFrame->data[comp];
		auto const srcPitch = avFrame->linesize[comp];

		auto dst = pic->getPlane(comp);
		auto const dstPitch = pic->getPitch(comp);

		auto const w = avFrame->width * bytePerPixel / subsampling;
		auto const h = avFrame->height / subsampling;

		for (int y=0; y<h; ++y) {
			memcpy(dst, src, w);
			src += srcPitch;
			dst += dstPitch;
		}
	}
}
}

bool LibavDecode::processVideo(const DataAVPacket *data) {
	AVPacket *pkt = data->getPacket();
	int gotPicture;
	if (avcodec_decode_video2(codecCtx, avFrame->get(), &gotPicture, pkt) < 0) {
		log(Warning, "Error encoutered while decoding video.");
		return false;
	}
	if (av_frame_get_decode_error_flags(avFrame->get()) || (avFrame->get()->flags & AV_FRAME_FLAG_CORRUPT)) {
		log(Error, "Corrupted frame decoded.");
	}
	if (gotPicture) {
		auto pic = DataPicture::create(videoOutput, Resolution(avFrame->get()->width, avFrame->get()->height), libavPixFmt2PixelFormat((AVPixelFormat)avFrame->get()->format));
		copyToPicture(avFrame->get(), pic.get());
		pic->setTime(cumulatedDuration * codecCtx->time_base.num, codecCtx->time_base.den);
		cumulatedDuration += codecCtx->ticks_per_frame;
		videoOutput->emit(pic);
		return true;
	}
	av_frame_unref(avFrame->get());

	return false;
}

void LibavDecode::process(Data data) {
	auto decoderData = safe_cast<const DataAVPacket>(data);
	if (!dataReceived) {
		cumulatedDuration += clockToTimescale(data->getTime()*codecCtx->time_base.num, codecCtx->time_base.den);
		dataReceived = true;
	}
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		processVideo(decoderData.get());
		break;
	case AVMEDIA_TYPE_AUDIO:
		processAudio(decoderData.get());
		break;
	default:
		assert(0);
		return;
	}
}

void LibavDecode::flush() {
	auto nullPkt = uptr(new DataAVPacket(0));
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		while (processVideo(nullPkt.get())) {}
		break;
	case AVMEDIA_TYPE_AUDIO:
		while (processAudio(nullPkt.get())) {}
		break;
	default:
		assert(0);
		break;
	}
}

}
}
