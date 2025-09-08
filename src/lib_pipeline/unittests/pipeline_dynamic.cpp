#include "lib_pipeline/pipeline.hpp"
#include "tests/tests.hpp"

#include <atomic>

#include "pipeline_common.hpp"

using namespace Tests;
using namespace Modules;
using namespace Pipelines;

using Bool = std::atomic<bool>;
using namespace std::chrono;

namespace {

struct Source : Modules::Module {
  Source(KHost *host, Bool &sent)
      : sent(sent)
      , host(host) {
    out = addOutput();
    host->activate(true);
  }
  void process() override {
    out->post(out->allocData<DataRaw>(1));
    if(sent)
      host->activate(false);
  }
  Bool &sent;
  Modules::KHost *host;
  Modules::OutputDefault *out;
};
struct Receiver : Module {
  Receiver(KHost *, Bool &sent)
      : sent(sent) {
    addInput();
  }
  void process() { sent = true; }
  Bool &sent;
};

}

unittest("pipeline: dynamic module connection of an existing module (without modifying the topology)") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, GetInputPin(dualInput, 0));
  p.start();
  p.connect(src, GetInputPin(dualInput, 1));
}

unittest("pipeline: connect while running") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  ASSERT(src->getNumOutputs() >= 1);
  auto null1 = p.addModule<FakeSink>();
  auto null2 = p.addModule<FakeSink>();
  p.connect(src, null1);
  p.start();
  auto f = [&]() { p.connect(src, null2); };
  std::thread tf(f);
  tf.join();
}

unittest("pipeline: dynamic module connection of a new source module") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, dualInput);
  Bool received{false};
  auto receiver = p.addModule<Receiver>(received);
  p.connect(dualInput, receiver);
  p.start();
  auto src2 = p.addModule<FakeSource>(1);
  p.connect(src2, GetInputPin(dualInput, 1));
  p.start(); // start the new source
  while(!received) {
    std::this_thread::sleep_for(10ms);
  }
  p.exitSync(); // stop src
  p.waitForEndOfStream();
}

unittest("pipeline: remove a running source module") {
  Pipeline p;
  auto src1 = p.addModule<InfiniteSource>();
  p.addModule<InfiniteSource>();
  p.start();
  p.removeModule(src1);
}

unittest("pipeline: dynamic module connection of a new sink module") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto passthru = p.addModule<Passthru>();
  p.connect(src, passthru);
  p.start();
  auto sink = p.addModule<FakeSink>();
  p.connect(src, sink);
}

unittest("pipeline: disconnecting not-connected modules throws an error") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto sink = p.addModule<FakeSink>();
  p.start();
  ASSERT_THROWN(p.disconnect(src, 0, sink, 0));
}

unittest("pipeline: dynamic module disconnection (single ref decrease)") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto sink = p.addModule<FakeSink>();
  p.connect(src, sink);
  p.start();
  p.disconnect(src, 0, sink, 0);
}

unittest("pipeline: dynamic module disconnection (multiple ref decrease)") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, GetInputPin(dualInput, 0));
  p.connect(src, GetInputPin(dualInput, 1));
  p.start();
  p.disconnect(src, 0, dualInput, 0);
  p.disconnect(src, 0, dualInput, 1);
}

unittest("pipeline: dynamic module disconnection (remove module dynamically)") {
  // |           /=> .-----------.
  // |           |   |           |
  // | [Source] =|   | DualInput | => [Receiver]
  // |           |   |           |
  // |           \=> .-----------.
  Pipeline p;
  Bool trigger{false};
  auto src = p.addModule<Source>(trigger);
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, GetInputPin(dualInput, 0));
  p.connect(src, GetInputPin(dualInput, 1));
  Bool received{false};
  auto receiver = p.addModule<Receiver>(received);
  p.connect(dualInput, receiver);
  p.start();
  while(!received) {
    std::this_thread::sleep_for(10ms);
  }

  // Remove the middle filter
  p.disconnect(src, 0, dualInput, 0);
  p.disconnect(src, 0, dualInput, 1);
  p.disconnect(dualInput, 0, receiver, 0);
  p.removeModule(dualInput);

  trigger = true;
  p.waitForEndOfStream();
}

unittest("pipeline: removing a connected sink module throws an error") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, GetInputPin(dualInput, 0));
  p.connect(src, GetInputPin(dualInput, 1));
  p.start();
  ASSERT_THROWN(p.removeModule(dualInput));
}

unittest("pipeline: removing a connected source module throws an error") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, GetInputPin(dualInput, 0));
  p.connect(src, GetInputPin(dualInput, 1));
  p.start();
  ASSERT_THROWN(p.removeModule(src));
}

unittest("pipeline: dynamic module disconnection (remove source)") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, GetInputPin(dualInput, 0));
  p.connect(src, GetInputPin(dualInput, 1));
  p.start();
  p.disconnect(src, 0, dualInput, 0);
  p.disconnect(src, 0, dualInput, 1);
  p.removeModule(src);
}

unittest("pipeline: dynamic module addition") {
  Pipeline p;
  auto src = p.addModule<InfiniteSource>();
  p.start();
  auto sink = p.addModule<FakeSink>();
  p.connect(src, sink);
}
