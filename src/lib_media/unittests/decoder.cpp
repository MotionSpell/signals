#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/picture.hpp" // DataPicture
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/metadata.hpp" // MetadataPkt
#include "lib_media/transform/audio_convert.hpp"
#include "lib_media/encode/libav_encode.hpp"
#include "lib_media/in/file.hpp"
#include "lib_media/out/null.hpp"
#include "lib_utils/tools.hpp"

using namespace Tests;
using namespace Modules;

namespace {

template<size_t numBytes>
std::shared_ptr<DataBase> createPacket(uint8_t const (&bytes)[numBytes]) {
	auto pkt = make_shared<DataRaw>(numBytes);
	memcpy(pkt->buffer->data().ptr, bytes, numBytes);
	return pkt;
}

std::shared_ptr<DataBase> getTestMp3Frame() {
	static const uint8_t mp3_sine_frame[] = {
		0xff, 0xfb, 0x30, 0xc0, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x49, 0x6e, 0x66, 0x6f,
		0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x29,
		0x00, 0x00, 0x19, 0xb6, 0x00, 0x0c, 0x0c, 0x12,
		0x12, 0x18, 0x18, 0x18, 0x1e, 0x1e, 0x24, 0x24,
		0x24, 0x2a, 0x2a, 0x30, 0x30, 0x30, 0x36, 0x36,
		0x3c, 0x3c, 0x43, 0x43, 0x43, 0x49, 0x49, 0x4f,
		0x4f, 0x4f, 0x55, 0x55, 0x5b, 0x5b, 0x5b, 0x61,
		0x61, 0x67, 0x67, 0x67, 0x6d, 0x6d, 0x73, 0x73,
		0x79, 0x79, 0x79, 0x7f, 0x7f, 0x86, 0x86, 0x86,
		0x8c, 0x8c, 0x92, 0x92, 0x92, 0x98, 0x98, 0x9e,
		0x9e, 0xa4, 0xa4, 0xa4, 0xaa, 0xaa, 0xb0, 0xb0,
		0xb0, 0xb6, 0xb6, 0xbc, 0xbc, 0xbc, 0xc3, 0xc3,
		0xc9, 0xc9, 0xc9, 0xcf, 0xcf, 0xd5, 0xd5, 0xdb,
		0xdb, 0xdb, 0xe1, 0xe1, 0xe7, 0xe7, 0xe7, 0xed,
		0xed, 0xf3, 0xf3, 0xf3, 0xf9, 0xf9, 0xff, 0xff,
		0x00, 0x00, 0x00, 0x00
	};

	auto r = createPacket(mp3_sine_frame);

	{
		auto meta = make_shared<MetadataPkt>(AUDIO_PKT);
		meta->codec = "mp3";
		r->setMetadata(meta);
	}

	r->set(CueFlags {});

	return r;
}
}

unittest("decoder: audio simple") {
	struct FrameCounter : ModuleS {
		void processOne(Data) override {
			++frameCount;
		}
		int frameCount = 0;
	};

	auto decode = loadModule("Decoder", &NullHost, (void*)(uintptr_t)AUDIO_PKT);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(decode->getOutput(0), rec->getInput(0));

	for(int i=0; i < 3; ++i) {
		auto frame = getTestMp3Frame();
		decode->getInput(0)->push(frame);
	}
	decode->flush();

	ASSERT_EQUALS(3, rec->frameCount);
}

unittest("decoder: timestamp propagation") {
	struct FrameCounter : ModuleS {
		void processOne(Data data) override {
			mediaTimes.push_back(data->get<PresentationTime>().time);
		}
		std::vector<int64_t> mediaTimes;
	};

	auto decode = loadModule("Decoder", &NullHost, (void*)(uintptr_t)AUDIO_PKT);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(decode->getOutput(0), rec->getInput(0));

	for(int i=0; i < 5; ++i) {
		auto frame = getTestMp3Frame();
		frame->set(PresentationTime {i});
		decode->getInput(0)->push(frame);
	}
	decode->flush();

	auto expected = std::vector<int64_t>({0, 1, 2, 3, 4});
	ASSERT_EQUALS(expected, rec->mediaTimes);
}

namespace {

std::shared_ptr<DataBase> getTestH264Frame() {
	static const uint8_t h264_gray_frame[] = {
		0x00, 0x00, 0x00, 0x01,
		0x67, 0x4d, 0x40, 0x0a, 0xe8, 0x8f, 0x42, 0x00,
		0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00,
		0x64, 0x1e, 0x24, 0x4a, 0x24,
		0x00, 0x00, 0x00, 0x01, 0x68, 0xeb, 0xc3, 0xcb,
		0x20, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,
		0xaf, 0xfd, 0x0f, 0xdf,
	};

	auto r = createPacket(h264_gray_frame);

	{
		auto meta = make_shared<MetadataPkt>(VIDEO_PKT);
		meta->codec = "h264_annexb";
		r->setMetadata(meta);
	}

	r->set(CueFlags {});

	return r;
}
}

unittest("decoder: video simple") {
	auto decode = loadModule("Decoder", &NullHost, (void*)(uintptr_t)VIDEO_PKT);
	auto data = getTestH264Frame();

	std::vector<std::string> actualFrames;

	auto onPic = [&](Data data) {
		auto const pic = safe_cast<const DataPicture>(data);
		auto const format = pic->getFormat();
		auto const firstPixel = *pic->getPlane(0);
		auto const lastPixel = *(pic->getPlane(0) + pic->getStride(0) * (format.res.height-1) + format.res.width - 1);

		char info[256];
		sprintf(info, "'%dx%d %d %.2X %.2X'", format.res.width, format.res.height, (int)format.format, firstPixel, lastPixel);
		actualFrames.push_back(info);
	};

	auto expectedFrames = std::vector<std::string>({
		"'16x16 1 80 80'",
		"'16x16 1 80 80'",
	});

	ConnectOutput(decode->getOutput(0), onPic);
	decode->getInput(0)->push(data);
	decode->getInput(0)->push(data);
	decode->flush();
	ASSERT_EQUALS(expectedFrames, actualFrames);
}

unittest("decoder: destroy without flushing") {
	auto decode = loadModule("Decoder", &NullHost, (void*)(uintptr_t)VIDEO_PKT);

	int picCount = 0;
	auto onPic = [&](Data) {
		++picCount;
	};

	ConnectOutput(decode->getOutput(0), onPic);
	decode->getInput(0)->push(getTestH264Frame());
	ASSERT_EQUALS(0, picCount);
}

unittest("decoder: flush without feeding") {
	auto decode = loadModule("Decoder", &NullHost, (void*)(uintptr_t)VIDEO_PKT);

	int picCount = 0;
	auto onPic = [&](Data) {
		++picCount;
	};

	ConnectOutput(decode->getOutput(0), onPic);
	decode->flush();
	ASSERT_EQUALS(0, picCount);
}

unittest("decoder: audio mp3 manual frame to AAC") {
	EncoderConfig cfg { EncoderConfig::Audio };

	auto decode = loadModule("Decoder", &NullHost, (void*)(uintptr_t)AUDIO_PKT);
	auto encoder = loadModule("Encoder", &NullHost, &cfg);

	ConnectOutputToInput(decode->getOutput(0), encoder->getInput(0));

	auto frame = getTestMp3Frame();

	decode->getInput(0)->push(frame);
}

unittest("decoder: audio mp3 to converter to AAC") {
	EncoderConfig cfg { EncoderConfig::Audio };

	auto decoder = loadModule("Decoder", &NullHost, (void*)(uintptr_t)AUDIO_PKT);
	auto encoder = loadModule("Encoder", &NullHost, &cfg);

	auto const dstFormat = PcmFormat(44100, 2, AudioLayout::Stereo, AudioSampleFormat::F32, AudioStruct::Planar);
	auto converterCfg = AudioConvertConfig { {0}, dstFormat, 1024 };
	auto converter = loadModule("AudioConvert", &NullHost, &converterCfg);

	ConnectOutputToInput(decoder->getOutput(0), converter->getInput(0));
	ConnectOutputToInput(converter->getOutput(0), encoder->getInput(0));

	auto frame = getTestMp3Frame();
	decoder->getInput(0)->push(frame);
	decoder->process();
	decoder->flush();
	converter->flush();
	encoder->flush();
}
