#include "tests/tests.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/out/null.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "pipeline_common.hpp"

using namespace Tests;
using namespace Modules;
using namespace Pipelines;

namespace {

unittest("pipeline: dynamic module connection of an existing module (without modifying the topology)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.connect(demux, 0, dualInput, 1);
	p.start();
	p.waitForEndOfStream();
}

unittest("pipeline: connect while running") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	ASSERT(demux->getNumOutputs() > 1);
	auto null1 = p.addModule<Out::Null>();
	auto null2 = p.addModule<Out::Null>();
	p.connect(demux, 0, null1, 0);
	p.start();
	auto f = [&]() {
		p.connect(demux, 0, null2, 0);
	};
	std::thread tf(f);
	p.waitForEndOfStream();
	tf.join();
}

unittest("[DISABLED] pipeline: dynamic module connection of a new module") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux, 1>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.start();
	auto demux2 = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	p.connect(demux2, 0, dualInput, 1);
	//FIXME: if (demux2->isSource()) demux2->process(); //only sources need to be triggered
	p.waitForEndOfStream();
}

unittest("[DISABLED]` pipeline: wrong disconnection") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto null = p.addModule<Out::Null>();
	p.start();
	ASSERT_THROWN(p.disconnect(demux, 0, null, 0));
}

unittest("pipeline: dynamic module disconnection (single ref decrease)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto null = p.addModule<Out::Null>();
	p.connect(demux, 0, null, 0);
	p.start();
	p.disconnect(demux, 0, null, 0);
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (multiple ref decrease)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.connect(demux, 0, dualInput, 1);
	p.start();
	p.disconnect(demux, 0, dualInput, 0);
	p.disconnect(demux, 0, dualInput, 1);
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (remove module dynamically)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.connect(demux, 0, dualInput, 1);
	p.start();
	p.disconnect(demux, 0, dualInput, 0);
	p.disconnect(demux, 0, dualInput, 1);
	p.removeModule(dualInput);
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: dynamic module disconnection (remove sink without disconnect)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.connect(demux, 0, dualInput, 1);
	p.start();
	p.removeModule(dualInput);
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: dynamic module disconnection (remove source without disconnect)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.connect(demux, 0, dualInput, 1);
	p.start();
	p.removeModule(demux);
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: dynamic module disconnection (remove source)") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4");
	auto dualInput = p.addModule<DualInput>();
	p.connect(demux, 0, dualInput, 0);
	p.connect(demux, 0, dualInput, 1);
	p.start();
	p.disconnect(demux, 0, dualInput, 0);
	p.disconnect(demux, 0, dualInput, 1);
	//TODO: demux->flush(); //we want to keep all the data
	p.removeModule(demux);
	p.waitForEndOfStream();
}

//TODO: we should fuzz the creation because it is actually stored with a vector (not thread-safe)
unittest("[DISABLED] pipeline: dynamic module addition") {
	Pipeline p;
	auto demux = p.addModule<Demux::LibavDemux>("data/beepbop.mp4", true);
	p.start();
	/*TODO: auto f = [&]() {
		p.exitSync();
	};
	std::thread tf(f);*/
	auto null = p.addModule<Out::Null>();
	p.connect(demux, 0, null, 0);
	p.waitForEndOfStream();
}

}
