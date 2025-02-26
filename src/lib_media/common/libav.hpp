#pragma once

#include "picture.hpp"
#include "pcm.hpp"
#include "metadata.hpp"
#include <memory>

struct AVPacket;
struct AVCodecContext;

extern "C" {
#include <libavutil/frame.h> // AVFrame
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}

std::shared_ptr<AVCodecContext> shptr(AVCodecContext *p);

namespace Modules {

const char* avCodecIdToSignalsId(int avCodecId);
int signalsIdToAvCodecId(const char* name);

Metadata createMetadataPktLibavVideo(AVCodecContext* codecCtx);
Metadata createMetadataPktLibavAudio(AVCodecContext* codecCtx);
Metadata createMetadataPktLibavSubtitle(AVCodecContext* codecCtx);

class PcmFormat;
struct DataPcm;
void libavAudioCtxConvertLibav(const PcmFormat *cfg, int &sampleRate, enum AVSampleFormat &format, AVChannelLayout *layout);
void libavAudioCtxConvert(const PcmFormat *cfg, AVCodecContext *codecCtx);
void libavFrameDataConvert(const DataPcm *data, AVFrame *frame);
void libavFrame2pcmConvert(const AVFrame *frame, PcmFormat *cfg);

AVPixelFormat pixelFormat2libavPixFmt(PixelFormat format);
PixelFormat libavPixFmt2PixelFormat(AVPixelFormat avPixfmt);

void copyToPicture(AVFrame const* avFrame, DataPicture* pic);
extern "C" int avGetBuffer2(struct AVCodecContext *s, AVFrame *frame, int flags);

std::string avStrError(int err);

}

