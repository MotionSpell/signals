#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/picture.hpp"

using namespace Tests;
using namespace Modules;
using namespace std;

static auto createYuvPic(Resolution res) {
	auto r = make_shared<DataPicture>(res, PixelFormat::I420);
	r->set(PresentationTime{0});
	return r;
}

static auto createNv12Pic(Resolution res) {
	auto r = make_shared<DataPicture>(res, PixelFormat::NV12);
	r->set(PresentationTime{0});
	return r;
}

unittest("video converter: pass-through") {
	auto const res = Resolution(16, 32);
	auto const format = PictureFormat(res, PixelFormat::I420);
	int numFrames = 0;

	auto onFrame = [&](Data data) {
		auto pic = safe_cast<const DataPicture>(data);
		ASSERT(pic->getFormat() == format);
		numFrames++;
	};

	{
		auto convert = loadModule("VideoConvert", &NullHost, &format);
		ConnectOutput(convert->getOutput(0), onFrame);

		auto pic = createYuvPic(res);
		convert->getInput(0)->push(pic);
		convert->process();
	}

	ASSERT_EQUALS(1, numFrames);
}

unittest("video converter: different sizes") {
	auto const srcRes = Resolution(16, 32);
	auto const dstRes = Resolution(24, 8);
	auto const format = PictureFormat(dstRes, PixelFormat::I420);
	int numFrames = 0;

	auto onFrame = [&](Data data) {
		auto pic = safe_cast<const DataPicture>(data);
		ASSERT(pic->getFormat() == format);
		numFrames++;
	};

	{
		auto convert = loadModule("VideoConvert", &NullHost, &format);
		ConnectOutput(convert->getOutput(0), onFrame);

		auto pic = createYuvPic(srcRes);
		convert->getInput(0)->push(pic);
		convert->process();
	}

	ASSERT_EQUALS(1, numFrames);
}

unittest("video converter: format conversion (NV12 to I420)") {
	auto const res = Resolution(128, 20);
	auto const format = PictureFormat(res, PixelFormat::I420);
	int numFrames = 0;

	auto onFrame = [&](Data data) {
		auto pic = safe_cast<const DataPicture>(data);
		ASSERT(pic->getFormat() == format);
		numFrames++;
	};

	{
		auto convert = loadModule("VideoConvert", &NullHost, &format);
		ConnectOutput(convert->getOutput(0), onFrame);

		auto pic = createNv12Pic(res);
		convert->getInput(0)->push(pic);
		convert->process();
	}

	ASSERT_EQUALS(1, numFrames);
}

