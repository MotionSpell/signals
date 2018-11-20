#include "libav.hpp"
#include "pcm.hpp"
#include "picture_allocator.hpp"
#include "lib_utils/clock.hpp"
#include "lib_utils/log.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/format.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
}

namespace Modules {
void AVCodecContextDeleter(AVCodecContext *p);
}

std::shared_ptr<AVCodecContext> shptr(AVCodecContext *p) {
	return std::shared_ptr<AVCodecContext>(p, Modules::AVCodecContextDeleter);
}


namespace Modules {

void AVCodecContextDeleter(AVCodecContext *p) {
	avcodec_close(p);
	avcodec_free_context(&p);
}

StreamType getType(AVCodecContext* codecCtx) {
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO: return VIDEO_PKT;
	case AVMEDIA_TYPE_AUDIO: return AUDIO_PKT;
	case AVMEDIA_TYPE_SUBTITLE: return SUBTITLE_PKT;
	default: throw std::runtime_error("Unknown stream type. Only audio, video and subtitle handled.");
	}
}

static
void initMetadatPkt(MetadataPkt* meta, AVCodecContext* codecCtx) {
	enforce(codecCtx != nullptr, "MetadataPkt 'codecCtx' can't be null.");
	meta->codec = avcodec_get_name(codecCtx->codec_id);
	meta->codecSpecificInfo.assign(codecCtx->extradata, codecCtx->extradata + codecCtx->extradata_size);
	meta->bitrate = codecCtx->bit_rate;

	if (!codecCtx->time_base.num || !codecCtx->time_base.den)
		throw std::runtime_error(format("Unsupported time scale %s/%s.", codecCtx->time_base.den, codecCtx->time_base.num));
	meta->timeScale = Fraction(codecCtx->time_base.den, codecCtx->time_base.num);
}

Metadata createMetadataPktLibavVideo(AVCodecContext* codecCtx) {
	auto meta = make_shared<MetadataPktVideo>();
	initMetadatPkt(meta.get(), codecCtx);
	meta->timeScale = Fraction(codecCtx->time_base.den, codecCtx->time_base.num);
	meta->pixelFormat = libavPixFmt2PixelFormat(codecCtx->pix_fmt);
	auto const &sar = codecCtx->sample_aspect_ratio;
	meta->sampleAspectRatio =	Fraction(sar.num, sar.den);
	meta->resolution = Resolution(codecCtx->width, codecCtx->height);
	if (!codecCtx->framerate.num || !codecCtx->framerate.den)
		throw std::runtime_error(format("Unsupported video frame rate %s/%s.", codecCtx->framerate.den, codecCtx->framerate.num));
	meta->framerate = Fraction(codecCtx->framerate.num, codecCtx->framerate.den);
	return meta;
}

static
bool isPlanar(const AVCodecContext* codecCtx) {
	switch (codecCtx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_FLT:
		return false;
	case AV_SAMPLE_FMT_S16P:
	case AV_SAMPLE_FMT_FLTP:
		return true;
	default:
		throw std::runtime_error(format("Unknown libav audio format [%s] (2)", codecCtx->sample_fmt));
	}
}

static
AudioSampleFormat getFormat(const AVCodecContext* codecCtx) {
	switch (codecCtx->sample_fmt) {
	case AV_SAMPLE_FMT_S16: return Modules::S16;
	case AV_SAMPLE_FMT_S16P: return Modules::S16;
	case AV_SAMPLE_FMT_FLT: return Modules::F32;
	case AV_SAMPLE_FMT_FLTP: return Modules::F32;
	default:
		throw std::runtime_error(format("Unknown libav audio format [%s] (3)", codecCtx->sample_fmt));
	}
}

static
AudioLayout getLayout(const AVCodecContext* codecCtx) {
	switch (codecCtx->channel_layout) {
	case AV_CH_LAYOUT_MONO:   return Mono; break;
	case AV_CH_LAYOUT_STEREO: return Stereo; break;
	default:
		switch (codecCtx->channels) {
		case 1: return Mono; break;
		case 2: return Stereo; break;
		default: throw std::runtime_error("Unknown libav audio layout");
		}
	}
}

Metadata createMetadataPktLibavAudio(AVCodecContext* codecCtx) {
	auto meta = make_shared<MetadataPktAudio>();
	initMetadatPkt(meta.get(), codecCtx);
	meta->numChannels = codecCtx->channels;
	meta->planar = meta->numChannels > 1 ? isPlanar(codecCtx) : true;
	meta->sampleRate = codecCtx->sample_rate;
	meta->bitsPerSample = av_get_bytes_per_sample(codecCtx->sample_fmt) * 8;
	meta->frameSize = codecCtx->frame_size;
	meta->format = getFormat(codecCtx);
	meta->layout = getLayout(codecCtx);
	return meta;
}

Metadata createMetadataPktLibavSubtitle(AVCodecContext* codecCtx) {
	auto meta = make_shared<MetadataPktAudio>();
	initMetadatPkt(meta.get(), codecCtx);
	return meta;
}

//conversions
void libavAudioCtxConvertLibav(const Modules::PcmFormat *cfg, int &sampleRate, AVSampleFormat &format, int &numChannels, uint64_t &layout) {
	sampleRate = cfg->sampleRate;

	switch (cfg->layout) {
	case Modules::Mono: layout = AV_CH_LAYOUT_MONO; break;
	case Modules::Stereo: layout = AV_CH_LAYOUT_STEREO; break;
	default: throw std::runtime_error("Unknown libav audio layout");
	}
	numChannels = av_get_channel_layout_nb_channels(layout);
	assert(numChannels == cfg->numChannels);

	switch (cfg->sampleFormat) {
	case Modules::S16: format = av_get_alt_sample_fmt(AV_SAMPLE_FMT_S16, cfg->numPlanes > 1); break;
	case Modules::F32: format = av_get_alt_sample_fmt(AV_SAMPLE_FMT_FLT, cfg->numPlanes > 1); break;
	default: throw std::runtime_error("Unknown libav audio format (1)");
	}
}

void libavAudioCtxConvert(const PcmFormat *cfg, AVCodecContext *codecCtx) {
	libavAudioCtxConvertLibav(cfg, codecCtx->sample_rate, codecCtx->sample_fmt, codecCtx->channels, codecCtx->channel_layout);
}

void libavFrame2pcmConvert(const AVFrame *frame, PcmFormat *cfg) {
	cfg->sampleRate = frame->sample_rate;

	cfg->numChannels = cfg->numPlanes = frame->channels;
	switch (frame->format) {
	case AV_SAMPLE_FMT_S16:
		cfg->sampleFormat = Modules::S16;
		cfg->numPlanes = 1;
		break;
	case AV_SAMPLE_FMT_S16P:
		cfg->sampleFormat = Modules::S16;
		break;
	case AV_SAMPLE_FMT_FLT:
		cfg->sampleFormat = Modules::F32;
		cfg->numPlanes = 1;
		break;
	case AV_SAMPLE_FMT_FLTP:
		cfg->sampleFormat = Modules::F32;
		break;
	default:
		throw std::runtime_error("Unknown libav audio format (3)");
	}

	switch (frame->channel_layout) {
	case AV_CH_LAYOUT_MONO:   cfg->layout = Modules::Mono; break;
	case AV_CH_LAYOUT_STEREO: cfg->layout = Modules::Stereo; break;
	default:
		switch (cfg->numChannels) {
		case 1: cfg->layout = Modules::Mono; break;
		case 2: cfg->layout = Modules::Stereo; break;
		default: throw std::runtime_error("Unknown libav audio layout");
		}
	}
}

void libavFrameDataConvert(const DataPcm *pcmData, AVFrame *frame) {
	auto const& format = pcmData->getFormat();
	AVSampleFormat avsf;
	libavAudioCtxConvertLibav(&format, frame->sample_rate, avsf, frame->channels, frame->channel_layout);
	frame->format = (int)avsf;
	for (int i = 0; i < format.numPlanes; ++i) {
		frame->data[i] = pcmData->getPlane(i);
		if (i == 0)
			frame->linesize[i] = (int)pcmData->getPlaneSize(i) / format.numChannels;
		else
			frame->linesize[i] = 0;
	}
	frame->nb_samples = (int)(pcmData->size() / format.getBytesPerSample());
}

AVPixelFormat pixelFormat2libavPixFmt(PixelFormat format) {
	switch (format) {
	case PixelFormat::Y8: return AV_PIX_FMT_GRAY8;
	case PixelFormat::I420: return AV_PIX_FMT_YUV420P;
	case PixelFormat::YUV420P10LE: return AV_PIX_FMT_YUV420P10LE;
	case PixelFormat::YUV422P: return AV_PIX_FMT_YUV422P;
	case PixelFormat::YUV422P10LE: return AV_PIX_FMT_YUV422P10LE;
	case PixelFormat::YUYV422: return AV_PIX_FMT_YUYV422;
	case PixelFormat::NV12: return AV_PIX_FMT_NV12;
	case PixelFormat::NV12P010LE: return AV_PIX_FMT_P010LE;
	case PixelFormat::RGB24: return AV_PIX_FMT_RGB24;
	case PixelFormat::RGBA32: return AV_PIX_FMT_RGBA;
	default: throw std::runtime_error("Unknown pixel format to convert (1). Please contact your vendor.");
	}
}

PixelFormat libavPixFmt2PixelFormat(AVPixelFormat avPixfmt) {
	switch (avPixfmt) {
	case AV_PIX_FMT_GRAY8: return PixelFormat::Y8;
	case AV_PIX_FMT_YUV420P: case AV_PIX_FMT_YUVJ420P: return PixelFormat::I420;
	case AV_PIX_FMT_YUV420P10LE: return PixelFormat::YUV420P10LE;
	case AV_PIX_FMT_YUV422P: return PixelFormat::YUV422P;
	case AV_PIX_FMT_YUV422P10LE: return PixelFormat::YUV422P10LE;
	case AV_PIX_FMT_YUYV422: return PixelFormat::YUYV422;
	case AV_PIX_FMT_NV12: return PixelFormat::NV12;
	case AV_PIX_FMT_P010LE: return PixelFormat::NV12P010LE;
	case AV_PIX_FMT_RGB24: return PixelFormat::RGB24;
	case AV_PIX_FMT_RGBA: return PixelFormat::RGBA32;
	default: throw std::runtime_error("Unknown pixel format to convert (2). Please contact your vendor.");
	}
}

//DataAVPacket
void AVPacketDeleter::operator()(AVPacket *p) {
	av_packet_unref(p);
	delete(p);
}

DataAVPacket::DataAVPacket(size_t size)
	: pkt(std::unique_ptr<AVPacket, AVPacketDeleter>(new AVPacket)) {
	av_init_packet(pkt.get());
	av_packet_unref(pkt.get());
	if (size)
		av_new_packet(pkt.get(), (int)size);
}

DataAVPacket::~DataAVPacket() {
	g_Log->log(Debug, format("Freeing %s, pts=%s", this, pkt->pts).c_str());
}

Span DataAVPacket::data() {
	return Span { pkt->data, (size_t)pkt->size };
}

SpanC DataAVPacket::data() const {
	return SpanC { pkt->data, (size_t)pkt->size };
}

AVPacket* DataAVPacket::getPacket() const {
	return pkt.get();
}

void DataAVPacket::restamp(int64_t offsetIn180k, uint64_t pktTimescale) const {
	auto const offset = clockToTimescale(offsetIn180k, pktTimescale);
	pkt->dts += offset;
	if (pkt->pts != AV_NOPTS_VALUE) {
		pkt->pts += offset;
	}
}

void DataAVPacket::resize(size_t size) {
	if (av_grow_packet(pkt.get(), size))
		throw std::runtime_error(format("Cannot resize DataAVPacket to size %s (cur=%s)", size, pkt->size));
}

void copyToPicture(AVFrame const* avFrame, DataPicture* pic) {
	for (size_t comp = 0; comp<pic->getNumPlanes(); ++comp) {
		auto const subsampling = comp == 0 ? 1 : 2;
		auto const bytePerPixel = pic->getFormat().format == PixelFormat::YUYV422 ? 2 : 1;
		auto const w = avFrame->width * bytePerPixel / subsampling;
		auto const h = avFrame->height / subsampling;
		auto src = avFrame->data[comp];
		auto const srcPitch = avFrame->linesize[comp];
		auto dst = pic->getPlane(comp);
		auto const dstPitch = pic->getPitch(comp);
		for (int y = 0; y<h; ++y) {
			memcpy(dst, src, w);
			src += srcPitch;
			dst += dstPitch;
		}
	}
}

std::string avStrError(int err) {
	char buffer[256] {};
	av_strerror(err, buffer, sizeof buffer);
	return buffer;
}

}
