#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/mux/libav_mux.hpp"
#include "lib_media/mux/mux_mp4_config.hpp"
#include "lib_utils/tools.hpp"
#include "modules_common.hpp"
#include <cassert>

using namespace std;
using namespace Tests;
using namespace Modules;

namespace {

std::ostream& operator<<(std::ostream& o, Meta const& meta) {
	o << "filename='" << meta.filename << "'" << std::endl;
	o << "mimeType='" << meta.mimeType << "'" << std::endl;
	o << "codecName='" << meta.codecName << "'" << std::endl;
	o << "lang='" << meta.lang << "'" << std::endl;
	o << "durationIn180k=" << meta.durationIn180k << std::endl;
	o << "filesize=" << meta.filesize << std::endl;
	o << "latencyIn180k=" << meta.latencyIn180k << std::endl;
	o << "startsWithRAP=" << meta.startsWithRAP << std::endl;
	o << "eos=" << meta.eos << std::endl;
	return o;
}

}

unittest("remux test: GPAC mp4 mux") {
	DemuxConfig cfg;
	cfg.url = "data/beepbop.mp4";
	auto demux = loadModule("LibavDemux", &NullHost, &cfg);
	Mp4MuxConfig muxCfg {};
	auto mux = loadModule("GPACMuxMP4", &NullHost, &muxCfg);

	ConnectModules(demux.get(), 0, mux.get(), 0); //FIXME: reimplement with multiple inputs
	demux->process();
}

unittest("remux test: libav mp4 mux") {
	DemuxConfig cfg;
	cfg.url = "data/beepbop.mp4";
	auto demux = loadModule("LibavDemux", &NullHost, &cfg);
	std::shared_ptr<IModule> avcc2annexB;
	auto muxConfig = MuxConfig{"out/output_libav", "mp4", ""};
	auto mux = loadModule("LibavMux", &NullHost, &muxConfig);
	ASSERT(demux->getNumOutputs() > 1);
	for (int i = 0; i < demux->getNumOutputs(); ++i) {
		//declare statically metadata to avoid missing data at start
		auto data = make_shared<DataRaw>(0);
		data->setMetadata(demux->getOutput(i)->getMetadata());
		mux->getInput(i)->push(data);

		if (demux->getOutput(i)->getMetadata()->isVideo()) {
			assert(!avcc2annexB);
			avcc2annexB = loadModule("AVCC2AnnexBConverter", &NullHost, nullptr);
			ConnectModules(demux.get(), i, avcc2annexB.get(), 0);
			ConnectModules(avcc2annexB.get(), 0, mux.get(), i);
		} else {
			ConnectModules(demux.get(), i, mux.get(), i);
		}
	}

	demux->process();
}

unittest("mux test: GPAC mp4 with generic descriptor") {
	auto pkt = make_shared<DataRaw>(1);
	pkt->setMetadata(make_shared<MetadataPktVideo>());
	pkt->set(CueFlags{});
	pkt->set(DecodingTime{});
	pkt->set(PresentationTime{});

	const char * my4CC = "toto";
	bool received = false;
	auto onSample = [&](Data pkt) {
		received = true;
		ASSERT(std::string((char*)pkt->data().ptr, pkt->data().len).find(my4CC) != string::npos);
	};

	Mp4MuxConfig muxCfg {};
	muxCfg.fragmentPolicy = OneFragmentPerFrame;
	muxCfg.MP4_4CC = (my4CC[0]<<24)|(my4CC[1]<<16)|(my4CC[2]<<8)|my4CC[3];
	auto mux = loadModule("GPACMuxMP4", &NullHost, &muxCfg);
	ConnectOutput(mux->getOutput(0), onSample);

	mux->getInput(0)->push(pkt);
	mux->flush();
	ASSERT(received);
}

unittest("mux GPAC mp4 failure tests") {
	const uint64_t segmentDurationInMs = 2000;

	{
		auto cfg = Mp4MuxConfig{"out/output_video_gpac_00", segmentDurationInMs, NoSegment, NoFragment};
		ASSERT_THROWN(loadModule("GPACMuxMP4", &NullHost, &cfg));
	}
	{
		auto cfg = Mp4MuxConfig{"out/output_video_gpac_02", 0, NoSegment, OneFragmentPerSegment};
		ASSERT_THROWN(loadModule("GPACMuxMP4", &NullHost, &cfg));
	}
	{
		auto cfg = Mp4MuxConfig{"", 0, NoSegment, NoFragment, FlushFragMemory};
		ASSERT_THROWN(loadModule("GPACMuxMP4", &NullHost, &cfg));
	}
	{
		auto cfg = Mp4MuxConfig{"out/output_video_gpac_10", 0, IndependentSegment, NoFragment, SegNumStartsAtZero};
		ASSERT_THROWN(loadModule("GPACMuxMP4", &NullHost, &cfg));
	}
	{
		auto cfg = Mp4MuxConfig{"", segmentDurationInMs, IndependentSegment, NoFragment, SegNumStartsAtZero | FlushFragMemory};
		ASSERT_THROWN(loadModule("GPACMuxMP4", &NullHost, &cfg););
	}
	{
		auto cfg = Mp4MuxConfig{"out/output_video_gpac_20", segmentDurationInMs, FragmentedSegment, NoFragment, SegNumStartsAtZero};
		ASSERT_THROWN(loadModule("GPACMuxMP4", &NullHost, &cfg););
	}
}

static
IOutput* getFirstAudioPin(IModule* demux) {
	for (int i = 0; i < demux->getNumOutputs(); ++i) {
		auto pin = demux->getOutput(i);
		auto metadata = pin->getMetadata();
		if (metadata->isAudio())
			return pin;
	}
	assert(0);
	return nullptr;
}

static
std::vector<Meta> runMux(std::shared_ptr<IModule> m) {
	DemuxConfig cfg;
	cfg.url = "data/beepbop.mp4";
	auto demux = loadModule("LibavDemux", &NullHost, &cfg);

	auto const audioPin = getFirstAudioPin(demux.get());

	ConnectOutputToInput(audioPin, m->getInput(0));
	auto listener = createModule<Listener>();
	ConnectModules(m.get(), 0, listener.get(), 0);

	for(int i=0; i < 1000; ++i)
		demux->process();
	m->flush();

	return listener->results;
}

unittest("mux GPAC mp4: no segment, no fragment") {
	std::vector<Meta> ref = { { "", "audio/mp4", "mp4a.40.2", "", 0, 10437, 0, 1, 1 } };
	auto cfg = Mp4MuxConfig{"", 0, NoSegment, NoFragment};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

unittest("mux GPAC mp4: no segment, one fragment per RAP") {
	std::vector<Meta> ref = { { "", "audio/mp4", "mp4a.40.2", "", 0, 29869, 0, 1, 1 } };
	auto cfg = Mp4MuxConfig{"", 0, NoSegment, OneFragmentPerRAP};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

unittest("mux GPAC mp4: no segment, one fragment per frame") {
	std::vector<Meta> ref = { { "", "audio/mp4", "mp4a.40.2", "", 0, 29869, 4180, 1, 1 } };
	auto cfg = Mp4MuxConfig{"", 0, NoSegment, OneFragmentPerFrame};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

unittest("mux GPAC mp4: independent segment, no fragments") {
	std::vector<Meta> ref = {
		{ "", "audio/mp4", "mp4a.40.2", "", 363629, 5226, 360000, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 359445, 5336, 359445, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 175543, 3022, 175543, 1, 1 },
	};

	const uint64_t segmentDurationInMs = 2000;
	auto cfg = Mp4MuxConfig{"", segmentDurationInMs, IndependentSegment, NoFragment, SegNumStartsAtZero};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

unittest("mux GPAC mp4: fragmented segments, one fragment per segment") {
	std::vector<Meta> ref = {
		{ "", "audio/mp4", "mp4a.40.2", "", 0, 0, 0, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 363629, 4957, 360000, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 359445, 5047, 359445, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 175543, 2597, 175543, 1, 1 },
	};

	const uint64_t segmentDurationInMs = 2000;
	auto cfg = Mp4MuxConfig{"", segmentDurationInMs, FragmentedSegment, OneFragmentPerSegment, SegNumStartsAtZero};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

unittest("mux GPAC mp4: fragmented segments, one fragment per RAP") {
	std::vector<Meta> ref = {
		{ "", "audio/mp4", "mp4a.40.2", "", 0, 0, 0, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 363629, 15629, 360000, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 359445, 15599, 359445, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 175543, 7685, 175543, 1, 1 },
	};

	const uint64_t segmentDurationInMs = 2000;
	auto cfg = Mp4MuxConfig{"", segmentDurationInMs, FragmentedSegment, OneFragmentPerRAP, SegNumStartsAtZero};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

unittest("mux GPAC mp4: fragmented segments, one fragment per frame") {
	std::vector<Meta> ref = {
		{ "", "audio/mp4", "mp4a.40.2", "", 0, 0, 4180, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 363629, 15629, 4180, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 359445, 15599, 4180, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 175543, 7685, 4180, 1, 1 },
	};

	const uint64_t segmentDurationInMs = 2000;
	auto cfg = Mp4MuxConfig{"", segmentDurationInMs, FragmentedSegment, OneFragmentPerFrame, SegNumStartsAtZero};
	ASSERT_EQUALS(ref, runMux(loadModule("GPACMuxMP4", &NullHost, &cfg)));
}

// remove this when the below tests are split
std::vector<Meta> operator+(std::vector<Meta> const& a, std::vector<Meta> const& b) {
	std::vector<Meta> r;
	for(auto& val : a)
		r.push_back(val);
	for(auto& val : b)
		r.push_back(val);
	return r;
}

unittest("mux GPAC mp4 combination coverage 1") {
	std::vector<Meta> ref = {
		{ "", "audio/mp4", "mp4a.40.2", "", 0, 10437, 0, 1, 1 },
		{ "output_video_gpac_12-0.mp4", "audio/mp4", "mp4a.40.2", "", 363629, 4838, 360000, 1, 1 },
		{ "output_video_gpac_12-1.mp4", "audio/mp4", "mp4a.40.2", "", 359445, 4936, 359445, 1, 1 },
		{ "output_video_gpac_12-2.mp4", "audio/mp4", "mp4a.40.2", "", 175543, 2838, 175543, 1, 1 },
		{ "output_video_gpac_13-0.mp4", "audio/mp4", "mp4a.40.2", "", 363629, 19298, 360000, 1, 1 },
		{ "output_video_gpac_13-1.mp4", "audio/mp4", "mp4a.40.2", "", 359445, 19232, 359445, 1, 1 },
		{ "output_video_gpac_13-2.mp4", "audio/mp4", "mp4a.40.2", "", 175543, 9734, 175543, 1, 1 },
		{ "output_video_gpac_14-0.mp4", "audio/mp4", "mp4a.40.2", "", 363629, 19298, 4180, 1, 1 },
		{ "output_video_gpac_14-1.mp4", "audio/mp4", "mp4a.40.2", "", 359445, 19232, 4180, 1, 1 },
		{ "output_video_gpac_14-2.mp4", "audio/mp4", "mp4a.40.2", "", 175543, 9734, 4180, 1, 1 },
	};

	const uint64_t segmentDurationInMs = 2000;

	auto cfg1 = Mp4MuxConfig{"", 0, NoSegment, NoFragment};
	auto cfg2 = Mp4MuxConfig{"output_video_gpac_12", segmentDurationInMs, IndependentSegment, OneFragmentPerSegment, SegNumStartsAtZero | Browsers};
	auto cfg3 = Mp4MuxConfig{"output_video_gpac_13", segmentDurationInMs, IndependentSegment, OneFragmentPerRAP, SegNumStartsAtZero | Browsers};
	auto cfg4 = Mp4MuxConfig{"output_video_gpac_14", segmentDurationInMs, IndependentSegment, OneFragmentPerFrame, SegNumStartsAtZero | Browsers};

	auto res1 = runMux(loadModule("GPACMuxMP4", &NullHost, &cfg1));
	auto res2 = runMux(loadModule("GPACMuxMP4", &NullHost, &cfg2));
	auto res3 = runMux(loadModule("GPACMuxMP4", &NullHost, &cfg3));
	auto res4 = runMux(loadModule("GPACMuxMP4", &NullHost, &cfg4));
	ASSERT_EQUALS(ref, res1 + res2 + res3 + res4);
}

unittest("mux GPAC mp4 combination coverage 2") {
	std::vector<Meta> ref = {
		{ "", "audio/mp4", "mp4a.40.2", "", 363629, 5226, 360000, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 359445, 5336, 359445, 1, 1 },
		{ "", "audio/mp4", "mp4a.40.2", "", 175543, 3022, 175543, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 0, 729, 0, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 363629, 4957, 360000, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 359445, 5047, 359445, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 175543, 2597, 175543, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 0, 729, 0, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 363629, 4949, 360000, 1, 0 },
		{  "", "audio/mp4", "mp4a.40.2", "", 0, 8, 0, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 359445, 5039, 359445, 1, 0 },
		{  "", "audio/mp4", "mp4a.40.2", "", 0, 8, 0, 1, 1 },
		{  "", "audio/mp4", "mp4a.40.2", "", 175543, 2589, 175543, 1, 0 },
		{  "", "audio/mp4", "mp4a.40.2", "", 0, 8, 0, 1, 1 },
	};

	const uint64_t segmentDurationInMs = 2000;

	auto cfg1 = Mp4MuxConfig{"", segmentDurationInMs, IndependentSegment, NoFragment, SegNumStartsAtZero};
	auto cfg2 = Mp4MuxConfig{"", segmentDurationInMs, FragmentedSegment, OneFragmentPerSegment, SegNumStartsAtZero};
	auto cfg3 = Mp4MuxConfig{"", segmentDurationInMs, FragmentedSegment, OneFragmentPerSegment, SegNumStartsAtZero | FlushFragMemory};

	ASSERT_EQUALS(ref,
	    runMux(loadModule("GPACMuxMP4", &NullHost, &cfg1))
	    + runMux(loadModule("GPACMuxMP4", &NullHost, &cfg2))
	    + runMux(loadModule("GPACMuxMP4", &NullHost, &cfg3))
	);
}

unittest("remux test: canonical to H.264 Annex B bitstream converter") {
	const uint8_t input[] = {0, 0, 0, 4, 44, 55, 66, 77 };
	auto pkt = make_shared<DataRaw>(sizeof input);
	memcpy(pkt->buffer->data().ptr, input, sizeof input);

	std::vector<uint8_t> actual;
	bool received = false;

	auto onSample = [&](Data pkt) {
		received = true;
		actual.assign(pkt->data().ptr, pkt->data().ptr + pkt->data().len);
	};

	auto avcc2annexB = loadModule("AVCC2AnnexBConverter", &NullHost, nullptr);
	ConnectOutput(avcc2annexB->getOutput(0), onSample);
	avcc2annexB->getInput(0)->push(pkt);
	ASSERT(received);

	auto const expected = std::vector<uint8_t>({0, 0, 0, 1, 44, 55, 66, 77 });
	ASSERT_EQUALS(expected, actual);
}

unittest("GPAC mp4 mux: don't create empty fragments") {
	struct Recorder : ModuleS {
		void processOne(Data data) {
			auto meta = safe_cast<const MetadataFile>(data->getMetadata());
			durations.push_back(meta->durationIn180k);
		}
		vector<int64_t> durations;
	};

	auto cfg = Mp4MuxConfig{"", 1000, FragmentedSegment, OneFragmentPerRAP, Browsers | SegmentAtAny};
	auto mux = loadModule("GPACMuxMP4", &NullHost, &cfg);
	auto recorder = createModule<Recorder>();
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

		static const auto meta = make_shared<MetadataPktVideo>();
		meta->timeScale = {1, 1};
		meta->framerate = {1, 1};
		meta->codec = "h264_annexb";
		auto accessUnit = make_shared<DataRaw>(sizeof h264_gray_frame);
		accessUnit->setMetadata(meta);
		accessUnit->set(CueFlags{});
		memcpy(accessUnit->buffer->data().ptr, h264_gray_frame, sizeof h264_gray_frame);
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
		picture->set(PresentationTime{time});
		picture->set(DecodingTime{time});
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
		(0 * IClock::Rate),
	});
	ASSERT_EQUALS(expected, recorder->durations);
}
