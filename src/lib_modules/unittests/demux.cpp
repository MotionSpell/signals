#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
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
	for(size_t i=0; i < times.size()-1; ++i)
		r.push_back(times[i+1] - times[i]);
	return r;
}
}

unittest("LibavDemux: rollover") {

	struct MyOutput : ModuleS {
		MyOutput() {
			addInput(new Input<DataBase>(this));
		}
		vector<int64_t> times;
		void process(Data data) override {
			times.push_back(data->getMediaTime());
		}
	};

	auto demux = create<Demux::LibavDemux>("data/rollover.ts");
	auto rec = create<MyOutput>();
	ConnectOutputToInput(demux->getOutput(0), rec->getInput(0));

	demux->process(nullptr);
	demux->flush();

	vector<int64_t> expected(73, 7200);
	ASSERT_EQUALS(expected, deltas(rec->times));
}

namespace {

secondclasstest("demux one track: Demux::GPACDemuxMP4Simple -> Out::Print") {
	auto mp4Demux = create<Demux::GPACDemuxMP4Simple>("data/beepbop.mp4");
	auto p = create<Out::Print>(std::cout);

	ConnectOutputToInput(mp4Demux->getOutput(0), p->getInput(0));

	mp4Demux->process(nullptr);
}

secondclasstest("[DISABLED] demux one track: File -> Demux::GPACDemuxMP4Full -> Out::Print") {
	auto f = create<In::File>("data/beepbop.mp4");
	auto mp4Demux = create<Demux::GPACDemuxMP4Full>();
	auto p = create<Out::Print>(std::cout);

	ConnectOutputToInput(f->getOutput(0), mp4Demux->getInput(0));
	ConnectOutputToInput(mp4Demux->getOutput(0), p->getInput(0));

	f->process(nullptr);
}

}
