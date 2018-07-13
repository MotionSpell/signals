#include "video_generator.hpp"
#include "../common/metadata.hpp"
#include <string.h> // memset

auto const FRAMERATE = 25;

namespace Modules {
namespace In {

VideoGenerator::VideoGenerator(int maxFrames_) : maxFrames(maxFrames_) {
	output = addOutput<OutputPicture>();
	output->setMetadata(make_shared<MetadataRawVideo>());
}

bool VideoGenerator::work() {

	if(maxFrames && m_numFrames >= (uint64_t)maxFrames)
		return false;

	auto const dim = Resolution(320, 180);
	auto pic = DataPicture::create(output, dim, YUV420P);

	// generate video
	auto const p = pic->data();
	auto const FLASH_PERIOD = FRAMERATE;
	auto const flash = (m_numFrames % FLASH_PERIOD) == 0;
	auto const val = flash ? 0xCC : 0x80;
	memset(p, val, pic->getSize());

	auto const framePeriodIn180k = IClock::Rate / FRAMERATE;
	assert(IClock::Rate % FRAMERATE == 0);
	pic->setMediaTime(m_numFrames * framePeriodIn180k);

	if (m_numFrames % 25 < 2)
		output->emit(pic);

	++m_numFrames;
	return true;
}

}
}
