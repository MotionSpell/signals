#include "lib_media/common/attributes.hpp"
#include "lib_media/in/sound_generator.hpp"
#include "lib_media/in/video_generator.hpp"
#include "lib_modules/modules.hpp"
#include "lib_utils/string_tools.hpp" // makeVector
#include "tests/tests.hpp"

using namespace Tests;
using namespace Modules;

unittest("video generator") {
  auto videoGen = createModule<In::VideoGenerator>(&NullHost);

  std::vector<int> times;
  auto onFrame = [&](Data data) {
    auto rawData = safe_cast<const DataPicture>(data);
    times.push_back((int)rawData->get<PresentationTime>().time);
  };
  ConnectOutput(videoGen->getOutput(0), onFrame);

  for(int i = 0; i < 4; ++i) {
    videoGen->process();
  }

  ASSERT_EQUALS(makeVector({
                      0 * 7200,
                      1 * 7200,
                      2 * 7200,
                      3 * 7200,
                }),
        times);
}

unittest("video generator: from url") {
  auto videoGen = createModule<In::VideoGenerator>(&NullHost, "videogen://framecount=7&framerate=30");

  int count = 0;
  auto onFrame = [&](Data) { ++count; };
  ConnectOutput(videoGen->getOutput(0), onFrame);

  for(int i = 0; i < 100; ++i)
    videoGen->process();

  ASSERT_EQUALS(7, count);
}
