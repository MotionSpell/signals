#include "libavfilter.hpp"

#include "../common/attributes.hpp"
#include "../common/ffpp.hpp"
#include "../common/libav.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/tools.hpp"

#include <string>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/hwcontext.h>
}

using namespace Modules;

struct AVFilter;
struct AVFilterGraph;
struct AVFilterContext;

namespace {

class LibavFilter : public ModuleS {
  public:
  LibavFilter(KHost *host, const AvFilterConfig &cfg);
  ~LibavFilter();
  void processOne(Data data) override;
  void flush() override;

  private:
  KHost *const m_host;
  OutputDefault *output = nullptr;
  AVFilterGraph *graph = nullptr;
  AVFilterContext *buffersrc_ctx = nullptr, *buffersink_ctx = nullptr;
  std::unique_ptr<ffpp::Frame> const avFrameIn, avFrameOut;
  const AvFilterConfig cfg;
};

LibavFilter::LibavFilter(KHost *host, const AvFilterConfig &cfg)
    : m_host(host)
    , graph(avfilter_graph_alloc())
    , avFrameIn(new ffpp::Frame)
    , avFrameOut(new ffpp::Frame)
    , cfg(cfg) {
  input->setMetadata(cfg.isHardwareFilter ? safe_cast<MetadataRawVideo>(make_shared<MetadataRawVideoHw>())
                                          : make_shared<MetadataRawVideo>());
  output = addOutput();
  output->setMetadata(cfg.isHardwareFilter ? safe_cast<MetadataRawVideo>(make_shared<MetadataRawVideoHw>())
                                           : make_shared<MetadataRawVideo>());
}

LibavFilter::~LibavFilter() { avfilter_graph_free(&graph); }

void LibavFilter::flush() { processOne(nullptr); }

void LibavFilter::processOne(Data data) {
  if(!buffersrc_ctx) {
    if(!data)
      return; // likely flushing before any data arrived

    char args[512];
    const auto format = safe_cast<const DataPicture>(data)->getFormat();
    AVPixelFormat pf = pixelFormat2libavPixFmt(format.format);
    snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", format.res.width,
          format.res.height, pf, 1, (int)IClock::Rate, format.res.width, format.res.height);
    auto ret = avfilter_graph_create_filter(&buffersrc_ctx, avfilter_get_by_name("buffer"), "in", args, nullptr, graph);
    if(ret < 0)
      throw error("Cannot create filter source");

    ret = avfilter_graph_create_filter(
          &buffersink_ctx, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr, graph);
    if(ret < 0)
      throw error("Cannot create filter destination");

    auto srcParams = av_buffersrc_parameters_alloc();
    if(!srcParams)
      throw error("Cannot create filter source param");
    memset(srcParams, 0, sizeof(*srcParams));

    srcParams->format = pixelFormat2libavPixFmt(format.format);
    if(cfg.isHardwareFilter) {
      buffersrc_ctx->hw_device_ctx = av_buffer_ref(safe_cast<const MetadataRawVideoHw>(data->getMetadata())->deviceCtx);
      srcParams->hw_frames_ctx = av_buffer_ref(safe_cast<const MetadataRawVideoHw>(data->getMetadata())->framesCtx);
    }
    srcParams->width = format.res.width;
    srcParams->height = format.res.height;
    srcParams->time_base.num = 1;
    srcParams->time_base.den = (int)IClock::Rate;
    srcParams->sample_aspect_ratio.num = format.res.width;
    srcParams->sample_aspect_ratio.den = format.res.height;
    ret = av_buffersrc_parameters_set(buffersrc_ctx, srcParams);
    if(ret < 0)
      throw error("Cannot add filter source hardware param");
    if(cfg.isHardwareFilter) {
      av_buffer_unref(&srcParams->hw_frames_ctx);
    }
    av_free(srcParams);

    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    AVFilterInOut *inputs = avfilter_inout_alloc();
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ret = avfilter_graph_parse(graph, cfg.filterArgs.c_str(), inputs, outputs, NULL);
    if(ret < 0)
      throw error("Cannot parse filter graph");

    if(cfg.isHardwareFilter) {
      for(unsigned i = 0; i < graph->nb_filters; i++)
        graph->filters[i]->hw_device_ctx =
              av_buffer_ref(safe_cast<const MetadataRawVideoHw>(data->getMetadata())->deviceCtx);
    }

    ret = avfilter_graph_config(graph, NULL);
    if(ret < 0)
      throw error("Cannot config filter graph");
  }

  if(data) {
    const auto pic = safe_cast<const DataPicture>(data);
    avFrameIn->get()->pict_type = AV_PICTURE_TYPE_NONE;
    avFrameIn->get()->format = (int)pixelFormat2libavPixFmt(pic->getFormat().format);

    for(int i = 0; i < pic->getNumPlanes(); ++i) {
      avFrameIn->get()->width = pic->getFormat().res.width;
      avFrameIn->get()->height = pic->getFormat().res.height;
      avFrameIn->get()->data[i] = (uint8_t *)pic->getPlane(i);
      avFrameIn->get()->linesize[i] = (int)pic->getStride(i);
    }
    avFrameIn->get()->pts = data->get<PresentationTime>().time;

    if(cfg.isHardwareFilter) {
      auto meta = safe_cast<const MetadataRawVideoHw>(data->getMetadata());
      for(int i = 0; i < AV_NUM_DATA_POINTERS && meta->dataRef[i]; ++i) {
        avFrameIn->get()->buf[i] = av_buffer_ref(meta->dataRef[i]);
      }
      avFrameIn->get()->hw_frames_ctx = av_buffer_ref(meta->framesCtx);
    }

    if(av_buffersrc_add_frame_flags(buffersrc_ctx, avFrameIn->get(), AV_BUFFERSRC_FLAG_PUSH) < 0)
      throw error("Error while feeding the filtergraph");
  }

  while(1) {
    auto const ret = av_buffersink_get_frame(buffersink_ctx, avFrameOut->get());
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }
    if(ret < 0) {
      m_host->log(Error, "Corrupted filter video frame.");
    }
    auto pic = output->allocData<DataPicture>(
          Resolution(av_buffersink_get_w(buffersink_ctx), av_buffersink_get_h(buffersink_ctx)),
          libavPixFmt2PixelFormat((AVPixelFormat)av_buffersink_get_format(buffersink_ctx)));
    copyToPicture(avFrameOut->get(), pic.get());
    pic->set(PresentationTime{
          av_rescale_q(avFrameOut->get()->pts, buffersink_ctx->inputs[0]->time_base, {1, (int)IClock::Rate})});

    if(cfg.isHardwareFilter) {
      auto metadataOut = make_shared<MetadataRawVideoHw>();
      for(int i = 0; i < AV_NUM_DATA_POINTERS && avFrameOut->get()->buf[i]; ++i) {
        metadataOut->dataRef[i] = av_buffer_ref(avFrameOut->get()->buf[i]);
      }
      metadataOut->framesCtx = av_buffer_ref(avFrameOut->get()->hw_frames_ctx);
      metadataOut->deviceCtx = av_buffer_ref(buffersink_ctx->hw_device_ctx);
      output->setMetadata(metadataOut);
    } else {
      output->setMetadata(make_shared<MetadataRawVideo>());
    }
    output->post(pic);

    av_frame_unref(avFrameOut->get());
  }

  av_frame_unref(avFrameIn->get());
}

IModule *createObject(KHost *host, void *va) {
  auto config = (AvFilterConfig *)va;
  enforce(host, "LibavFilter: host can't be NULL");
  enforce(config, "LibavFilter: config can't be NULL");
  return createModule<LibavFilter>(host, *config).release();
}

auto const registered = Factory::registerModule("LibavFilter", &createObject);
}
