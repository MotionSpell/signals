#include "lib_media/common/attributes.hpp"
#include "lib_media/in/sound_generator.hpp"
#include "lib_media/in/video_generator.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/sysclock.hpp"
#include "lib_utils/tools.hpp"
#include "tests/tests.hpp"

#include <thread> // std::this_thread

using namespace Tests;
using namespace Modules;
using namespace std::chrono;

namespace {

secondclasstest("render: sound generator") {
  auto soundGen = createModule<In::SoundGenerator>(&NullHost);
  auto render = Modules::loadModule("SDLAudio", &NullHost, nullptr);

  ConnectOutputToInput(soundGen->getOutput(0), render->getInput(0));

  for(int i = 0; i < 50; ++i) {
    soundGen->process();
  }
  soundGen->flush();
  render->flush();
}

secondclasstest("render: sound generator, evil samples") {
  auto clock = make_shared<SystemClock>(1.0);
  auto render = Modules::loadModule("SDLAudio", &NullHost, clock.get());

  PcmFormat fmt{};
  fmt.sampleFormat = S16;
  fmt.sampleRate = 44100;
  fmt.numPlanes = 1;

  auto sample = make_shared<DataPcm>(100, fmt);
  sample->set(PresentationTime{299454611464360LL});
  render->getInput(0)->push(sample);
  render->process();

  // wait for crash
  std::this_thread::sleep_for(1ms);
}

secondclasstest("render: A/V sync, one thread") {
  auto videoGen = createModule<In::VideoGenerator>(&NullHost);
  auto videoRender = Modules::loadModule("SDLVideo", &NullHost, nullptr);
  ConnectOutputToInput(videoGen->getOutput(0), videoRender->getInput(0));

  auto soundGen = createModule<In::SoundGenerator>(&NullHost);
  auto soundRender = Modules::loadModule("SDLAudio", &NullHost, nullptr);
  ConnectOutputToInput(soundGen->getOutput(0), soundRender->getInput(0));

  for(int i = 0; i < 25 * 5; ++i) {
    videoGen->process();
    soundGen->process();
  }
}

auto createYuvPic(Resolution res) {
  auto r = make_shared<DataPicture>(res, PixelFormat::I420);
  DataPicture::setup(r.get(), res, res, PixelFormat::I420);
  return r;
}

secondclasstest("render: dynamic resolution") {
  auto videoRender = Modules::loadModule("SDLVideo", &NullHost, nullptr);

  auto pic1 = createYuvPic(Resolution(128, 64));
  pic1->set(PresentationTime{1000});
  videoRender->getInput(0)->push(pic1);
  videoRender->process();

  auto pic2 = createYuvPic(Resolution(64, 256));
  pic2->set(PresentationTime{2000});
  videoRender->getInput(0)->push(pic2);
  videoRender->process();
}

}
