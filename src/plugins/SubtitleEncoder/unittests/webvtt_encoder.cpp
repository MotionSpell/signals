#include "../subtitle_encoder.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/subtitle.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "tests/tests.hpp"

using namespace Tests;
using namespace Modules;

namespace {
struct OutStub : ModuleS {
  std::vector<int64_t> times;
  std::vector<std::string> webvtt;
  void processOne(Data data) override {
    webvtt.push_back(std::string((char *)data->data().ptr, data->data().len));
    times.push_back(data->get<PresentationTime>().time);
  }
};
}

unittest("webvtt_encoder") {
  SubtitleEncoderConfig cfg;
  cfg.isWebVTT = true;
  cfg.splitDurationInMs = 1000;
  cfg.maxDelayBeforeEmptyInMs = 2000;
  cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
  auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);

  Page page{0, IClock::Rate * 4, std::vector<Page::Line>({{"toto"}, {"titi", {}, {"#ff0000"}}})}; //FIXME: second style is ignored
  auto data = std::make_shared<DataSubtitle>(0);
  auto const time = page.showTimestamp + IClock::Rate * 4;
  data->set(DecodingTime{time});
  data->set(PresentationTime{time});
  data->page = page;

  auto webvttAnalyzer = createModule<OutStub>();
  ConnectOutputToInput(m->getOutput(0), webvttAnalyzer->getInput(0));

  m->getInput(0)->push(data);

  std::vector<int64_t> expectedTimes = {0, timescaleToClock(cfg.splitDurationInMs, 1000)};
  std::vector<std::string> expectedWebVTT = {R"|(WEBVTT
X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0

STYLE
::cue {
  color: #ffffff;
  background-color: #000000c2;
}

00:00:00.000 --> 00:00:01.000
toto
titi

)|",
        R"|(WEBVTT
X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0

STYLE
::cue {
  color: #ffffff;
  background-color: #000000c2;
}

00:00:01.000 --> 00:00:02.000
toto
titi

)|"};

  ASSERT_EQUALS(expectedTimes, webvttAnalyzer->times);
  ASSERT_EQUALS(expectedWebVTT, webvttAnalyzer->webvtt);
}

unittest("webvtt_encoder: segmentation and empty page") {
  SubtitleEncoderConfig cfg;
  cfg.isWebVTT = true;
  cfg.splitDurationInMs = 1000;
  cfg.maxDelayBeforeEmptyInMs = 2000;
  cfg.forceEmptyPage = true;
  cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
  auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);

  Page page1{IClock::Rate * 0 / 1, IClock::Rate * 1 / 2, std::vector<Page::Line>({{"toto1"}})};
  Page page2{IClock::Rate * 1 / 2, IClock::Rate * 3 / 4, std::vector<Page::Line>({{"toto2.1"}, {"toto2.2"}})};
  Page page3{IClock::Rate * 3 / 4, IClock::Rate * 5 / 4,
        std::vector<Page::Line>({{"toto3", {12, 40}, {"#ffffff", "#000000"}}})};

  auto makeData = [](Page &page, int64_t time) {
    auto data = std::make_shared<DataSubtitle>(0);
    data->set(DecodingTime{time});
    data->set(PresentationTime{time});
    data->page = page;
    return data;
  };

  auto data1 = makeData(page1, IClock::Rate * 1);
  auto data2 = makeData(page2, IClock::Rate * 2);
  auto data3 = makeData(page3, IClock::Rate * 5);

  auto webvttAnalyzer = createModule<OutStub>();
  ConnectOutputToInput(m->getOutput(0), webvttAnalyzer->getInput(0));

  m->getInput(0)->push(data1);
  m->getInput(0)->push(data2);
  m->getInput(0)->push(data3);

  std::vector<int64_t> expectedTimes = {
        0, timescaleToClock(cfg.splitDurationInMs, 1000), timescaleToClock(cfg.splitDurationInMs * 2, 1000)};
  std::vector<std::string> expectedWebVTT = {R"|(WEBVTT
X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0

STYLE
::cue {
  color: #ffffff;
  background-color: #000000c2;
}

00:00:00.000 --> 00:00:00.500
toto1

00:00:00.500 --> 00:00:00.750
toto2.1
toto2.2

00:00:00.750 --> 00:00:01.000 position:50% line:48% size:100% align:center
toto3

)|",
        R"|(WEBVTT
X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0

STYLE
::cue {
  color: #ffffff;
  background-color: #000000;
}

00:00:01.000 --> 00:00:01.250 position:50% line:48% size:100% align:center
toto3

)|",
        R"|(WEBVTT
X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0

STYLE
::cue {
  color: #ffffff;
  background-color: #000000c2;
}

00:00:02.000 --> 00:00:03.000

)|"};

  ASSERT_EQUALS(expectedTimes, webvttAnalyzer->times);
  ASSERT_EQUALS(expectedWebVTT, webvttAnalyzer->webvtt);
}
