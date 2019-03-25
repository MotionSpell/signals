#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_utils/tools.hpp"

#include "lib_media/transform/logo_overlay.hpp"

using namespace Tests;
using namespace Modules;

namespace {

void setPixel(DataPicture* pic, int x, int y, int value) {
	pic->getPlane(0)[x+y*pic->getStride(0)] = value;
}

int getPixel(const DataPicture* pic, int x, int y) {
	return pic->getPlane(0)[x+y*pic->getStride(0)];
}

// Must stay binary compatible with DataPicture
struct TestPic : DataPicture {
	TestPic(Resolution r, PixelFormat fmt) : DataPicture(r, fmt) {
		setup(this, r, r, fmt);
	}

	bool operator!=(TestPic const& other) const {
		if(other.getFormat() != getFormat())
			return true;
		auto res = getFormat().res;
		for(int y=0; y < res.height; ++y) {
			for(int x=0; x < res.width; ++x) {
				if(getPixel(this, x, y) != getPixel(&other, x, y))
					return true;
			}
		}
		return false;
	}
};

std::ostream& operator<<(std::ostream& o, TestPic const& pic) {
	auto res = pic.getFormat().res;
	for(int y=0; y < res.height; ++y) {
		for(int x=0; x < res.width; ++x) {
			char buff[8];
			sprintf(buff, "%.2X ", pic.getPlane(0)[pic.getStride(0)*y+x]);
			o << buff;
		}
		o << "\n";
	}
	o << "\n";
	return o;
}

template<typename Lambda>
std::shared_ptr<TestPic> createTestPic(Resolution logoDim, Lambda pixelFormula) {
	auto logo = make_shared<TestPic>(logoDim, PixelFormat::I420);

	for(int y=0; y < logoDim.height; ++y)
		for(int x=0; x < logoDim.width; ++x)
			setPixel(logo.get(), x, y, pixelFormula(x, y));

	return logo;
}
}

unittest("LogoOverlay: simple") {
	struct FrameRecorder : ModuleS {
		void processOne(Data d) override {
			data = d;
		}
		Data data;
	};

	static auto const COLOR_BLANK = 0x11; // picture uniform color
	static auto const COLOR_BORDER = 0xBB; // logo border
	static auto const COLOR_INSIDE = 0xAA; // inside the logo

	auto const logoDim = Resolution(7, 8);
	LogoOverlayConfig cfg {};
	cfg.dim = logoDim;
	cfg.x = 10;
	cfg.y = 20;

	auto grayBorders = [&](int x, int y) {
		return x == 0 || x == logoDim.width-1 || y == 0 || y == logoDim.height-1 ? COLOR_BORDER : COLOR_INSIDE;
	};

	auto logo = createTestPic(logoDim, grayBorders);

	auto overlay = loadModule("LogoOverlay", &NullHost, &cfg);
	auto rec = createModule<FrameRecorder>();
	ConnectOutputToInput(overlay->getOutput(0), rec->getInput(0));

	// run the overlay

	overlay->getInput(1)->push(logo);

	auto pic = createTestPic(Resolution(29, 32), [](int, int) {
		return COLOR_BLANK;
	});
	overlay->getInput(0)->push(pic);

	overlay->flush();

	// check resulting composed picture

	auto expectedPixelFormula = [](int x, int y) {
		if(x < 10 || x > 16 || y < 20 || y > 27)
			return COLOR_BLANK;
		if(x == 10 || x == 16 || y == 20 || y == 27)
			return COLOR_BORDER;
		return COLOR_INSIDE;
	};
	auto expected = createTestPic(Resolution(29, 32), expectedPixelFormula);
	auto actual = (TestPic*)safe_cast<const DataPicture>(rec->data).get();
	ASSERT_EQUALS(*expected, *actual);
}

