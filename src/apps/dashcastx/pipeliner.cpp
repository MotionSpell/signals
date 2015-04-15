extern "C" {
#include "libavcodec/avcodec.h" //FIXME: there should be none of the modules include at the application level
}
#include "lib_modules/modules.hpp"
#include "lib_media/media.hpp"
#include "pipeliner.hpp"
#include <sstream>

namespace {
Encode::LibavEncode* createEncoder(PropsDecoder *decoderProps, bool isLowLatency) {
	auto const codecType = decoderProps ? decoderProps->getAVCodecContext()->codec_type : AVMEDIA_TYPE_UNKNOWN;
	if (codecType == AVMEDIA_TYPE_VIDEO) {
		Log::msg(Log::Info, "[Encoder] Found video stream");
		return new Encode::LibavEncode(Encode::LibavEncode::Video, isLowLatency);
	}
	else if (codecType == AVMEDIA_TYPE_AUDIO) {
		Log::msg(Log::Info, "[Encoder] Found audio stream");
		return new Encode::LibavEncode(Encode::LibavEncode::Audio);
	}
	else {
		Log::msg(Log::Info, "[Encoder] Found unknown stream");
		return nullptr;
	}
}

Module* createConverter(PropsDecoder *decoderProps, const Resolution &dstRes) {
	auto const codecType = decoderProps ? decoderProps->getAVCodecContext()->codec_type : AVMEDIA_TYPE_UNKNOWN;
	if (codecType == AVMEDIA_TYPE_VIDEO) {
		Log::msg(Log::Info, "[Converter] Found video stream");
		auto srcCtx = decoderProps->getAVCodecContext();
		auto dstFormat = PictureFormat(dstRes, libavPixFmt2PixelFormat(srcCtx->pix_fmt));
		return new Transform::VideoConvert(dstFormat);
	} else if (codecType == AVMEDIA_TYPE_AUDIO) {
		Log::msg(Log::Info, "[Converter] Found audio stream");
		auto format = PcmFormat(44100, 2, AudioLayout::Stereo, AudioSampleFormat::S16, AudioStruct::Interleaved);
		return new Transform::AudioConvert(format);
	} else {
		Log::msg(Log::Info, "[Converter] Found unknown stream");
		return nullptr;
	}
}
}

void declarePipeline(Pipeline &pipeline, const dashcastXOptions &opt) {
	auto connect = [&](PipelinedModule* src, PipelinedModule* dst) {
		pipeline.connect(src->getPin(0), dst);
	};

	auto demux = pipeline.addModule(new Demux::LibavDemux(opt.url), true);
	auto dasher = pipeline.addModule(new Modules::Stream::MPEG_DASH(
		opt.isLive ? Modules::Stream::MPEG_DASH::Live : Modules::Stream::MPEG_DASH::Static, opt.segmentDuration));

	for (int i = 0; i < (int)demux->getNumPin(); ++i) {
		auto props = demux->getPin(i)->getProps();
		auto decoderProps = safe_cast<PropsDecoder>(props);
		auto decoder = pipeline.addModule(new Decode::LibavDecode(*decoderProps));

		pipeline.connect(demux->getPin(i), decoder);

		auto converter = pipeline.addModule(createConverter(decoderProps, opt.res));
		if (!converter)
			continue;

		connect(decoder, converter);

		auto rawEncoder = createEncoder(decoderProps, opt.isLive);
		auto encoder = pipeline.addModule(rawEncoder);
		if (!encoder)
			continue;

		connect(converter, encoder);

		std::stringstream filename;
		filename << i;
		auto muxer = pipeline.addModule(new Mux::GPACMuxMP4(filename.str(), true, opt.segmentDuration));
		connect(encoder, muxer);
		rawEncoder->sendOutputPinsInfo();

		connect(muxer, dasher);
	}
}
