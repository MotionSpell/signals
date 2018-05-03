#include "tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_media/in/sound_generator.hpp"
#include "lib_media/in/video_generator.hpp"
#include "lib_media/render/sdl_audio.hpp"
#include "lib_media/render/sdl_video.hpp"
#include "lib_utils/tools.hpp"

using namespace Tests;
using namespace Modules;

namespace {

unittest("sound generator") {
	auto soundGen = create<In::SoundGenerator>();
	auto render = create<Render::SDLAudio>();

	ConnectOutputToInput(soundGen->getOutput(0), render->getInput(0));

	for(int i=0; i < 25; ++i) {
		soundGen->process(nullptr);
	}
}

unittest("video generator") {
	auto videoGen = create<In::VideoGenerator>();
	auto render = create<Render::SDLVideo>();

	std::vector<int> times;
	auto onFrame = [&](Data data) {
		auto rawData = safe_cast<const DataPicture>(data);
		times.push_back((int)rawData->getMediaTime());
		render->process(rawData);
	};
	Connect(videoGen->getOutput(0)->getSignal(), onFrame);

	for (int i = 0; i < 50; ++i) {
		videoGen->process(nullptr);
	}

	ASSERT_EQUALS(makeVector(0, 7200, 180000, 187200), times);
}

}
