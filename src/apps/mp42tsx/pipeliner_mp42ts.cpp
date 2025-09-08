#include "pipeliner_mp42ts.hpp"

#include "lib_utils/system_clock.hpp"

// modules
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/out/file.hpp"
#include "plugins/TsMuxer/mpegts_muxer.hpp"

using namespace Modules;
using namespace Pipelines;

void declarePipeline(Pipeline &pipeline, const mp42tsXOptions &opt) {
  DemuxConfig cfg;
  cfg.url = opt.url;
  auto demux = pipeline.add("LibavDemux", &cfg);

  TsMuxerConfig muxCfg{};
  muxCfg.muxRate = 5 * 1000 * 1000;
  auto mux = pipeline.add("TsMuxer", &muxCfg);
  for(int i = 0; i < demux->getNumOutputs(); ++i) {
    auto flow = GetOutputPin(demux, i);
    pipeline.connect(flow, GetInputPin(mux, i));
  }

  auto file = pipeline.addModule<Out::File>(opt.output);
  pipeline.connect(mux, file);
}
