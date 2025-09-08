#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata_file.hpp"
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/encode/libav_encode.hpp"
#include "lib_media/mux/mux_mp4_config.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/tools.hpp"
#include "tests/tests.hpp"

#include <iostream> // cerr
#include <vector>

using namespace Tests;
using namespace Modules;
using namespace std;

auto const VIDEO_RESOLUTION = Resolution(320, 180);

static auto createYuvPic(Resolution res) { return make_shared<DataPicture>(res, PixelFormat::I420); }

unittest("encoder: video simple") {
  auto picture = createYuvPic(VIDEO_RESOLUTION);

  int numEncodedFrames = 0;
  auto onFrame = [&](Data /*data*/) { numEncodedFrames++; };

  EncoderConfig cfg{EncoderConfig::Video};
  auto encode = loadModule("Encoder", &NullHost, &cfg);
  ConnectOutput(encode->getOutput(0), onFrame);
  for(int i = 0; i < 37; ++i) {
    picture->set(PresentationTime{timescaleToClock(i, 25)}); // avoid warning about non-monotonic pts
    encode->getInput(0)->push(picture);
    encode->process();
  }
  encode->flush();

  ASSERT_EQUALS(37, numEncodedFrames);
}

static shared_ptr<DataBase> createPcm(int samples) {
  PcmFormat fmt;
  fmt.sampleRate = 44100;
  const auto inFrameSizeInBytes = (size_t)(samples * fmt.getBytesPerSample() / fmt.numPlanes);
  auto pcm = make_shared<DataPcm>(samples, fmt);
  std::vector<uint8_t> input(inFrameSizeInBytes);
  auto inputRaw = input.data();
  for(int i = 0; i < fmt.numPlanes; ++i)
    memcpy(pcm->getPlane(i), inputRaw, inFrameSizeInBytes);
  return pcm;
}

unittest("encoder: audio timestamp passthrough (modulo priming)") {

  vector<int64_t> times;
  auto onFrame = [&](Data data) { times.push_back(data->get<PresentationTime>().time); };

  vector<int64_t> inputTimes = {10000, 20000, 30000, 777000};

  EncoderConfig cfg{EncoderConfig::Audio};
  auto encode = loadModule("Encoder", &NullHost, &cfg);
  ConnectOutput(encode->getOutput(0), onFrame);

  for(auto time : inputTimes) {
    auto pcm = createPcm(1024);
    pcm->set(PresentationTime{time});
    encode->getInput(0)->push(pcm);
    encode->process();
  }
  encode->flush();

  int primingDuration = (1024 * IClock::Rate + 44100 - 1) / 44100;
  vector<int64_t> expected = {10000 - primingDuration, 10000, 20000, 30000, 777000};
  ASSERT_EQUALS(expected, times);
}

unittest("encoder: video timestamp passthrough") {
  vector<int64_t> times;
  auto onFrame = [&](Data data) { times.push_back(data->get<PresentationTime>().time); };

  vector<int64_t> inputTimes = {0, 1, 2, 3, 4, 20, 21, 22, 10, 11, 12};

  EncoderConfig cfg;
  cfg.type = EncoderConfig::Video;
  cfg.frameRate = Fraction(IClock::Rate);
  auto encode = loadModule("Encoder", &NullHost, &cfg);
  ConnectOutput(encode->getOutput(0), onFrame);
  for(auto time : inputTimes) {
    auto picture = createYuvPic(VIDEO_RESOLUTION);
    picture->set(PresentationTime{time});
    encode->getInput(0)->push(picture);
    encode->process();
  }
  encode->flush();

  vector<int64_t> expected = {0, 1, 2, 3, 4, 20, 21, 22, 10, 11, 12};
  ASSERT_EQUALS(expected, times);
}

void RAPTest(const Fraction fps, const Fraction gopSize, const vector<uint64_t> &times, const vector<bool> &RAPs) {
  EncoderConfig p{EncoderConfig::Video};
  p.frameRate = fps;
  p.GOPSize = gopSize;
  auto picture = createYuvPic(VIDEO_RESOLUTION);
  auto encode = loadModule("Encoder", &NullHost, &p);
  size_t i = 0;
  auto onFrame = [&](Data data) {
    if(i < RAPs.size()) {
      auto keyframe = data->get<CueFlags>().keyframe;
      ASSERT(keyframe == RAPs[i]);
    }
    i++;
  };
  ConnectOutput(encode->getOutput(0), onFrame);
  for(size_t i = 0; i < times.size(); ++i) {
    picture->set(PresentationTime{(int64_t)times[i]});
    encode->getInput(0)->push(picture);
    encode->process();
  }
  encode->flush();
  ASSERT(i == RAPs.size());
}

unittest("encoder: RAP placement (25/1 fps)") {
  const vector<uint64_t> times = {0, IClock::Rate / 2, IClock::Rate, IClock::Rate * 3 / 2, IClock::Rate * 2};
  const vector<bool> RAPs = {true, false, true, false, true};
  auto const fps = Fraction(25, 1);
  RAPTest(fps, fps, times, RAPs);
}

unittest("encoder: RAP placement (30000/1001 fps)") {
  const vector<uint64_t> times = {0, IClock::Rate / 2, IClock::Rate, IClock::Rate * 3 / 2, IClock::Rate * 2};
  const vector<bool> RAPs = {true, false, true, false, true};
  auto const fps = Fraction(30000, 1001);
  RAPTest(fps, fps, times, RAPs);
}

unittest("encoder: RAP placement (25/1 fps, 3.84s GOP)") {
  auto const fps = Fraction(25, 1);
  auto const gop = Fraction(3840, 1000) * fps;
  vector<uint64_t> times;
  vector<bool> RAPs;
  Fraction t(0);
  while(t <= gop / fps) {
    times.push_back(fractionToClock(t));
    if(t == 0 || t == gop / fps)
      RAPs.push_back(true);
    else
      RAPs.push_back(false);
    t = t + fps.inverse();
  }
  RAPTest(fps, gop, times, RAPs);
}

unittest("encoder: RAP placement (noisy)") {
  const auto &ms = std::bind(timescaleToClock<uint64_t>, std::placeholders::_1, 1000);
  const vector<uint64_t> times = {0, ms(330), ms(660), ms(990), ms(1330), ms(1660)};
  const vector<bool> RAPs = {true, false, false, true, false, false};
  auto const fps = Fraction(3, 1);
  RAPTest(fps, fps, times, RAPs);
}

unittest("encoder: RAP placement (incorrect timings)") {
  const vector<uint64_t> times = {0, 0, IClock::Rate};
  const vector<bool> RAPs = {true, false, true};
  auto const fps = Fraction(25, 1);
  RAPTest(fps, fps, times, RAPs);
}
