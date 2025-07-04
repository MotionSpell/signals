#include "lib_utils/system_clock.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/queue.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/attributes.hpp"
#include "SDL2/SDL.h"
#include "render_common.hpp"
#include <thread>
#include <csignal>

using namespace Modules;
using namespace Modules::Render;

namespace {

Uint32 pixelFormat2SDLFormat(const Modules::PixelFormat format) {
	switch (format) {
	case PixelFormat::I420   : return SDL_PIXELFORMAT_IYUV;
	case PixelFormat::YUYV422: return SDL_PIXELFORMAT_YUY2;
	case PixelFormat::NV12   : return SDL_PIXELFORMAT_NV12;
	case PixelFormat::RGB24  : return SDL_PIXELFORMAT_RGB24;
	case PixelFormat::RGBA32 : return SDL_PIXELFORMAT_RGBX8888;
	default: throw std::runtime_error("Pixel format not supported.");
	}
}

Uint32 queueOneUserEvent(Uint32, void*) {
	SDL_Event event {};
	event.type = SDL_USEREVENT;
	SDL_PushEvent(&event);
	return 0;
}

struct SDLVideo : ModuleS {
	SDLVideo(KHost* host, IClock* clock)
		: m_host(host),
		  m_clock(clock ? clock : g_SystemClock.get()),
		  workingThread(&SDLVideo::doRender, this) {
		input->setMetadata(make_shared<MetadataRawVideo>());

		// wait for the working thread to create the window
		m_dataQueue.pop();
	}

	~SDLVideo() {
		m_dataQueue.push(nullptr);
		queueOneUserEvent(0, nullptr);
		workingThread.join();
	}

	void doRender() {

		{
			// save current Ctrl-C handler
			auto savedHandler = std::signal(SIGINT, nullptr);

			if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1)
				throw std::runtime_error(format("Couldn't initialize: %s", SDL_GetError()));

			// restore Ctrl-C handler
			std::signal(SIGINT, savedHandler);
		}

		// display the first frame as fast as possible, to avoid a black screen
		respectTimestamps = false;

		pictureFormat.res = Resolution(320, 180);
		pictureFormat.format = PixelFormat::I420;
		window = SDL_CreateWindow("Signals SDLVideo renderer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pictureFormat.res.width, pictureFormat.res.height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if (!window)
			throw error(format("Couldn't set create window: %s", SDL_GetError()));

		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!renderer) {
			SDL_DestroyWindow(window);
			throw error(format("Couldn't set create renderer: %s", SDL_GetError()));
		}
		m_dataQueue.push(nullptr); //unlock the constructor

		createTexture();

		SDL_EventState(SDL_KEYUP, SDL_IGNORE); //ignore key up events, they don't even get filtered

		while(processEvents()) {
		}

		SDL_DestroyTexture(texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}

	bool processEvents() {
		SDL_Event event;
		SDL_WaitEvent(&event);

		switch (event.type) {
		case SDL_USEREVENT: {
			Data data;
			if(m_dataQueue.tryPop(data)) {
				if(!data)
					return false;
				displayFrame(data);
			}
		}
		break;
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
				SDL_RenderSetViewport(renderer, nullptr);
				displaySize.width = event.window.data1;
				displaySize.height = event.window.data2;
			} else if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
				present();
			}
			break;
		case SDL_QUIT:
			std::raise(SIGINT);
			break;
		}

		return true;
	}

	void displayFrame(Data data) {
		auto pic = safe_cast<const DataPicture>(data);
		if (pic->getFormat() != pictureFormat) {
			pictureFormat = pic->getFormat();
			createTexture();
		}

		if (pictureFormat.format == PixelFormat::I420) {
			SDL_UpdateYUVTexture(texture, nullptr,
			    pic->getPlane(0), (int)pic->getStride(0),
			    pic->getPlane(1), (int)pic->getStride(1),
			    pic->getPlane(2), (int)pic->getStride(2));
		} else {
			SDL_UpdateTexture(texture, nullptr, pic->getPlane(0), (int)pic->getStride(0));
		}

		present();
	}

	void present() {
		SDL_Rect displayrect {};
		displayrect.w = displaySize.width;
		displayrect.h = displaySize.height;
		SDL_RenderCopy(renderer, texture, nullptr, &displayrect);
		SDL_RenderPresent(renderer);

		// now that we have something on the screen, respect the timestamps
		respectTimestamps = true;
	}

	void createTexture() {
		m_host->log(Info, format("%sx%s", pictureFormat.res.width, pictureFormat.res.height).c_str());

		if (texture)
			SDL_DestroyTexture(texture);

		texture = SDL_CreateTexture(renderer, pixelFormat2SDLFormat(pictureFormat.format), SDL_TEXTUREACCESS_STATIC, pictureFormat.res.width, pictureFormat.res.height);
		if (!texture)
			throw error(format("Couldn't set create texture: %s", SDL_GetError()));

		displaySize.width = pictureFormat.res.width;
		displaySize.height = pictureFormat.res.height;
		SDL_SetWindowSize(window, displaySize.width, displaySize.height);
	}

	void processOne(Data data) override {
		m_dataQueue.push(data);

		auto const now = fractionToClock(m_clock->now());
		auto const timestamp = data->get<PresentationTime>().time + PREROLL_DELAY; // assume timestamps start at zero
		auto const delay = respectTimestamps ? std::max<int64_t>(0, timestamp - now) : 0;
		auto const delayInMs = clockToTimescale(delay, 1000);

		SDL_AddTimer((Uint32)delayInMs, &queueOneUserEvent, nullptr);
	}

	KHost* const m_host;
	IClock* const m_clock;

	SDL_Window* window = nullptr;
	SDL_Renderer* renderer = nullptr;
	SDL_Texture* texture = nullptr;
	Resolution displaySize {};
	PictureFormat pictureFormat {};
	bool respectTimestamps = true;

	Queue<Data> m_dataQueue; //FIXME: useless now we have input ports
	std::thread workingThread;
};

IModule* createObject(KHost* host, void* va) {
	auto clock = (IClock*)va;
	enforce(host, "SDLVideo: host can't be NULL");
	return createModule<SDLVideo>(host, clock).release();
}

auto const registered = Factory::registerModule("SDLVideo", &createObject);
}
