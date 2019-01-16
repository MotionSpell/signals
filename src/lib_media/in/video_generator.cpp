#include "video_generator.hpp"
#include "../common/metadata.hpp"
#include <string.h> // memset
#include <cassert>

namespace {

span<const char*> getFont();

void renderDigit(uint8_t* dst, size_t stride, int digitValue) {
	assert(digitValue >=0 && digitValue <= 9);

	auto font = getFont();
	auto src = font[digitValue];

	for(int y=0; y < 5; ++y) {
		for(int x=0; x < 5; ++x) {
			if(src[x] == 'X')
				dst[x] = 0xFF;
		}
		src += 5;
		dst += stride;
	}
}

void renderNumber(uint8_t* dst, size_t stride, int value) {
	auto const margin = 3;
	auto const hstep = 6;
	dst += margin;
	dst += stride * margin;

	renderDigit(dst, stride, (value/100)%10);
	dst += hstep;

	renderDigit(dst, stride, (value/10)%10);
	dst += hstep;

	renderDigit(dst, stride, (value/1)%10);
	dst += hstep;
}

}

namespace Modules {
namespace In {

VideoGenerator::VideoGenerator(KHost* host, int maxFrames_, int frameRate)
	:  m_host(host), maxFrames(maxFrames_), m_frameRate(frameRate) {
	output = addOutput<OutputPicture>();
	output->setMetadata(make_shared<MetadataRawVideo>());
	m_host->activate(true);
}

void VideoGenerator::process() {

	if(maxFrames && m_numFrames >= (uint64_t)maxFrames) {
		m_host->activate(false);
		return;
	}

	auto const dim = Resolution(320, 180);
	auto pic = DataPicture::create(output, dim, PixelFormat::I420);

	// generate video
	auto const p = pic->data().ptr;
	auto const flash = (m_numFrames % m_frameRate) == 0;
	auto const val = flash ? 0xCC : 0x80;
	memset(p, val, pic->getSize());

	if(dim.width > 32 && dim.height > 32)
		renderNumber(pic->getPlane(0), pic->getStride(0), m_numFrames);

	auto const framePeriodIn180k = IClock::Rate / m_frameRate;
	assert(IClock::Rate % m_frameRate == 0);
	pic->setMediaTime(m_numFrames * framePeriodIn180k);

	output->post(pic);

	++m_numFrames;
}

}
}

namespace {
span<const char*> getFont() {
	static const char* font[] = {
		".XXX."
		"X...X"
		"X...X"
		"X...X"
		".XXX.",

		"....X"
		"...XX"
		"..X.X"
		"....X"
		"....X",

		".XXX."
		"X...X"
		"..XX."
		".X..."
		"XXXXX",

		".XXX."
		"X...X"
		"..XX."
		"X...X"
		".XXX.",

		"X...X"
		"X...X"
		".XXXX"
		"....X"
		"....X",

		"XXXXX"
		"X...."
		"XXXX."
		"....X"
		"XXXX.",

		".XXX."
		"X...."
		"XXXX."
		"X...X"
		".XXX.",

		"XXXXX"
		"....X"
		"....X"
		"....X"
		"....X",

		".XXX."
		"X...X"
		".XXX."
		"X...X"
		".XXX.",

		".XXX."
		"X...X"
		".XXXX"
		"....X"
		".XXX.",
	};

	return font;
}
}
