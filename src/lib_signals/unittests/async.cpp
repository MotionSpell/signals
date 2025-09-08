#include "lib_signals/signals.hpp"
#include "tests/tests.hpp"

#include <thread>

using namespace Tests;
using namespace Signals;

namespace {

inline void sleepInMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

secondclasstest("destroy on execution") {
  Signal<int> sig;
  sig.connect(sleepInMs);
  sig.emit(1000);
}

secondclasstest("disconnect before execution") {
  Signal<int> sig;
  auto uid = sig.connect(sleepInMs);
  sig.disconnect(uid);
  sig.emit(1000);
}

secondclasstest("disconnect on execution") {
  Signal<int> sig;
  auto uid = sig.connect(sleepInMs);
  sig.emit(1000);
  sig.disconnect(uid);
}
}
