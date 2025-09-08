#include "lib_utils/fraction.hpp"

#include "tests/tests.hpp"

using namespace Tests;

unittest("Fraction: comparison, natural numbers") {
  ASSERT(Fraction(1, 10) <= 100);
  ASSERT(Fraction(100, 2) > 25);
  ASSERT(Fraction(31, 10) > 3);
}

unittest("Fraction: comparison, relative numbers") {
  ASSERT(Fraction(-1, 1) <= 100);
  ASSERT(Fraction(1, -1) <= 100);
}
