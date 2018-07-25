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
	p.start();
	p.connect(src, 0, dualInput, 1);
	p.waitForEndOfStream();
}

unittest("pipeline: connect while running") {
	Pipeline p;
	auto src = p.addModule<InfiniteSource>();
	ASSERT(src->getNumOutputs() >= 1);
	auto null1 = p.addModule<FakeSink>();
	auto null2 = p.addModule<FakeSink>();
	p.connect(src, 0, null1, 0);
	p.start();
	auto f = [&]() {
		p.connect(src, 0, null2, 0);
		p.exitSync();
		p.waitForEndOfStream();
	};
	std::thread tf(f);
	tf.join();
}

unittest("[DISABLED] pipeline: dynamic module connection of a new source module") {
	Pipeline p;
	auto src = p.addModule<FakeSource, 1>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.start();
	auto demux2 = p.addModule<FakeSource>();
	p.connect(demux2, 0, dualInput, 1);
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module connection of a new sink module") {
	Pipeline p;
	auto src = p.addModule<FakeSource, 1>();
	auto passthru = p.addModule<Passthru>();
	p.connect(src, 0, passthru, 0);
	p.start();
	auto sink = p.addModule<FakeSink>();
	p.connect(src, 0, sink, 0);
	p.waitForEndOfStream();
}

unittest("pipeline: wrong disconnection") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto sink = p.addModule<FakeSink>();
	p.start();
	ASSERT_THROWN(p.disconnect(src, 0, sink, 0));
}

unittest("pipeline: dynamic module disconnection (single ref decrease)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto sink = p.addModule<FakeSink>();
	p.connect(src, 0, sink, 0);
	p.start();
	p.disconnect(src, 0, sink, 0);
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
	struct Source : Modules::Module {
		Source(bool &sent) : sent(sent) {
			out = addOutput<Modules::OutputDefault>();
		}
		void process() {
			while (!sent)
				out->emit(out->getBuffer(0));
		}
		bool &sent;
		Modules::OutputDefault* out;
	};
	struct Receiver : Module {
		Receiver(bool &sent) : sent(sent) {
			addInput(new Modules::Input<Modules::DataBase>(this));
		}
		void process() {
			sent = true;
		}
		bool &sent;
	};

	Pipeline p;
	bool trigger = false;
	auto src = p.addModule<Source>(trigger);
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	bool received = false;
	auto receiver = p.addModule<Receiver>(received);
	p.connect(dualInput, 0, receiver, 0);
	p.start();
	while (!received) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	p.disconnect(src, 0, dualInput, 0);
	p.disconnect(src, 0, dualInput, 1);
	p.disconnect(dualInput, 0, receiver, 0);
	p.removeModule(dualInput);
	trigger = true;
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (remove sink without disconnect)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	ASSERT_THROWN(p.removeModule(dualInput));
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (remove source without disconnect)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	ASSERT_THROWN(p.removeModule(src));
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module disconnection (remove source)") {
	Pipeline p;
	auto src = p.addModule<FakeSource>();
	auto dualInput = p.addModule<DualInput>();
	p.connect(src, 0, dualInput, 0);
	p.connect(src, 0, dualInput, 1);
	p.start();
	p.disconnect(src, 0, dualInput, 0);
	p.disconnect(src, 0, dualInput, 1);
	p.removeModule(src);
	p.waitForEndOfStream();
}

unittest("pipeline: dynamic module addition") {
	Pipeline p;
	auto src = p.addModule<InfiniteSource>();
	p.start();
	auto sink = p.addModule<FakeSink>();
	p.connect(src, 0, sink, 0);
	p.exitSync(); //stop src
	p.waitForEndOfStream();
}
