#include "tests.hpp"
#include "modules.hpp"

#include "render/sdl_audio.hpp"
#include "render/sdl_video.hpp"
#include "in/sound_generator.hpp"
#include "in/video_generator.hpp"

using namespace Tests;
using namespace Modules;

namespace {

unittest("A/V sync: one thread") {
	auto videoGen = uptrSafeModule(new In::VideoGenerator);
	auto videoRender = uptrSafeModule(new Render::SDLVideo);
	ConnectPinToModule(videoGen->getPin(0), videoRender);

	//FIXME: avoid SDL audio and video parallel creations
	const int sleepDurInMs = 100;
	const std::chrono::milliseconds dur(sleepDurInMs);
	std::this_thread::sleep_for(dur);

	auto soundGen = uptrSafeModule(new In::SoundGenerator);
	auto soundRender = uptrSafeModule(Render::SDLAudio::create());
	ConnectPinToModule(soundGen->getPin(0), soundRender);

	for(int i=0; i < 25*5; ++i) {
		videoGen->process(nullptr);
		soundGen->process(nullptr);
	}

	videoGen->waitForCompletion();
	videoRender->waitForCompletion();
	soundGen->waitForCompletion();
	soundRender->waitForCompletion();
}

unittest("A/V sync: separate threads") {
#if 0
	auto f = [&]() {
		auto videoGen = uptrSafeModule(new In::VideoGenerator);
		auto videoRender = uptrSafeModule(new Render::SDLVideo);
		ConnectPinToModule(videoGen->getPin(0), videoRender);

		for(int i=0; i < 25*5; ++i) {
			videoGen->process(nullptr);
		}

		videoGen->waitForCompletion();
		videoRender->waitForCompletion();
	};
	auto g = [&]() {
		auto soundGen = uptrSafeModule(new In::SoundGenerator);
		auto soundRender = uptrSafeModule(Render::SDLAudio::create());
		ConnectPinToModule(soundGen->getPin(0), soundRender);
		for(int i=0; i < 25*5; ++i) {
			soundGen->process(nullptr);
		}

		soundGen->waitForCompletion();
		soundRender->waitForCompletion();
	};

	std::thread tf(f);

	//FIXME: avoid SDL audio and video parallel creations
	const int sleepDurInMs = 100;
	const std::chrono::milliseconds dur(sleepDurInMs);
	std::this_thread::sleep_for(dur);

	std::thread tg(g);

	tf.join();
	tg.join();
#endif
}

}
