#include "lib_utils/scheduler.hpp"

#include "lib_utils/fraction.hpp"
#include "lib_utils/queue.hpp"
#include "lib_utils/sysclock.hpp"
#include "tests/tests.hpp"

using std::make_shared;

// allows ASSERT_EQUALS on fractions
static std::ostream &operator<<(std::ostream &o, Fraction f) {
  o << f.num << "/" << f.den;
  return o;
}

namespace {

template<typename T>
std::vector<T> transferToVector(Queue<T> &q) {
  std::vector<T> r;
  T val;
  while(q.tryPop(val))
    r.push_back(val);
  return r;
}

auto const f0 = Fraction(0, 1000);
auto const f1 = Fraction(1, 1000);
auto const f10 = Fraction(10, 1000);
auto const f50 = Fraction(50, 1000);
auto const f1000 = Fraction(1, 1);

// A fully offline clock+timer implementation.
// To advance the time, call 'sleep'.
class TestClock : public IClock, public ITimer {
  public:
  // ITimer
  void scheduleIn(std::function<void()> &&task, Fraction delay) {
    callback = task;
    eventTime = curr + delay;
  }

  // IClock
  virtual Fraction now() const { return curr; }

  virtual void sleep(Fraction delay) const {
    auto nonConstThis = const_cast<TestClock *>(this);
    nonConstThis->advanceTime(delay);
  }

  virtual double getSpeed() const { return 1.0; }

  private:
  void advanceTime(Fraction delta) {
    curr = curr + delta;
    if(callback && curr >= eventTime) {

      // invoking the callback might modify 'this->callback'
      auto cb = std::move(callback);

      cb();
    }
  }

  Fraction curr = 0;
  Fraction eventTime;
  std::function<void()> callback;
};

unittest("scheduler: basic") {
  auto clk = make_shared<TestClock>();
  Scheduler s(clk, clk);
}

unittest("scheduler: scheduled events are delayed") {
  Queue<Fraction> q;
  auto f = [&](Fraction time) { q.push(time); };

  auto clk = make_shared<TestClock>();
  Scheduler s(clk, clk);
  s.scheduleIn(f, f50);
  ASSERT(transferToVector(q).empty());
}

unittest("scheduler: scheduled events are not delayed too much") {
  Queue<Fraction> q;
  auto f = [&](Fraction time) { q.push(time); };

  auto clock = make_shared<TestClock>();
  Scheduler s(clock, clock);
  s.scheduleIn(f, 0);
  clock->sleep(f50);
  ASSERT_EQUALS(1u, transferToVector(q).size());
}

unittest("scheduler: expired scheduled events are executed, but not the others") {
  Queue<Fraction> q;
  auto f = [&](Fraction time) { q.push(time); };

  auto clock = make_shared<TestClock>();
  Scheduler s(clock, clock);
  s.scheduleIn(f, 0);
  s.scheduleIn(f, f1000);
  clock->sleep(f50);
  ASSERT_EQUALS(1u, transferToVector(q).size());
}

unittest("scheduler: absolute-time scheduled events are received in order") {
  Queue<Fraction> q;
  auto f = [&](Fraction time) { q.push(time); };

  auto clock = make_shared<TestClock>();
  Scheduler s(clock, clock);
  auto const now = clock->now();
  s.scheduleAt(f, now + f0);
  s.scheduleAt(f, now + f1);
  clock->sleep(f50);
  auto v = transferToVector(q);
  ASSERT_EQUALS(2u, v.size());
  auto const t1 = v[0], t2 = v[1];
  ASSERT_EQUALS(f1, t2 - t1);
}

unittest("scheduleEvery: periodic events are executed periodically") {
  Queue<Fraction> q;
  auto f = [&](Fraction time) { q.push(time); };
  auto const period = Fraction(10, 1000);

  {
    auto clock = make_shared<TestClock>();
    Scheduler s(clock, clock);
    scheduleEvery(&s, f, period, 0);
    for(int i = 0; i < 5; ++i)
      clock->sleep(f10);
  }
  auto v = transferToVector(q);
  ASSERT(v.size() >= 3);
  auto const t1 = v[0], t2 = v[1];
  ASSERT_EQUALS(period, t2 - t1);
}

unittest("scheduler: events scheduled out-of-order are executed in order") {
  std::string order; // marker values

  {
    auto clock = make_shared<TestClock>();
    Scheduler s(clock, clock);
    s.scheduleIn([&](Fraction) { order += 'B'; }, f50);
    s.scheduleIn([&](Fraction) { order += 'A'; }, f10);
    clock->sleep(Fraction(1, 10));
  }
  ASSERT_EQUALS(std::string("AB"), order);
}

unittest("scheduler: can still schedule and trigger 'near' tasks while waiting for a 'far' one") {
  auto const oneMsec = Fraction(1, 1000);
  auto const oneHour = Fraction(3600, 1);

  auto clock = make_shared<TestClock>();
  Queue<Fraction> q;
  auto f = [&](Fraction /*time*/) { q.push(clock->now()); };

  Scheduler s(clock, clock);
  s.scheduleIn(f, oneHour);
  clock->sleep(f10); // let the scheduler run and start waiting for oneHour
  s.scheduleIn(f, oneMsec); // now schedule an imminent task
  clock->sleep(f10 * 3); // allow some time for the imminent task to run

  // don't destroy 's' now, as it would wake it up and miss the goal of this test

  auto v = transferToVector(q);
  ASSERT_EQUALS(1u, v.size());
}

unittest("scheduler: task cancellation") {
  bool task1done = false;
  bool task2done = false;

  auto task1 = [&](Fraction) { task1done = true; };
  auto task2 = [&](Fraction) { task2done = true; };

  auto clock = make_shared<TestClock>();
  Scheduler s(clock, clock);
  auto const now = clock->now();
  auto h1 = s.scheduleAt(task1, now + f1);
  s.scheduleAt(task2, now + f10);
  s.cancel(h1);
  clock->sleep(f50);

  ASSERT(!task1done);
  ASSERT(task2done);
}

}
