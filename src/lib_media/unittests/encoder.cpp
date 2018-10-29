#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/metadata_file.hpp"
#include "lib_media/common/libav.hpp"
#include "lib_media/common/pcm.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/encode/libav_encode.hpp"
#include "lib_media/mux/mux_mp4_config.hpp"
#include "lib_utils/tools.hpp"
#include <iostream> // cerr
#include <vector>

using namespace Tests;
using namespace Modules;
using namespace std;

auto const VIDEO_RESOLUTION = Resolution(320, 180);

unittest("encoder: video simple") {
	auto picture = make_shared<PictureYUV420P>(VIDEO_RESOLUTION);

	int numEncodedFrames = 0;
	auto onFrame = [&](Data /*data*/) {
		numEncodedFrames++;
	};

	EncoderConfig cfg { EncoderConfig::Video };
	auto encode = loadModule("Encoder", &NullHost, &cfg);
	ConnectOutput(encode.get(), onFrame);
	for (int i = 0; i < 37; ++i) {
		picture->setMediaTime(i); // avoid warning about non-monotonic pts
		encode->getInput(0)->push(picture);
		encode->process();
	}
	encode->flush();

	ASSERT_EQUALS(37, numEncodedFrames);
}

static
shared_ptr<DataBase> createPcm(int samples) {
	PcmFormat fmt;
	const auto inFrameSizeInBytes = (size_t)(samples * fmt.getBytesPerSample() / fmt.numPlanes);
	auto pcm = make_shared<DataPcm>(0);
	pcm->setFormat(fmt);
	std::vector<uint8_t> input(inFrameSizeInBytes);
	auto inputRaw = input.data();
	for (uint8_t i = 0; i < fmt.numPlanes; ++i)
		pcm->setPlane(i, inputRaw, inFrameSizeInBytes);
	return pcm;
}

unittest("encoder: audio timestamp passthrough (modulo priming)") {

	vector<int64_t> times;
	auto onFrame = [&](Data data) {
		times.push_back(data->getMediaTime());
	};

	vector<int64_t> inputTimes = { 10000, 20000, 30000, 777000 };

	EncoderConfig cfg { EncoderConfig::Audio };
	auto encode = loadModule("Encoder", &NullHost, &cfg);
	ConnectOutput(encode.get(), onFrame);

	for(auto time : inputTimes) {
		auto pcm = createPcm(1024);
		pcm->setMediaTime(time);
		encode->getInput(0)->push(pcm);
		encode->process();
	}
	encode->flush();

	vector<int64_t> expected = { 10000 - 1024, 10000, 20000, 30000, 777000 };
	ASSERT_EQUALS(expected, times);
}

unittest("encoder: video timestamp passthrough") {
	vector<int64_t> times;
	auto onFrame = [&](Data data) {
		times.push_back(data->getMediaTime());
	};

	vector<int64_t> inputTimes = {0, 1, 2, 3, 4, 20, 21, 22, 10, 11, 12};

	EncoderConfig cfg { EncoderConfig::Video };
	auto encode = loadModule("Encoder", &NullHost, &cfg);
	ConnectOutput(encode.get(), onFrame);
	for(auto time : inputTimes) {
		auto picture = make_shared<PictureYUV420P>(VIDEO_RESOLUTION);
		picture->setMediaTime(time);
		encode->getInput(0)->push(picture);
		encode->process();
	}
	encode->flush();

	vector<int64_t> expected = {0, 1, 2, 3, 4, 20, 21, 22, 10, 11, 12};
	ASSERT_EQUALS(expected, times);
}

void RAPTest(const Fraction fps, const vector<uint64_t> &times, const vector<bool> &RAPs) {
	EncoderConfig p { EncoderConfig::Video };
	p.frameRate = fps;
	p.GOPSize = fps;
	auto picture = make_shared<PictureYUV420P>(VIDEO_RESOLUTION);
	auto encode = loadModule("Encoder", &NullHost, &p);
	size_t i = 0;
	auto onFrame = [&](Data data) {
		if (i < RAPs.size()) {
			auto pkt = safe_cast<const DataAVPacket>(data);
			ASSERT(pkt->isRap() == RAPs[i]);
		}
		i++;
	};
	ConnectOutput(encode.get(), onFrame);
	for (size_t i = 0; i < times.size(); ++i) {
		picture->setMediaTime(times[i]);
		encode->getInput(0)->push(picture);
		encode->process();
	}
	encode->flush();
	ASSERT(i == RAPs.size());
}

unittest("encoder: RAP placement (25/1 fps)") {
	const vector<uint64_t> times = { 0, IClock::Rate / 2, IClock::Rate, IClock::Rate * 3 / 2, IClock::Rate * 2 };
	const vector<bool> RAPs = { true, false, true, false, true };
	RAPTest(Fraction(25, 1), times, RAPs);
}

unittest("encoder: RAP placement (30000/1001 fps)") {
	const vector<uint64_t> times = { 0, IClock::Rate/2, IClock::Rate, IClock::Rate*3/2, IClock::Rate*2 };
	const vector<bool> RAPs = { true, false, true, false, true };
	RAPTest(Fraction(30000, 1001), times, RAPs);
}

unittest("encoder: RAP placement (noisy)") {
	const auto &ms = std::bind(timescaleToClock<uint64_t>, std::placeholders::_1, 1000);
	const vector<uint64_t> times = { 0, ms(330), ms(660), ms(990), ms(1330), ms(1660) };
	const vector<bool> RAPs = { true, false, false, true, false, false };
	RAPTest(Fraction(3, 1), times, RAPs);
}

unittest("encoder: RAP placement (incorrect timings)") {
	const vector<uint64_t> times = { 0, 0, IClock::Rate };
	const vector<bool> RAPs = { true, false, true };
	RAPTest(Fraction(25, 1), times, RAPs);
}

extern "C" {
#include <libavcodec/avcodec.h> // AVCodecContext
}

unittest("GPAC mp4 mux: don't create empty fragments") {
	struct Recorder : ModuleS {
		Recorder() {
			addInput(this);
		}
		void process(Data data) {
			auto meta = safe_cast<const MetadataFile>(data->getMetadata());
			durations.push_back(meta->durationIn180k);
		}
		vector<int64_t> durations;
	};

	auto cfg = Mp4MuxConfig{"", 1000, FragmentedSegment, OneFragmentPerRAP, Browsers | SegmentAtAny};
	auto mux = loadModule("GPACMuxMP4", &NullHost, &cfg);
	auto recorder = create<Recorder>();
	ConnectOutputToInput(mux->getOutput(0), recorder->getInput(0));

	auto createH264AccessUnit = []() {
		static const uint8_t h264_gray_frame[] = {
			0x00, 0x00, 0x00, 0x01,
			0x67, 0x4d, 0x40, 0x0a, 0xe8, 0x8f, 0x42, 0x00,
			0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00,
			0x64, 0x1e, 0x24, 0x4a, 0x24,
			0x00, 0x00, 0x00, 0x01, 0x68, 0xeb, 0xc3, 0xcb,
			0x20, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,
			0xaf, 0xfd, 0x0f, 0xdf,
		};

		auto ctx = make_shared<AVCodecContext>();
		ctx->time_base = {1, 1};
		ctx->codec_id = AV_CODEC_ID_H264;
		auto accessUnit = make_shared<DataAVPacket>(sizeof h264_gray_frame);
		static const auto meta = make_shared<MetadataPktLibavVideo>(ctx);
		accessUnit->setMetadata(meta);
		memcpy(accessUnit->data().ptr, h264_gray_frame, sizeof h264_gray_frame);
		return accessUnit;
	};

	auto const times = vector<int64_t>({
		(1 * IClock::Rate),
		(0 * IClock::Rate),
		(3 * IClock::Rate),
		(7 * IClock::Rate) / 2,
		(4 * IClock::Rate),
	});
	for(auto time : times) {
		auto picture = createH264AccessUnit();
		picture->setMediaTime(time);
		mux->getInput(0)->push(picture);
		mux->process();
	}
	mux->flush();

	auto const expected = vector<int64_t>({
		(0 * IClock::Rate),
		(1 * IClock::Rate),
		(1 * IClock::Rate),
		(1 * IClock::Rate),
		(1 * IClock::Rate),
		(1 * IClock::Rate),
		(0 * IClock::Rate),
	});
	ASSERT_EQUALS(expected, recorder->durations);
}

//TODO: add a more complex test for each module!
