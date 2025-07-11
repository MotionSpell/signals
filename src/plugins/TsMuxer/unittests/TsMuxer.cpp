#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/picture.hpp" // DataPicture
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/metadata.hpp" // MetadataPkt
#include "lib_media/transform/audio_convert.hpp"
#include "lib_media/in/file.hpp"
#include "lib_media/out/null.hpp"
#include "lib_utils/tools.hpp"
#include <algorithm> //std::min

#include "plugins/TsMuxer/mpegts_muxer.hpp"

using namespace Tests;
using namespace Modules;

namespace {

template<size_t numBytes>
std::shared_ptr<DataBase> createPacket(uint8_t const (&bytes)[numBytes]) {
	auto pkt = make_shared<DataRaw>(numBytes);
	memcpy(pkt->buffer->data().ptr, bytes, numBytes);
	pkt->set(CueFlags {});
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
		auto meta = make_shared<MetadataPktAudio>();
		meta->codec = "mp3";
		meta->numChannels = 2;
		meta->sampleRate = 48000;
		meta->frameSize = 0;
		meta->bitrate = 128*1000;
		r->setMetadata(meta);
	}

	return r;
}

std::shared_ptr<DataBase> getTestAacFrame() {
	static const uint8_t sine_frame[] = {
		0xff, 0xf1, 0x50, 0x40, 0x1c, 0x3f, 0xfc, 0x01, 0x22, 0x98, 0xda, 0xd8,
		0x3d, 0xd6, 0x93, 0x80, 0x76, 0xdb, 0x22, 0x13, 0x6a, 0x38, 0x46, 0x1c,
		0x9c, 0x5e, 0xae, 0x85, 0xf1, 0xab, 0xd5, 0xff, 0x4d, 0x7b, 0x0f, 0x3e,
		0x6d, 0x4f, 0xfc, 0x5f, 0xc3, 0xcf, 0x1f, 0x16, 0x56, 0x09, 0x3c, 0x64,
		0xab, 0x22, 0xab, 0x38, 0x39, 0x56, 0xe8, 0xc8, 0x58, 0x3d, 0x70, 0x72,
		0x31, 0xdb, 0xf0, 0xd8, 0x1e, 0x03, 0x1c, 0xad, 0x45, 0x11, 0xe6, 0xe5,
		0x50, 0x6b, 0xba, 0x24, 0x1b, 0x23, 0x80, 0xd9, 0x04, 0x83, 0x03, 0x64,
		0xe1, 0x27, 0x01, 0x8d, 0xbb, 0x84, 0xcc, 0x0c, 0x0c, 0xee, 0x12, 0x70,
		0x18, 0x19, 0x97, 0x77, 0x09, 0x69, 0xe5, 0x15, 0x12, 0x0c, 0x0b, 0xd7,
		0x84, 0xc1, 0xd4, 0x4a, 0x26, 0x28, 0x14, 0x7b, 0xd7, 0x26, 0xb6, 0xae,
		0x66, 0x16, 0xea, 0x94, 0x6a, 0xda, 0x89, 0x30, 0xd7, 0x4e, 0xa9, 0xb1,
		0xad, 0xda, 0x46, 0x67, 0x73, 0x23, 0x76, 0x14, 0x67, 0x67, 0x76, 0x76,
		0x76, 0x76, 0x67, 0x67, 0xad, 0x9d, 0xa4, 0x9a, 0x49, 0xb1, 0xc3, 0x1a,
		0xdd, 0x9d, 0x9d, 0xa4, 0x9a, 0x49, 0x9d, 0xb1, 0xc2, 0x9a, 0xd9, 0xc1,
		0x81, 0x81, 0x81, 0x81, 0x8d, 0x84, 0x84, 0x84, 0xde, 0xc7, 0x34, 0x52,
		0xca, 0x12, 0x56, 0xe8, 0x04, 0xad, 0x6b, 0x74, 0x9e, 0x05, 0x80, 0xde,
		0xef, 0x1d, 0xfb, 0xcf, 0xbd, 0x18, 0x98, 0xd1, 0x4a, 0x27, 0x5d, 0x6b,
		0x6d, 0x36, 0xdb, 0x16, 0x74, 0xd9, 0xd3, 0x63, 0x32, 0xd1, 0x3a, 0x27,
		0x4c, 0xe8, 0x90, 0x94, 0xa5, 0x52, 0xe0, 0xd3, 0x5c
	};

	auto r = createPacket(sine_frame);

	{
		auto meta = make_shared<MetadataPktAudio>();
		meta->codec = "aac_adts";
		meta->numChannels = 2;
		meta->sampleRate = 48000;
		meta->frameSize = 0;
		meta->bitrate = 128*1000;
		r->setMetadata(meta);
	}

	return r;
}

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
		auto meta = make_shared<MetadataPktVideo>();
		meta->codec = "h264_annexb";
		meta->resolution = { 128, 128 };
		meta->bitrate = 300*1000;
		r->setMetadata(meta);
	}

	return r;
}
}

unittest("TsMuxer: audio simple: mp3") {

	struct FrameCounter : ModuleS {
		void processOne(Data pkt) override {
			totalBytes += pkt->data().len;
		}
		int totalBytes = 0;
	};

	TsMuxerConfig cfg;
	cfg.muxRate = 1000 * 1000;
	auto mux = loadModule("TsMuxer", &NullHost, &cfg);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(mux->getOutput(0), rec->getInput(0));

	mux->getInput(0)->connect();

	for(int i=0; i < 30; ++i) {
		auto frame = getTestMp3Frame();
		int64_t pts = i*(IClock::Rate/20);
		frame->set(PresentationTime{pts});
		frame->set<DecodingTime>({pts});
		mux->getInput(0)->push(frame);
	}
	mux->flush();

	ASSERT_EQUALS(100, std::min(100, rec->totalBytes));
}

unittest("TsMuxer: audio simple: aac") {

	struct FrameCounter : ModuleS {
		void processOne(Data pkt) override {
			totalBytes += pkt->data().len;
		}
		int totalBytes = 0;
	};

	TsMuxerConfig cfg;
	cfg.muxRate = 1000 * 1000;
	auto mux = loadModule("TsMuxer", &NullHost, &cfg);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(mux->getOutput(0), rec->getInput(0));

	mux->getInput(0)->connect();

	for(int i=0; i < 30; ++i) {
		auto frame = getTestAacFrame();
		int64_t pts = i*(IClock::Rate/20);
		frame->set(PresentationTime{pts});
		frame->set<DecodingTime>({pts});
		mux->getInput(0)->push(frame);
	}
	mux->flush();

	ASSERT_EQUALS(100, std::min(100, rec->totalBytes));
}

unittest("TsMuxer: audio + video") {

	struct FrameCounter : ModuleS {
		void processOne(Data pkt) override {
			totalBytes += pkt->data().len;
		}
		int totalBytes = 0;
	};

	TsMuxerConfig cfg;
	cfg.muxRate = 1000 * 1000;
	auto mux = loadModule("TsMuxer", &NullHost, &cfg);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(mux->getOutput(0), rec->getInput(0));

	mux->getInput(0)->connect();
	mux->getInput(1)->connect();

	for(int i=0; i < 30; ++i) {
		int64_t pts = i*(IClock::Rate/20);

		{
			auto frame = getTestH264Frame();
			frame->set(PresentationTime{pts});
			frame->set<DecodingTime>({pts});
			mux->getInput(0)->push(frame);
		}

		{
			auto frame = getTestMp3Frame();
			frame->set(PresentationTime{pts});
			frame->set<DecodingTime>({pts});
			mux->getInput(1)->push(frame);
		}
	}
	mux->flush();

	ASSERT_EQUALS(100, std::min(100, rec->totalBytes));
}

unittest("TsMuxer: flush without feeding") {
	TsMuxerConfig cfg;
	cfg.muxRate = 1000 * 1000;
	auto mux = loadModule("TsMuxer", &NullHost, &cfg);

	int picCount = 0;
	auto onPic = [&](Data) {
		++picCount;
	};

	ConnectOutput(mux->getOutput(0), onPic);
	mux->flush();
	ASSERT_EQUALS(0, picCount);
}

