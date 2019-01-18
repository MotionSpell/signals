#pragma once

#include "lib_modules/utils/helper.hpp"
#include "../common/picture.hpp"

namespace Modules {
namespace In {

class VideoGenerator : public Module {
	public:
		struct Config {
			int maxFrames = 0;
			int frameRate = 25;
			Resolution res = Resolution(320, 180);
		};

		VideoGenerator(KHost* host, int maxFrames = 0, int frameRate = 25);

		void process() override;

	private:
		KHost* const m_host;
		Config const config;
		uint64_t m_numFrames = 0;
		OutputPicture *output;
};

}
}
