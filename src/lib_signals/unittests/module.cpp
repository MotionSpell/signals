#include "tests/tests.hpp"
#include "lib_signals/signals.hpp"
#include "lib_utils/queue_inspect.hpp"

using namespace Tests;
using namespace Signals;

namespace {
struct Signaler {
	Signal<int(int)> signal;
};

inline int dummyPrint(int a) {
	return a;
}

struct Slot {
	int slot(int a) {
		return 1 + dummyPrint(a);
	}
};

unittest("basic module connection tests") {
	Signaler sender;
	Signaler &senderRef = sender;
	Signaler *senderPtr = &sender;
	Slot receiver;
	Slot &receiverRef = receiver;
	Slot *receiverPtr = &receiver;
	Connect(sender.signal, &receiver, &Slot::slot);
	Connect(senderRef.signal, &receiver, &Slot::slot);
	Connect(senderPtr->signal, &receiver, &Slot::slot);
	Connect(senderPtr->signal, &receiverRef, &Slot::slot);
	Connect(senderPtr->signal, receiverPtr, &Slot::slot);

	sender.signal.emit(100);
	auto res = sender.signal.results();
	ASSERT_EQUALS(makeVector({101, 101, 101, 101, 101}), transferToVector(*res));
}
}
