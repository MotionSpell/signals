#include "tests/tests.hpp"
#include "pipeline_common.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "pipeline_common.hpp"

using namespace Tests;
using namespace Modules;
using namespace Pipelines;

unittest("pipeline: dynamic module connection of an existing module (without modifying the topology)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.waitForEndOfStream();
}

unittest("pipeline: connect while running") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	ASSERT(src->getNumOutputs() >= 1);
	auto null1 = p.addModule<Stub>();
	auto null2 = p.addModule<Stub>();
	p.connect(src, 0, null1, 0);
	p.start();
	auto f = [&]() {
		p.connect(src, 0, null2, 0);
	};
	std::thread tf(f);
	p.waitForEndOfStream();
	tf.join();
}

unittest("[DISABLED] pipeline: dynamic module connection of a new module") {
	Pipeline p;
	auto src = p.addModule<FakeSource, 1>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.start();
	auto demux2 = p.addModule<FakeSource>();
	p.connect(demux2, 0, dualInput, 1);
	//FIXME: if (demux2->isSource()) demux2->process(); //only sources need to be triggered
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: wrong disconnection") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto stub = p.addModule<Stub>();
	p.start();
	ASSERT_THROWN(p.disconnect(src, 0, stub, 0));
}

unittest("pipeline: dynamic module disconnection (single ref decrease)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto stub = p.addModule<Stub>();
	p.connect(src, 0, stub, 0);
	p.start();
	p.disconnect(src, 0, stub, 0);
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (multiple ref decrease)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.disconnect(src, 0, dualInput, 0);
	p.disconnect(src, 0, dualInput, 1);
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (remove module dynamically)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.disconnect(src, 0, dualInput, 0);
	p.disconnect(src, 0, dualInput, 1);
	p.removeModule(dualInput);
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: dynamic module disconnection (remove sink without disconnect)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.removeModule(dualInput);
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: dynamic module disconnection (remove source without disconnect)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.removeModule(src);
	p.waitForEndOfStream();
}

unittest("[DISABLED] pipeline: dynamic module disconnection (remove source)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.disconnect(src, 0, dualInput, 0);
	p.disconnect(src, 0, dualInput, 1);
	//TODO: src->flush(); //we want to keep all the data
	p.removeModule(src);
	p.waitForEndOfStream();
}

//TODO: we should fuzz the creation because it is actually stored with a vector (not thread-safe)
unittest("[DISABLED] pipeline: dynamic module addition") {
	Pipeline p;
	auto src = p.addModule<InfiniteSource>();
	p.start();
	/*TODO: auto f = [&]() {
		p.exitSync();
	};
	std::thread tf(f);*/
	auto stub = p.addModule<Stub>();
	p.connect(src, 0, stub, 0);
	p.waitForEndOfStream();
}
