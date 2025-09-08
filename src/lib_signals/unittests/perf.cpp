#include "lib_signals/signals.hpp"
#include "lib_utils/profiler.hpp"
#include "tests/tests.hpp"

#include <sstream>
#include <vector>

using namespace Tests;
using namespace Signals;

// #define ENABLE_PERF_TESTS

#ifdef ENABLE_PERF_TESTS
namespace {
inline int compute(int a) {
  int64_t n = (int64_t)1 << a;
  if(a <= 0) {
    return 1;
  }
  uint64_t res = n;
  while(--n > 1) {
    res *= n;
  }
  return (int)res;
}

inline int log2(int i) {
  int res = 0;
  while(i >>= 1) {
    ++res;
  }
  return res;
}

inline bool isPow2(int i) { return (i == 0) || (i - (1 << (int)log2(i)) == 0); }

#define TEST_MAX_SIZE (1 << 12)
#define TEST_TIMEOUT_IN_US 800000

template<typename SignalSignature, typename Result, template<typename> class ExecutorTemplate, typename ValType>
void emitTest(std::function<SignalSignature> f, ValType val) {
  ExecutorTemplate<SignalSignature> executor;
  Signal<SignalSignature, Result> sig(executor);
  std::vector<size_t> id(TEST_MAX_SIZE);
  bool timeout = false;
  for(int i = 0; i < TEST_MAX_SIZE + 1; ++i) {
    if(isPow2(i)) {
      {
        std::stringstream ss;
        ss << "Emit time for " << FORMAT(i, TEST_MAX_SIZE) << " connected callbacks";
        if(i > 0) {
          id[i - 1] = sig.connect(f);
        }
        Tools::Profiler p(ss.str());
        sig.emit(val);
        auto res = sig.results();
        if(p.elapsedInUs() > TEST_TIMEOUT_IN_US) {
          timeout = true;
        }
      }
      {
        std::stringstream ss;
        ss << FORMAT(i, TEST_MAX_SIZE) << " direct calls                     ";
        Tools::Profiler p(ss.str());
        for(int j = 0; j < i; ++j) {
          f(val);
        }
        if(p.elapsedInUs() > 2 * TEST_TIMEOUT_IN_US) {
          timeout = true;
        }
      }
      if(timeout) {
        std::cout << "Automatic timeout: current test will be stopped (success)" << std::endl;
        return;
      }
    } else {
      id[i - 1] = sig.connect(f);
    }
  }
}

unittest("create a signal") {
  {
    Tools::Profiler p("Create void(void)");
    Signal<void(void)> sig;
  }
  {
    Tools::Profiler p("Create int(int)");
    for(int i = 0; i < TEST_MAX_SIZE; ++i) {
      Signal<int(int)> sig;
    }
  }
  {
    Tools::Profiler p("Create int(int x 8)");
    for(int i = 0; i < TEST_MAX_SIZE; ++i) {
      Signal<int(int, int, int, int, int, int, int, int)> sig;
    }
  }
}

unittest("connect and disconnect a high number of callbacks on one signal") {
  Signal<int(int)> sig;
  std::vector<size_t> id(TEST_MAX_SIZE + 1);
  for(int i = 0; i < TEST_MAX_SIZE + 1; ++i) {
    std::stringstream ss;
    if(isPow2(i)) {
      ss << "Connect number    " << FORMAT(i, TEST_MAX_SIZE);
      Tools::Profiler p(ss.str());
      id[i] = sig.connect(dummy);
    } else {
      id[i] = sig.connect(dummy);
    }
  }
  for(int i = 0; i < TEST_MAX_SIZE + 1; ++i) {
    std::stringstream ss;
    if(isPow2(i)) {
      ss << "Disconnect number " << FORMAT(i, TEST_MAX_SIZE);
      Tools::Profiler p(ss.str());
      bool res = sig.disconnect(id[i]);
      ASSERT(res);
    } else {
      bool res = sig.disconnect(id[i]);
      ASSERT(res);
    }
  }
}

// dummy unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit dummy  on pool") { emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(dummy, 1789); }
unittest("unsafe emit dummy  on  sync") { emitTest<int(int), ResultVector<void>, ExecutorSync, int>(dummy, 1789); }

// dummy safe
unittest("safe emit dummy  on pool") { emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(dummy, 1789); }
unittest("safe emit dummy  on  sync") { emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(dummy, 1789); }

// dummy unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit dummy  on pool") { emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(dummy, 1789); }
unittest("unsafe emit dummy  on  sync") { emitTest<int(int), ResultVector<void>, ExecutorSync, int>(dummy, 1789); }

// dummy safe
unittest("safe emit dummy  on pool") { emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(dummy, 1789); }
unittest("safe emit dummy  on  sync") { emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(dummy, 1789); }

// light computation (~1us) unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit light computation on pool") {
  emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(compute, 12);
}
unittest("unsafe emit light computation on  sync") {
  emitTest<int(int), ResultVector<void>, ExecutorSync, int>(compute, 12);
}

// light computation (~1us) safe
unittest("safe emit light computation on pool") {
  emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(compute, 12);
}
unittest("safe emit light computation on  sync") {
  emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(compute, 12);
}

// heavy computation (~40ms) unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit heavy computation on pool") {
  emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(compute, 25);
}
unittest("unsafe emit heavy computation on  sync") {
  emitTest<int(int), ResultVector<void>, ExecutorSync, int>(compute, 25);
}

// heavy computation (~40ms) safe
unittest("safe emit heavy computation on pool") {
  emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(compute, 25);
}
unittest("safe emit heavy computation on  sync") {
  emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(compute, 25);
}

// sleep unsafe
unittest("unsafe emit sleep   on  pool") {
  emitTest<void(int), ResultVector<void>, ExecutorThreadPool, int>(sleepInMs, 100);
}
unittest("unsafe emit sleep   on  sync") { emitTest<void(int), ResultVector<void>, ExecutorSync, int>(sleepInMs, 100); }

// sleep safe
unittest("safe emit sleep   on  pool") {
  emitTest<void(int), ResultQueue<void>, ExecutorThreadPool, int>(sleepInMs, 100);
}
unittest("safe emit sleep   on  sync") { emitTest<void(int), ResultQueue<void>, ExecutorSync, int>(sleepInMs, 100); }

}
#endif // ENABLE_PERF_TESTS
