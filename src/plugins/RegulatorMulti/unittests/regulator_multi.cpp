#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp" //MetadataPkt
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/clock.hpp"
#include "tests/tests.hpp"
#include <plugins/RegulatorMulti/regulator_multi.hpp>

using namespace std;
using namespace Tests;
using namespace Modules;

namespace {

struct FrameCounter : ModuleS {
  void processOne(Data data) override {
    if(data->get<CueFlags>().discontinuity) {
      ++discontinuities;
    } else {
      ++frameCount;
    }
  }
  int frameCount = 0;
  int discontinuities = 0;
};

struct ClockMock : IClock {
  Fraction now() const override { return time; }
  void set(int timeInMs) { time = Fraction(timeInMs, 1000); }
  Fraction time = Fraction(0);
};

unittest("RegulatorMulti: video is sent in advance + flush()") {
  auto clock = make_shared<ClockMock>();
  RegulatorMultiConfig rmCfg;
  rmCfg.clock = clock;
  auto reg = loadModule("RegulatorMulti", &NullHost, &rmCfg);
  auto rec = createModule<FrameCounter>();
  vector<shared_ptr<MetadataPkt>> meta = {
        make_shared<MetadataPktAudio>(), make_shared<MetadataPktVideo>(), make_shared<MetadataPktSubtitle>()};
  for(size_t i = 0; i < meta.size(); ++i) {
    reg->getInput(i)->connect();
    ConnectOutputToInput(reg->getOutput(i), rec->getInput(0));
  }

  auto push = [&](int index, int64_t timeInMs) {
    auto pkt = make_shared<DataRaw>(1);
    pkt->set(DecodingTime{timescaleToClock(timeInMs, 1000)});
    pkt->set(CueFlags{});
    pkt->setMetadata(meta[index]);
    reg->getInput(index)->push(pkt);
  };

  push(0, 0);
  push(1, rmCfg.maxMediaTimeDelayInMs); // video
  push(2, 0);
  ASSERT_EQUALS(3, rec->frameCount); // init: dispatched immediately
  ASSERT_EQUALS(3, rec->discontinuities); // 3 init packets

  push(0, 0);
  push(1, rmCfg.maxMediaTimeDelayInMs); // video
  push(2, 0);
  ASSERT_EQUALS(3, rec->frameCount);

  clock->set(rmCfg.maxMediaTimeDelayInMs + rmCfg.maxClockTimeDelayInMs);
  push(0, rmCfg.maxMediaTimeDelayInMs + 1);
  ASSERT_EQUALS(5, rec->frameCount);
  clock->set(2 * (rmCfg.maxMediaTimeDelayInMs + rmCfg.maxClockTimeDelayInMs) + 1);
  push(0, rmCfg.maxMediaTimeDelayInMs + 1); // data is discarded
  ASSERT_EQUALS(5, rec->frameCount);
  ASSERT_EQUALS(3, rec->discontinuities); // one more for idx=0
  push(0, 0);
  reg->flush();
  ASSERT_EQUALS(6, rec->frameCount);
  ASSERT_EQUALS(4, rec->discontinuities); // one more for idx=0
}

unittest("RegulatorMulti: backward discontinuity") {
  auto clock = make_shared<ClockMock>();
  RegulatorMultiConfig rmCfg;
  rmCfg.clock = clock;
  auto reg = loadModule("RegulatorMulti", &NullHost, &rmCfg);
  auto rec = createModule<FrameCounter>();
  vector<shared_ptr<MetadataPkt>> meta = {
        make_shared<MetadataPktAudio>(), make_shared<MetadataPktVideo>(), make_shared<MetadataPktSubtitle>()};
  for(size_t i = 0; i < meta.size(); ++i) {
    reg->getInput(i)->connect();
    ConnectOutputToInput(reg->getOutput(i), rec->getInput(0));
  }

  auto push = [&](int index, int64_t timeInMs) {
    auto pkt = make_shared<DataRaw>(1);
    pkt->set(DecodingTime{timescaleToClock(timeInMs, 1000)});
    pkt->set(CueFlags{});
    pkt->setMetadata(meta[index]);
    reg->getInput(index)->push(pkt);
  };

  push(0, 0);
  push(1, rmCfg.maxMediaTimeDelayInMs); // video
  ASSERT_EQUALS(2, rec->frameCount);
  ASSERT_EQUALS(2, rec->discontinuities); // 2 init packets

  auto ct = rmCfg.maxMediaTimeDelayInMs + rmCfg.maxClockTimeDelayInMs + 1;
  clock->set(ct);
  push(0, 20 * rmCfg.maxMediaTimeDelayInMs);
  ASSERT_EQUALS(2, rec->frameCount);
  push(0, 0); // backward media time
  ASSERT_EQUALS(4, rec->frameCount);
  ASSERT_EQUALS(2, rec->discontinuities);

  ct *= 2;
  clock->set(ct);
  push(0, 0); // follows a discontinuity: dispatched immediately
  ASSERT_EQUALS(4, rec->frameCount);
  push(1, 1); // same media time as clock time
  reg->flush();
  ASSERT_EQUALS(6, rec->frameCount);
  ASSERT_EQUALS(2, rec->discontinuities);
}

}
