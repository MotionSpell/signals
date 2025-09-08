#include "lib_signals/signals.hpp"
#include "lib_utils/string_tools.hpp" // makeVector
#include "tests/tests.hpp"

using namespace Tests;
using namespace Signals;

namespace {

std::vector<int> results;

inline void dummy(int a) { results.push_back(a); }
void dummy2(int a) { results.push_back(1 + a); }

unittest("signals_simple") {
  Signal<int> sig;

  auto const id = sig.connect(dummy);

  results.clear();
  sig.emit(100);
  ASSERT_EQUALS(makeVector({100}), results);

  auto id2 = sig.connect(dummy2);
  sig.connect(dummy);
  sig.connect(dummy2);
  results.clear();
  sig.emit(777);

  auto expected = makeVector({777, 778, 777, 778});
  ASSERT_EQUALS(expected, results);

  {
    sig.disconnect(id2);
    sig.disconnect(id);

    // disconnect again
    sig.disconnect(id);

    // wrong id
    sig.disconnect(id + 1);
  }
}

unittest("signals_simple (void return value)") {
  Signal<int> sig;
  sig.connect(dummy);
  sig.emit(100);
}

unittest("connect to lambda") {
  int result = 0;
  Signal<int> sig;
  sig.connect([&](int val) { result = val * val; });
  sig.emit(8);
  ASSERT_EQUALS(64, result);
}
}
