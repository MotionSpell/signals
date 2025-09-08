#include "lib_pipeline/pipeline.hpp"
#include "tests/tests.hpp"

#include "pipeline_common.hpp"

using namespace Tests;
using namespace Modules;
using namespace Pipelines;

namespace {

struct Split : public Modules::Module {
  Split(Modules::KHost *host)
      : host(host) {
    addOutput();
    addOutput();
    host->activate(true);
  }
  void process() override { host->activate(false); }
  Modules::KHost *host;
};

}

unittest("pipeline: empty") {
  { Pipeline p; }
  {
    Pipeline p;
    p.start();
  }
  {
    Pipeline p;
    p.waitForEndOfStream();
  }
  {
    Pipeline p;
    p.start();
    p.waitForEndOfStream();
  }
}

unittest("pipeline: connecting an input to an output throws an error") {
  auto p = std::make_unique<Pipeline>();
  auto src = p->addNamedModule<InfiniteSource>("Source");
  auto sink = p->addNamedModule<FakeSink>("Sink");
  ASSERT_THROWN(p->connect(sink, src));
}

unittest("pipeline: exceptions are propagated") {
  static const std::string expected("dummy exception");
  struct ModuleException : public Modules::Module {
    ModuleException(Modules::KHost *host)
        : m_host(host) {
      m_host->activate(true);
    }
    void process() override {
      m_host->activate(false);
      throw std::runtime_error(expected.c_str());
    }
    Modules::KHost *const m_host;
  };

  Pipeline p;
  std::string error;
  p.registerErrorCallback([&](const char *str) {
    error = str;
    return true;
  });
  p.addModule<ModuleException>();
  p.start();
  p.waitForEndOfStream();
  ASSERT_EQUALS(expected, error);
}

// TODO: add more tests with:
//- multithreaded
//- non-source modules (code path is different and does not give control to the app/Exception cbk),
//- different tests with return true/false in registerErrorCallback()
//- when one source is stopped, what happens to the other sources? can user call exitSync() from the error callback?

unittest("pipeline: pipeline with split (no join)") {
  Pipeline p;
  auto src = p.addModule<Split>();
  ASSERT(src->getNumOutputs() >= 2);
  for(int i = 0; i < src->getNumOutputs(); ++i) {
    auto passthru = p.addModule<Passthru>();
    p.connect(GetOutputPin(src, i), passthru);
    auto sink = p.addModule<FakeSink>();
    p.connect(passthru, sink);
  }

  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: pipeline with split (join)") {
  Pipeline p;
  auto src = p.addNamedModule<Split>("split");
  auto sink = p.addNamedModule<FakeSink>("FakeSink");
  auto passthru1 = p.addNamedModule<Passthru>("Passthru1");
  auto passthru2 = p.addNamedModule<Passthru>("Passthru2");

  p.connect(GetOutputPin(src, 0), passthru1);
  p.connect(passthru1, sink, true);

  p.connect(GetOutputPin(src, 1), passthru2);
  p.connect(passthru2, sink, true);

  p.start();
  p.waitForEndOfStream();
}

unittest("pipeline: multiple inputs (send same packets to 2 inputs and check call count)") {
  Pipeline p;
  auto generator = p.addModule<FakeSource>(1);
  auto dualInput = p.addModule<ThreadedDualInput>();
  p.connect(generator, GetInputPin(dualInput, 0));
  p.connect(generator, GetInputPin(dualInput, 1));
  p.start();
  p.waitForEndOfStream();
  ASSERT_EQUALS(ThreadedDualInput::numCalls, 1u);
}
