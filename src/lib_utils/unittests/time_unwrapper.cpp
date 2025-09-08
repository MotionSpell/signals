#include "../time_unwrapper.hpp"

#include "tests/tests.hpp"

#include <vector>

using namespace std;

// use a human-readable rollover period for the tests
auto const PERIOD = 1000;

static vector<int64_t> unwrapSequence(vector<int64_t> seq) {
  TimeUnwrapper u;
  u.WRAP_PERIOD = PERIOD;

  vector<int64_t> r;
  for(auto value : seq)
    r.push_back(u.unwrap(value));
  return r;
}

unittest("time_unwrapper: 64-bit wrap period") {
  TimeUnwrapper unwrapper;
  unwrapper.WRAP_PERIOD = 1LL << 50;
  ASSERT_EQUALS(0, unwrapper.unwrap(0));
}

unittest("time_unwrapper: monotonic passthrough") {
  auto expected = vector<int64_t>({0, 1, 2, 3, 4});
  ASSERT_EQUALS(expected, unwrapSequence({0, 1, 2, 3, 4}));
}

unittest("time_unwrapper: simple rollover") {
  auto expected = vector<int64_t>({
        990,
        1000,
        1010,
  });
  ASSERT_EQUALS(expected,
        unwrapSequence({
              990,
              0,
              10,
        }));
}

unittest("time_unwrapper: rollover on second iteration") {
  auto expected = vector<int64_t>({
        990,
        1000,
        1010,
        1500,
        1990,
        2000,
        2010,
  });
  ASSERT_EQUALS(expected,
        unwrapSequence({
              990,
              0,
              10,
              500,
              990,
              0,
              10,
        }));
}

unittest("time_unwrapper: non-monotonic passthrough") {
  auto expected = vector<int64_t>({0, 10, 30, 20, 40});
  ASSERT_EQUALS(expected, unwrapSequence({0, 10, 30, 20, 40}));
}

unittest("time_unwrapper: non-monotonic rollover") {
  auto expected = vector<int64_t>({
        950,
        949,
        975,
        974,
        1000,
        999,
        1025,
        1024,
  });
  ASSERT_EQUALS(expected,
        unwrapSequence({
              950,
              949,
              975,
              974,
              0,
              999,
              25,
              24,
        }));
}
