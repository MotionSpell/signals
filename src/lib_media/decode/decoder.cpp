#include "decoder.hpp"

#include "../common/attributes.hpp"
#include "../common/ffpp.hpp"
#include "../common/libav.hpp"
#include "../common/libav_hw.hpp"
#include "../common/metadata.hpp"
#include "../common/pcm.hpp"
#include "../common/picture_allocator.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp"

#include <cassert>

extern "C" {
#include <libavcodec/avcodec.h> // avcodec_open2
#include <libavutil/imgutils.h> // av_image_fill_linesizes
}

using namespace Modules;
auto const USE_CUVID_DEINTERLACE = false;

namespace {

void lavc_ReleaseFrame(void *opaque, uint8_t * /*data*/) {
  delete static_cast<PictureAllocator::PictureContext *>(opaque);
}

int avGetBuffer2(struct AVCodecContext *ctx, AVFrame *frame, int /*flags*/) {
  try {
    int w = frame->width, wAlign = frame->width, hAlign = frame->height, unaligned = 0, linesize[4] = {},
        linesize_align[4] = {};
    avcodec_align_dimensions2(ctx, &w, &hAlign, linesize_align);
    do {
      auto ret = av_image_fill_linesizes(linesize, ctx->pix_fmt, w);
      if(ret < 0)
        return ret;
      wAlign = w;
      // increase alignment of w for next try (rhs gives the lowest bit set in w)
      w += w & ~(w - 1);
      unaligned = 0;
      for(int i = 0; i < 4; i++)
        unaligned |= linesize[i] % linesize_align[i];
    } while(unaligned);
    auto dr = static_cast<PictureAllocator *>(ctx->opaque);
    auto picCtx = dr->getPicture(Resolution(frame->width, frame->height), Resolution(wAlign, hAlign),
          libavPixFmt2PixelFormat((AVPixelFormat)frame->format));
    if(!picCtx->pic)
      return -1;
    frame->opaque = picCtx;
    auto pic = picCtx->pic.get();
    for(size_t i = 0; i < AV_NUM_DATA_POINTERS; ++i) {
      frame->data[i] = NULL;
      frame->linesize[i] = 0;
      frame->buf[i] = NULL;
    }
    for(int i = 0; i < pic->getNumPlanes(); ++i) {
      frame->data[i] = pic->getPlane(i);
      frame->linesize[i] = (int)pic->getStride(i);
      assert(!(pic->getStride(i) % linesize_align[i]));
      frame->buf[i] = av_buffer_create(
            frame->data[i], frame->linesize[i], lavc_ReleaseFrame, i == 0 ? (void *)picCtx : nullptr, 0);
    }
    return 0;
  } catch(...) {
    return -1;
  }
}

struct Decoder : ModuleS, PictureAllocator {
  Decoder(KHost *host, DecoderConfig *cfg)
      : m_host(host)
      , avFrame(new ffpp::Frame)
      , hw(cfg->hw) {
    mediaOutput = addOutput();
    output = mediaOutput;

    if(cfg->type == VIDEO_PKT) {
      output->setMetadata(
            cfg->hw ? safe_cast<MetadataRawVideo>(make_shared<MetadataRawVideoHw>()) : make_shared<MetadataRawVideo>());
      getDecompressedData = std::bind(&Decoder::processVideo, this);
    } else if(cfg->type == AUDIO_PKT) {
      output->setMetadata(make_shared<MetadataRawAudio>());
      getDecompressedData = std::bind(&Decoder::processAudio, this);
    } else
      throw error("Can only decode audio or video.");
  }

  ~Decoder() {
    av_parser_close(parser);
    av_buffer_unref((AVBufferRef **)&hw->device);
  }

  void processOne(Data data) override {
    auto flags = data->get<CueFlags>();

    if(flags.discontinuity) {
      if(codecCtx) {
        m_host->log(Warning, "Discontinuity: flushing reframer and decoder");
        flush();
        // if (codecCtx->codec->capabilities & AV_CODEC_CAP_ENCODER_FLUSH)
        avcodec_flush_buffers(codecCtx.get());
        av_parser_close(parser);
        parser = nullptr;
      }

      if(!data->getMetadata())
        return; // empty discontinuity data
    }

    inputs[0]->updateMetadata(data);

    if(!codecCtx) {
      auto meta = data->getMetadata();
      if(!meta)
        throw error("Can't instantiate decoder: no metadata for input data");

      openDecoder(safe_cast<const MetadataPkt>(meta.get()));
    }

    assert(codecCtx);

    if(isDeclaration(data))
      return;

    assert(data && data->getMetadata());

    if(flags.unframed)
      ensureParser(safe_cast<const MetadataPkt>(data->getMetadata()).get());

    AVPacket pkt{};
    pkt.pts = data->get<PresentationTime>().time;
    try {
      pkt.dts = data->get<DecodingTime>().time;
    } catch(...) {
      pkt.dts = data->get<PresentationTime>().time;
    }
    pkt.data = (uint8_t *)data->data().ptr;
    pkt.size = (int)data->data().len;
    processPacket(&pkt);
  }

  void flush() override {
    if(codecCtx.get())
      processPacket(nullptr);
  }

  private:
  void openDecoder(const MetadataPkt *metadata) {
    auto avcodecId = signalsIdToAvCodecId(metadata->codec.c_str());
    const AVCodec *codec = nullptr;
    if(!hw) {
      codec = avcodec_find_decoder((AVCodecID)avcodecId);
    } else if(hw) {
      if(avcodecId == AV_CODEC_ID_H264) {
        codec = avcodec_find_decoder_by_name("h264_cuvid");

      } else if(avcodecId == AV_CODEC_ID_HEVC) {
        codec = avcodec_find_decoder_by_name("hevc_cuvid");
      }
    }
    if(!codec)
      throw error(format("Decoder not found for codec '%s' (hardware=%s).", metadata->codec, hw != nullptr));

    codecCtx = shptr(avcodec_alloc_context3(codec));
    codecCtx->pkt_timebase.num = IClock::Rate;
    codecCtx->pkt_timebase.den = 1;

    // copy extradata: this allows decoding headerless bitstreams
    // (i.e AVCC / H264-in-mp4).
    {
      auto const extradata = metadata->codecSpecificInfo;

      codecCtx->extradata = (uint8_t *)av_calloc(1, extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
      codecCtx->extradata_size = (int)extradata.size();
      if(extradata.data())
        memcpy(codecCtx->extradata, extradata.data(), extradata.size());
    }

    if(metadata->codec == "raw_video") {
      auto const m = safe_cast<const MetadataPktVideo>(metadata);
      codecCtx->pix_fmt = pixelFormat2libavPixFmt(m->pixelFormat);
      codecCtx->width = m->resolution.width;
      codecCtx->height = m->resolution.height;
    }

    if(metadata->codec == "raw_audio") {
      auto const m = safe_cast<const MetadataPktAudio>(metadata);
      av_channel_layout_default(&codecCtx->ch_layout, m->numChannels);
      codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
      codecCtx->sample_rate = m->sampleRate;
    }

    if(hw) {
      codecCtx->hw_device_ctx = av_buffer_ref((AVBufferRef *)hw->device);
    }

    std::string options = "-threads auto -err_detect 1 -flags output_corrupt -flags2 showall ";
    if(USE_CUVID_DEINTERLACE) {
      assert(hw);
      options += "-deint adaptive ";
    }
    ffpp::Dict dict(typeid(*this).name(), options);

    if(avcodec_open2(codecCtx.get(), codec, &dict) < 0)
      throw error("Couldn't open stream.");

    if(codecCtx->codec->type == AVMEDIA_TYPE_VIDEO) {
      if(codecCtx->codec->capabilities & AV_CODEC_CAP_DR1) {
        codecCtx->opaque = static_cast<PictureAllocator *>(this);
        codecCtx->get_buffer2 = avGetBuffer2;

        // use a large number: some H.264 streams require up to 14 simultaneous buffers.
        // this memory is lazily allocated anyway.
        allocatorSize = 64;
        mediaOutput->resetAllocator(allocatorSize);
      }
    }
  }

  void ensureParser(const MetadataPkt *metadata) {
    if(!parser) {
      parser = av_parser_init(codecCtx->codec_id);
      if(!parser)
        throw error(format("Parser not found for codec \"%s\". If reframining", metadata->codec));

      if(codecCtx->codec_id == AV_CODEC_ID_AAC)
        parser->duration = 1024; // ffmpeg's code excludes ADTS data: put a default
    }
  }

  std::shared_ptr<DataBase> processAudio() {
    PcmFormat pcmFormat;
    libavFrame2pcmConvert(avFrame->get(), &pcmFormat);
    auto out = mediaOutput->allocData<DataPcm>(avFrame->get()->nb_samples, pcmFormat);
    for(int i = 0; i < pcmFormat.numPlanes; ++i) {
      memcpy(out->getPlane(i), avFrame->get()->data[i],
            avFrame->get()->nb_samples * pcmFormat.getBytesPerSample() / pcmFormat.numPlanes);
    }

    return out;
  }

  std::shared_ptr<DataBase> processVideo() {
    std::shared_ptr<DataPicture> pic;
    if(auto ctx = static_cast<PictureContext *>(avFrame->get()->opaque)) {
      pic = ctx->pic;
      ctx->pic->setVisibleResolution(Resolution(codecCtx->width, codecCtx->height));
    } else {
      pic = mediaOutput->allocData<DataPicture>(Resolution(avFrame->get()->width, avFrame->get()->height),
            libavPixFmt2PixelFormat((AVPixelFormat)avFrame->get()->format));
      copyToPicture(avFrame->get(), pic.get());
    }

    if(hw) {
      auto metadataOut = make_shared<MetadataRawVideoHw>();
      for(int i = 0; i < AV_NUM_DATA_POINTERS && avFrame->get()->buf[i]; ++i) {
        metadataOut->dataRef[i] = av_buffer_ref(avFrame->get()->buf[i]);
      }
      metadataOut->framesCtx = av_buffer_ref(avFrame->get()->hw_frames_ctx);
      metadataOut->deviceCtx = av_buffer_ref(codecCtx->hw_device_ctx);
      output->setMetadata(metadataOut);
    }

    return pic;
  }

  PictureAllocator::PictureContext *getPicture(Resolution res, Resolution resInternal, PixelFormat format) override {
    auto ctx = new PictureAllocator::PictureContext;
    ctx->pic = mediaOutput->allocData<DataPicture>(res, resInternal, format);
    return ctx;
  }

  void decodePacket(AVPacket const *const packet) {
    auto ret = avcodec_send_packet(codecCtx.get(), packet);
    if(ret < 0)
      m_host->log(Warning, format("Decoding error: %s", avStrError(ret)).c_str());

    while(1) {
      auto ret = avcodec_receive_frame(codecCtx.get(), avFrame->get());
      if(ret != 0)
        break; // no more frames

      if(avFrame->get()->decode_error_flags || (avFrame->get()->flags & AV_FRAME_FLAG_CORRUPT))
        m_host->log(Warning, "Corrupted frame decoded");

      auto data = getDecompressedData();
      data->set(PresentationTime{avFrame->get()->pts});
      output->post(data);
    }
  }

  void reframeAndDecodePacket(AVPacket const *const pkt) {
    uint8_t *data = nullptr;
    int size = 0, side_data_elems = 0;
    int64_t pts = AV_NOPTS_VALUE, dts = AV_NOPTS_VALUE, pos = 0;
    AVPacket out_pkt{};
    AVPacketSideData *side_data = nullptr;
    auto const flush = !pkt;

    if(pkt) {
      data = pkt->data;
      size = pkt->size;
      pts = pkt->pts;
      dts = pkt->dts;
      pos = pkt->pos;
      side_data = pkt->side_data;
      side_data_elems = pkt->side_data_elems;
    }

    // extract frames from input packet
    while(size > 0 || flush) {
      auto len = av_parser_parse2(parser, codecCtx.get(), &out_pkt.data, &out_pkt.size, data, size, pts, dts, pos);
      if(len < 0) {
        // if we need to handle the case of incomplete frame, the code may need to be moved in a separate module
        m_host->log(Error, "Error while parsing: if this error happens in a loop, contact your vendor");
        break;
      }

      // resync on input media times when available
      if(parser->pts != AV_NOPTS_VALUE)
        out_pkt.pts = parser->pts;

      // no data: save last pts
      if(!out_pkt.size) {
        parser->pts = out_pkt.pts;
        break;
      }

      pos = 0;
      data += len;
      size -= len;
      pts = dts = AV_NOPTS_VALUE;

      if(side_data) {
        out_pkt.side_data = side_data;
        out_pkt.side_data_elems = side_data_elems;
        side_data = NULL;
        side_data_elems = 0;
      }

      if(parser->key_frame == 1 || (parser->key_frame == -1 && parser->pict_type == AV_PICTURE_TYPE_I)) {
        out_pkt.flags |= AV_PKT_FLAG_KEY;
      }
      if(pkt) {
        out_pkt.flags |= pkt->flags & (AV_PKT_FLAG_DISCARD | AV_PKT_FLAG_CORRUPT);
        if(parser->key_frame == -1 && parser->pict_type == AV_PICTURE_TYPE_NONE && (pkt->flags & AV_PKT_FLAG_KEY))
          out_pkt.flags |= AV_PKT_FLAG_KEY;
      }

      // some parsers (e.g. ac3) require to dispatch each frame
      decodePacket(&out_pkt);

      if(parser->duration > 0) {
        auto const duration = outputs[0]->getMetadata()->isAudio()
              ? Fraction(parser->duration, codecCtx->sample_rate)
              : Fraction(parser->duration * codecCtx->time_base.den, codecCtx->time_base.num);
        if(duration.den)
          out_pkt.pts += fractionToClock(duration);
      }
    }

    if(flush)
      decodePacket(nullptr);
  }

  void processPacket(AVPacket const *const pkt) {
    if(parser) {
      reframeAndDecodePacket(pkt);
    } else {
      decodePacket(pkt);
    }
  }

  KHost *const m_host;
  std::shared_ptr<AVCodecContext> codecCtx;
  AVCodecParserContext *parser = nullptr;
  std::unique_ptr<ffpp::Frame> const avFrame;
  std::shared_ptr<HardwareContextCuda> hw;
  OutputDefault *mediaOutput = nullptr; // used for allocation
  KOutput *output = nullptr;
  std::function<std::shared_ptr<DataBase>(void)> getDecompressedData;
};

IModule *createObject(KHost *host, void *va) {
  auto cfg = (DecoderConfig *)va;
  enforce(host, "Decoder: host can't be NULL");
  return createModule<Decoder>(host, cfg).release();
}

auto const registered = Factory::registerModule("Decoder", &createObject);

}
