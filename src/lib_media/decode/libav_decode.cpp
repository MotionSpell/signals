#include "libav_decode.hpp"
#include "../common/pcm.hpp"
#include "lib_utils/tools.hpp"
#include "lib_ffpp/ffpp.hpp"
#include <cassert>

namespace Modules {
namespace Decode {

LibavDecode::LibavDecode(std::shared_ptr<const MetadataPktLibav> metadata)
	: codecCtx(shptr(avcodec_alloc_context3(nullptr))), avFrame(new ffpp::Frame) {
	avcodec_copy_context(codecCtx.get(), metadata->getAVCodecContext().get());
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO: break;
	case AVMEDIA_TYPE_AUDIO: break;
	default: throw error(format("codec_type %s not supported. Must be audio or video.", codecCtx->codec_type));
	}

	auto const codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec)
		throw error(format("Decoder not found for codecID (%s).", codecCtx->codec_id));
	ffpp::Dict dict(typeid(*this).name(), "-threads auto -err_detect 1 -flags output_corrupt -flags2 showall");
	if (avcodec_open2(codecCtx.get(), codec, &dict) < 0)
		throw error("Couldn't open stream.");
	codecCtx->refcounted_frames = true;

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO: {
		auto input = addInput(new Input<DataAVPacket>(this));
		input->setMetadata(shptr(new MetadataPktLibavVideo(codecCtx)));
		if (codecCtx->codec->capabilities & AV_CODEC_CAP_DR1) {
			codecCtx->thread_safe_callbacks = 1;
			codecCtx->opaque = safe_cast<LibavDirectRendering>(this);
			codecCtx->get_buffer2 = avGetBuffer2;
			videoOutput = addOutputDynAlloc<OutputPicture>(std::thread::hardware_concurrency() * 4, shptr(new MetadataRawVideo));
		} else {
			videoOutput = addOutput<OutputPicture>(shptr(new MetadataRawVideo));
		}
		break;
	}
	case AVMEDIA_TYPE_AUDIO: {
		auto input = addInput(new Input<DataAVPacket>(this));
		input->setMetadata(shptr(new MetadataPktLibavAudio(codecCtx)));
		audioOutput = addOutput<OutputPcm>(shptr(new MetadataRawAudio));
		break;
	}
	default:
		throw error("Invalid output type.");
	}
}

LibavDecode::~LibavDecode() {
	videoOutput = nullptr;
	flush(); //flush to avoid a leak of LibavDirectRenderingContext pictures
}

bool LibavDecode::processAudio(AVPacket const * const pkt) {
	int gotFrame = 0;
	if (avcodec_decode_audio4(codecCtx.get(), avFrame->get(), &gotFrame, pkt) < 0) {
		log(Warning, "Error encountered while decoding audio.");
		return false;
	}
	if (av_frame_get_decode_error_flags(avFrame->get()) || (avFrame->get()->flags & AV_FRAME_FLAG_CORRUPT)) {
		log(Error, "Corrupted audio frame decoded.");
	}
	if (gotFrame) {
		auto out = audioOutput->getBuffer(0);
		PcmFormat pcmFormat;
		libavFrame2pcmConvert(avFrame->get(), &pcmFormat);
		out->setFormat(pcmFormat);
		for (uint8_t i = 0; i < pcmFormat.numPlanes; ++i) {
			out->setPlane(i, avFrame->get()->data[i], avFrame->get()->nb_samples * pcmFormat.getBytesPerSample() / pcmFormat.numPlanes);
		}
		auto const &timebase = safe_cast<const MetadataPktLibavAudio>(getInput(0)->getMetadata())->getAVCodecContext()->time_base;
		out->setMediaTime(avFrame->get()->pkt_pts * timebase.num, timebase.den);
		audioOutput->emit(out);
		av_frame_unref(avFrame->get());
		return true;
	}

	av_frame_unref(avFrame->get());
	return false;
}

bool LibavDecode::processVideo(AVPacket const * const pkt) {
	int gotPicture = 0;
	if (avcodec_decode_video2(codecCtx.get(), avFrame->get(), &gotPicture, pkt) < 0) {
		log(Warning, "Error encountered while decoding video.");
		return false;
	}
	if (av_frame_get_decode_error_flags(avFrame->get()) || (avFrame->get()->flags & AV_FRAME_FLAG_CORRUPT)) {
		log(Error, "Corrupted video frame decoded (%s).", gotPicture);
	}
	if (gotPicture) {
		std::shared_ptr<DataPicture> pic;
		auto ctx = static_cast<LibavDirectRenderingContext*>(avFrame->get()->opaque);
		if (ctx) {
			pic = ctx->pic;
			ctx->pic->setVisibleResolution(Resolution(codecCtx->width, codecCtx->height));
		} else {
			pic = DataPicture::create(videoOutput, Resolution(avFrame->get()->width, avFrame->get()->height), libavPixFmt2PixelFormat((AVPixelFormat)avFrame->get()->format));
			copyToPicture(avFrame->get(), pic.get());
		}
		auto const &timebase = safe_cast<const MetadataPktLibavVideo>(getInput(0)->getMetadata())->getAVCodecContext()->time_base;
		pic->setMediaTime(avFrame->get()->pkt_pts * timebase.num, timebase.den);
		if (videoOutput) videoOutput->emit(pic);
		av_frame_unref(avFrame->get());
		return true;
	}

	av_frame_unref(avFrame->get());
	return false;
}

LibavDirectRendering::LibavDirectRenderingContext* LibavDecode::getPicture(const Resolution &res, const Resolution &resInternal, const PixelFormat &format) {
	auto ctx = new LibavDirectRenderingContext;
	ctx->pic = DataPicture::create(videoOutput, res, resInternal, format);
	return ctx;
}

void LibavDecode::process(Data data) {
	auto decoderData = safe_cast<const DataAVPacket>(data);
	inputs[0]->updateMetadata(data);
	AVPacket *pkt = decoderData->getPacket();
	if (pkt->flags & AV_PKT_FLAG_RESET_DECODER) {
		avcodec_flush_buffers(codecCtx.get());
		pkt->flags &= ~AV_PKT_FLAG_RESET_DECODER;
	}

	processPacket(pkt);
}


bool LibavDecode::processPacket(AVPacket const * pkt) {
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		return processVideo(pkt);
	case AVMEDIA_TYPE_AUDIO:
		return processAudio(pkt);
	default:
		assert(0);
		return false;
	}
}

void LibavDecode::flush() {
	AVPacket nullPkt;
	av_init_packet(&nullPkt);
	av_free_packet(&nullPkt);

	while(processPacket(&nullPkt)) {
	}
}

}
}
