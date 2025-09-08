#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/decode/decoder.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_modules/core/connection.hpp"
#include "lib_modules/core/database.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/loader.hpp"
#include "tests/tests.hpp"

#include <algorithm> // is_sorted
#include <vector>

using namespace Tests;
using namespace Modules;
using namespace std;

unittest("LibavDemux => Decoder: output media times must increase") {
  vector<int64_t> mediaTimes;
  auto onPic = [&](Data data) { mediaTimes.push_back(data->get<PresentationTime>().time); };

  DemuxConfig cfg;
  cfg.url = "data/h264.ts";
  auto demux = loadModule("LibavDemux", &NullHost, &cfg);
  DecoderConfig decCfg;
  decCfg.type = VIDEO_PKT;
  auto decode = loadModule("Decoder", &NullHost, &decCfg);
  ConnectOutputToInput(demux->getOutput(0), decode->getInput(0));
  ConnectOutput(decode->getOutput(0), onPic);

  for(int i = 0; i < 100; ++i)
    demux->process();

  demux->flush();
  decode->flush();

  // output media times must increase
  ASSERT(is_sorted(mediaTimes.begin(), mediaTimes.end()));

  // all pictures must be there
  ASSERT_EQUALS(75, (int)mediaTimes.size());
}
