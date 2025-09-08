#include "lib_pipeline/pipeline.hpp"
#include "tests/tests.hpp"

#include "pipeline_common.hpp"

using namespace std;
using namespace Tests;
using namespace Modules;
using namespace Pipelines;

uint64_t ThreadedDualInput::numCalls = 0;

unittest("pipeline: source only") {
  Pipeline p;
  auto src = p.addModule<FakeSource>();
  ASSERT(src->getNumOutputs() >= 1);
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: a non left-connected module is not a source") {
  Pipeline p;
  auto data = make_shared<DataRaw>(1);
  p.addModule<FakeSink>();
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: connect one input (out of 2) to one output") {
  Pipeline p;
  auto src = p.addModule<FakeSource>();
  ASSERT(src->getNumOutputs() >= 1);
  auto sink = p.addModule<FakeSink>();
  p.connect(src, sink);
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: connect two outputs to the same input") {
  Pipeline p;
  auto src1 = p.addModule<FakeSource>();
  auto src2 = p.addModule<FakeSource>();
  auto sink = p.addModule<FakeSink>();
  p.connect(src1, sink);
  p.connect(src2, sink, true);
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: connect passthru to a multiple input module (1)") {
  Pipeline p;
  auto src = p.addModule<FakeSource>();
  ASSERT(src->getNumOutputs() >= 1);
  auto passthru = p.addModule<Passthru>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, passthru);
  p.connect(passthru, GetInputPin(dualInput, 0));
  p.connect(passthru, GetInputPin(dualInput, 1));
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: connect passthru to a multiple input module (2)") {
  Pipeline p;
  auto src = p.addModule<FakeSource>();
  ASSERT(src->getNumOutputs() >= 1);
  auto passthru0 = p.addModule<Passthru>();
  auto passthru1 = p.addModule<Passthru>();
  auto passthru2 = p.addModule<Passthru>();
  auto dualInput = p.addModule<DualInput>();
  p.connect(src, passthru0);
  p.connect(passthru0, passthru1);
  p.connect(passthru0, passthru2);
  p.connect(passthru1, GetInputPin(dualInput, 0));
  p.connect(passthru2, GetInputPin(dualInput, 1));
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: orphan dynamic inputs sink") {
  Pipeline p;
  auto src = p.addModule<FakeSource>();
  auto sink = p.addModule<FakeSink>();
  p.connect(src, sink);
  p.addModule<FakeSink>();
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: sink only (incorrect topology)") {
  Pipeline p;
  p.addModule<FakeSink>();
  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: passthru after split") {
  Pipeline p;
  auto generator = p.addModule<FakeSource>(1);
  auto dualInput = p.addModule<ThreadedDualInput>();
  p.connect(generator, GetInputPin(dualInput, 0));
  p.connect(generator, GetInputPin(dualInput, 1));
  auto passthru = p.addModule<Passthru>();
  p.connect(dualInput, passthru);
  p.start();
  p.waitForEndOfStream();
}
