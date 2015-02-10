#include "tests.hpp"
#include "signals.hpp"
#include <sstream>
#include <vector>

using namespace Tests;
using namespace Signals;

namespace {
template<typename SignalSignature, typename Result, template<typename> class ExecutorTemplate, typename ValType>
void emitTest(std::function<SignalSignature> f, ValType val) {
	ExecutorTemplate<SignalSignature> executor;
	Signal<SignalSignature, Result> sig(executor);
	std::vector<size_t> id(TEST_MAX_SIZE);
	bool timeout = false;
	for (int i = 0; i < TEST_MAX_SIZE + 1; ++i) {
		if (Util::isPow2(i)) {
			{
				std::stringstream ss;
				ss << "Emit time for " << FORMAT(i, TEST_MAX_SIZE) << " connected callbacks";
				if (i > 0) {
					id[i - 1] = sig.connect(f);
				}
				Tools::Profiler p(ss.str());
				sig.emit(val);
				auto res = sig.results();
				if (p.elapsedInUs() > TEST_TIMEOUT_IN_US) {
					timeout = true;
				}
			}
			{
				std::stringstream ss;
				ss << FORMAT(i, TEST_MAX_SIZE) << " direct calls                     ";
				Tools::Profiler p(ss.str());
				for (int j = 0; j < i; ++j) {
					f(val);
				}
				if (p.elapsedInUs() > 2 * TEST_TIMEOUT_IN_US) {
					timeout = true;
				}
			}
			if (timeout) {
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
		for (int i = 0; i < TEST_MAX_SIZE; ++i) {
			Signal<int(int)> sig;
		}
	}
	{
		Tools::Profiler p("Create int(int x 8)");
		for (int i = 0; i < TEST_MAX_SIZE; ++i) {
			Signal<int(int, int, int, int, int, int, int, int)> sig;
		}
	}
}

unittest("connect and disconnect a high number of callbacks on one signal") {
	Signal<int(int)> sig;
	std::vector<size_t> id(TEST_MAX_SIZE + 1);
	for (int i = 0; i < TEST_MAX_SIZE + 1; ++i) {
		std::stringstream ss;
		if (Util::isPow2(i)) {
			ss << "Connect number    " << FORMAT(i, TEST_MAX_SIZE);
			Tools::Profiler p(ss.str());
			id[i] = sig.connect(Util::dummy);
		} else {
			id[i] = sig.connect(Util::dummy);
		}
	}
	for (int i = 0; i < TEST_MAX_SIZE + 1; ++i) {
		std::stringstream ss;
		if (Util::isPow2(i)) {
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

//dummy unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit dummy  on async") {
	emitTest<int(int), ResultVector<void>, ExecutorAsync, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on auto") {
	emitTest<int(int), ResultVector<void>, ExecutorAuto, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on pool") {
	emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on  sync") {
	emitTest<int(int), ResultVector<void>, ExecutorSync, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on  lazy") {
	emitTest<int(int), ResultVector<void>, ExecutorLazy, int>(Util::dummy, 1789);
}

//dummy safe
unittest("safe emit dummy  on async") {
	emitTest<int(int), ResultQueue<int>, ExecutorAsync, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on auto") {
	emitTest<int(int), ResultQueue<int>, ExecutorAuto, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on pool") {
	emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on  sync") {
	emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on  lazy") {
	emitTest<int(int), ResultQueue<int>, ExecutorLazy, int>(Util::dummy, 1789);
}

//dummy unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit dummy  on async") {
	emitTest<int(int), ResultVector<void>, ExecutorAsync, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on auto") {
	emitTest<int(int), ResultVector<void>, ExecutorAuto, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on pool") {
	emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on  sync") {
	emitTest<int(int), ResultVector<void>, ExecutorSync, int>(Util::dummy, 1789);
}
unittest("unsafe emit dummy  on  lazy") {
	emitTest<int(int), ResultVector<void>, ExecutorLazy, int>(Util::dummy, 1789);
}

//dummy safe
unittest("safe emit dummy  on async") {
	emitTest<int(int), ResultQueue<int>, ExecutorAsync, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on auto") {
	emitTest<int(int), ResultQueue<int>, ExecutorAuto, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on pool") {
	emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on  sync") {
	emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(Util::dummy, 1789);
}
unittest("safe emit dummy  on  lazy") {
	emitTest<int(int), ResultQueue<int>, ExecutorLazy, int>(Util::dummy, 1789);
}

//light computation (~1us) unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit light computation on async") {
	emitTest<int(int), ResultVector<void>, ExecutorAsync, int>(Util::compute, 12);
}
unittest("unsafe emit light computation on auto") {
	emitTest<int(int), ResultVector<void>, ExecutorAuto, int>(Util::compute, 12);
}
unittest("unsafe emit light computation on pool") {
	emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(Util::compute, 12);
}
unittest("unsafe emit light computation on  sync") {
	emitTest<int(int), ResultVector<void>, ExecutorSync, int>(Util::compute, 12);
}
unittest("unsafe emit light computation on  lazy") {
	emitTest<int(int), ResultVector<void>, ExecutorLazy, int>(Util::compute, 12);
}

//light computation (~1us) safe
unittest("safe emit light computation on async") {
	emitTest<int(int), ResultQueue<int>, ExecutorAsync, int>(Util::compute, 12);
}
unittest("safe emit light computation on auto") {
	emitTest<int(int), ResultQueue<int>, ExecutorAuto, int>(Util::compute, 12);
}
unittest("safe emit light computation on pool") {
	emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(Util::compute, 12);
}
unittest("safe emit light computation on  sync") {
	emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(Util::compute, 12);
}
unittest("safe emit light computation on  lazy") {
	emitTest<int(int), ResultQueue<int>, ExecutorLazy, int>(Util::compute, 12);
}

//heavy computation (~40ms) unsafe - the result type is set to void to avoid crashed
unittest("unsafe emit heavy computation on async") {
	emitTest<int(int), ResultVector<void>, ExecutorAsync, int>(Util::compute, 25);
}
unittest("unsafe emit heavy computation on auto") {
	emitTest<int(int), ResultVector<void>, ExecutorAuto, int>(Util::compute, 25);
}
unittest("unsafe emit heavy computation on pool") {
	emitTest<int(int), ResultVector<void>, ExecutorThreadPool, int>(Util::compute, 25);
}
unittest("unsafe emit heavy computation on  sync") {
	emitTest<int(int), ResultVector<void>, ExecutorSync, int>(Util::compute, 25);
}
unittest("unsafe emit heavy computation on  lazy") {
	emitTest<int(int), ResultVector<void>, ExecutorLazy, int>(Util::compute, 25);
}

//heavy computation (~40ms) safe
unittest("safe emit heavy computation on async") {
	emitTest<int(int), ResultQueue<int>, ExecutorAsync, int>(Util::compute, 25);
}
unittest("safe emit heavy computation on auto") {
	emitTest<int(int), ResultQueue<int>, ExecutorAuto, int>(Util::compute, 25);
}
unittest("safe emit heavy computation on pool") {
	emitTest<int(int), ResultQueue<int>, ExecutorThreadPool, int>(Util::compute, 25);
}
unittest("safe emit heavy computation on  sync") {
	emitTest<int(int), ResultQueue<int>, ExecutorSync, int>(Util::compute, 25);
}
unittest("safe emit heavy computation on  lazy") {
	emitTest<int(int), ResultQueue<int>, ExecutorLazy, int>(Util::compute, 25);
}

//sleep unsafe
unittest("unsafe emit sleep   on async") {
	emitTest<void(int), ResultVector<void>, ExecutorAsync, int>(Util::sleepInMs, 100);
}
unittest("unsafe emit sleep   on  auto") {
	emitTest<void(int), ResultVector<void>, ExecutorAuto, int>(Util::sleepInMs, 100);
}
unittest("unsafe emit sleep   on  pool") {
	emitTest<void(int), ResultVector<void>, ExecutorThreadPool, int>(Util::sleepInMs, 100);
}
unittest("unsafe emit sleep   on  sync") {
	emitTest<void(int), ResultVector<void>, ExecutorSync, int>(Util::sleepInMs, 100);
}
unittest("unsafe emit sleep   on  lazy") {
	emitTest<void(int), ResultVector<void>, ExecutorLazy, int>(Util::sleepInMs, 100);
}

//sleep safe
unittest("safe emit sleep   on async") {
	emitTest<void(int), ResultQueue<void>, ExecutorAsync, int>(Util::sleepInMs, 100);
}
unittest("safe emit sleep   on  auto") {
	emitTest<void(int), ResultQueue<void>, ExecutorAuto, int>(Util::sleepInMs, 100);
}
unittest("safe emit sleep   on  pool") {
	emitTest<void(int), ResultQueue<void>, ExecutorThreadPool, int>(Util::sleepInMs, 100);
}
unittest("safe emit sleep   on  sync") {
	emitTest<void(int), ResultQueue<void>, ExecutorSync, int>(Util::sleepInMs, 100);
}
unittest("safe emit sleep   on  lazy") {
	emitTest<void(int), ResultQueue<void>, ExecutorLazy, int>(Util::sleepInMs, 100);
}
}

