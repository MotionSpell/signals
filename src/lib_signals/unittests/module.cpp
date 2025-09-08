#include "lib_signals/signals.hpp"
#include "lib_utils/string_tools.hpp" // makeVector
#include "tests/tests.hpp"

using namespace Tests;
using namespace Signals;

namespace {

/* member function helper */
template<typename Class, typename MemberFunction>
class MemberFunctor {
  public:
  MemberFunctor(Class *object, MemberFunction function)
      : object(object)
      , function(function) {}

  template<typename... Args>
  void operator()(Args... args) {
    (object->*function)(args...);
  }

  private:
  Class *object;
  MemberFunction function;
};

template<typename Class, typename... Args>
MemberFunctor<Class, void (Class::*)(Args...)> BindMember(Class *objectPtr, void (Class::*memberFunction)(Args...)) {
  return MemberFunctor<Class, void (Class::*)(Args...)>(objectPtr, memberFunction);
}

struct Signaler {
  Signal<int> signal;
};

inline int dummyPrint(int a) { return a; }

struct Slot {
  std::vector<int> vals;
  void slot(int a) { vals.push_back(1 + dummyPrint(a)); }
};

unittest("basic module connection tests") {
  Signaler sender;
  Slot receiver;
  Slot &receiverRef = receiver;
  Slot *receiverPtr = &receiver;
  sender.signal.connect(BindMember(&receiver, &Slot::slot));
  sender.signal.connect(BindMember(&receiver, &Slot::slot));
  sender.signal.connect(BindMember(&receiver, &Slot::slot));
  sender.signal.connect(BindMember(&receiverRef, &Slot::slot));
  sender.signal.connect(BindMember(receiverPtr, &Slot::slot));

  sender.signal.emit(100);
  ASSERT_EQUALS(makeVector({101, 101, 101, 101, 101}), receiver.vals);
}
}
