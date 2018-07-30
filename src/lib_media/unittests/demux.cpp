#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/demux/gpac_demux_mp4_simple.hpp"
#include "lib_media/demux/gpac_demux_mp4_full.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/in/file.hpp"
#include "lib_media/out/print.hpp"
#include "lib_utils/tools.hpp"
#include <iostream> // std::cout

using namespace Tests;
using namespace Modules;
using namespace std;

namespace {
vector<int64_t> deltas(vector<int64_t> times) {
	vector<int64_t> r;
	for(int i=0; i < (int)times.size()-1; ++i)
		r.push_back(times[i+1] - times[i]);
	return r;
}
}

// at the moment, the demuxer discards the first frame
unittest("LibavDemux: simple: 75 frames") {

	struct MyOutput : ModuleS {
		MyOutput() {
			createInput(this);
		}
		void process(Data) override {
			++frameCount;
		}
		int frameCount = 0;
	};

	auto demux = create<Demux::LibavDemux>(&NullHost, "data/simple.ts");
	auto rec = create<MyOutput>();
	ConnectOutputToInput(demux->getOutput(0), rec->getInput(0));

	demux->process();
	demux->flush();

	ASSERT_EQUALS(75, rec->frameCount);
}

unittest("LibavDemux: rollover") {

	struct MyOutput : ModuleS {
		MyOutput() {
			createInput(this);
		}
		vector<int64_t> times;
		void process(Data data) override {
			times.push_back(data->getMediaTime());
		}
	};

	auto demux = create<Demux::LibavDemux>(&NullHost, "data/rollover.ts");
	auto rec = create<MyOutput>();
	ConnectOutputToInput(demux->getOutput(0), rec->getInput(0));

	demux->process();
	demux->flush();

	vector<int64_t> expected(74, 7200);
	ASSERT_EQUALS(expected, deltas(rec->times));
}

unittest("empty param test: Demux") {
	ScopedLogLevel lev(Quiet);
	ASSERT_THROWN(create<Demux::GPACDemuxMP4Simple>(&NullHost, ""));
}

secondclasstest("demux one track: Demux::GPACDemuxMP4Simple -> Out::Print") {
	auto mp4Demux = create<Demux::GPACDemuxMP4Simple>(&NullHost, "data/beepbop.mp4");
	auto p = create<Out::Print>(std::cout);

	ConnectOutputToInput(mp4Demux->getOutput(0), p->getInput(0));

	mp4Demux->process();
}

unittest("GPACDemuxMP4Full: simple demux one track") {
	auto f = create<In::File>("data/beepbop.mp4");
	auto mp4Demux = create<Demux::GPACDemuxMP4Full>();

	int sampleCount = 0;
	auto onSample = [&](Data) {
		++sampleCount;
	};

	ConnectOutputToInput(f->getOutput(0), mp4Demux->getInput(0));
	ConnectOutput(mp4Demux.get(), onSample);

	f->process();

	ASSERT_EQUALS(215, sampleCount);
}

unittest("GPACDemuxMP4Full: simple demux one empty track") {
	auto f = create<In::File>("data/emptytrack.mp4");
	auto mp4Demux = create<Demux::GPACDemuxMP4Full>();

	int sampleCount = 0;
	auto onSample = [&](Data) {
		++sampleCount;
	};

	ConnectOutputToInput(f->getOutput(0), mp4Demux->getInput(0));
	ConnectOutput(mp4Demux.get(), onSample);

	f->process();

	ASSERT_EQUALS(0, sampleCount);

	auto meta = safe_cast<const MetadataPkt>(mp4Demux->getOutput(0)->getMetadata());
	ASSERT_EQUALS("0142C028FFE1001A6742C028116401E0089F961000000300100000030320F1832480010006681020B8CB20",
	    string2hex(meta->codecSpecificInfo.data(), meta->codecSpecificInfo.size()));
}

unittest("GPACDemuxMP4Full: demux fragments") {
	auto f = create<In::File>("data/fragments.mp4");
	auto mp4Demux = create<Demux::GPACDemuxMP4Full>();

	int sampleCount = 0;
	auto onSample = [&](Data) {
		++sampleCount;
	};

	ConnectOutputToInput(f->getOutput(0), mp4Demux->getInput(0));
	ConnectOutput(mp4Demux.get(), onSample);

	f->process();

	ASSERT_EQUALS(820, sampleCount);
}

