#pragma once

#include <sstream>
#include <stdint.h>
#include <vector>

// generate a file-unique identifier, based on current line
#define unittestSuffix(suffix, prettyName, type)                                                                       \
  static void testFunction##suffix();                                                                                  \
  static int g_isRegistered##suffix =                                                                                  \
        Tests::RegisterTest(&testFunction##suffix, prettyName, type, __FILE__, __LINE__);                              \
  static void testFunction##suffix()

#define unittestLine(counter, prettyName, type) unittestSuffix(counter, prettyName, type)

#define unittest(prettyName) unittestLine(__COUNTER__, prettyName, 0)

#define secondclasstest(prettyName) unittestLine(__COUNTER__, prettyName, 1)

#define fuzztest(prettyName) unittestLine(__COUNTER__, prettyName, 2)

namespace Tests {
template<typename T>
std::string ToString(T const &val);
}

// TODO: find a way not to explicitly depend on 'vector'.
template<typename T>
inline std::ostream &operator<<(std::ostream &o, std::vector<T> iterable) {
  o << "[";
  bool first = true;
  for(auto &val : iterable) {
    if(!first)
      o << ", ";
    o << Tests::ToString(val);
    first = false;
  }
  o << "]";
  return o;
}

namespace Tests {

void Fail(char const *file, int line, const char *msg);
void FailAssertEquals(char const *file, int line, std::string caption, std::string expected, std::string actual);
void GetFuzzTestData(uint8_t const *&ptr, size_t &len);

template<typename T>
std::string ToString(T const &val) {
  std::stringstream ss;
  ss << val;
  return ss.str();
}

template<>
inline std::string ToString<uint8_t>(uint8_t const &val) {
  std::stringstream ss;
  ss << (int)val;
  return ss.str();
}

template<typename T>
inline void Assert(char const *file, int line, const char *caption, T const &result) {
  if(!result) {
    std::string msg;
    msg += "assertion failed: ";
    msg += caption;
    ::Tests::Fail(file, line, msg.c_str());
  }
}

template<typename T, typename U>
inline void AssertEquals(char const *file, int line, const char *caption, T const &expected, U const &actual) {
  if(expected != actual) {
    auto sExpected = ToString(expected);
    auto sActual = ToString(actual);
    FailAssertEquals(file, line, caption, sExpected, sActual);
  }
}

#define ASSERT(expr) ::Tests::Assert(__FILE__, __LINE__, #expr, expr)

#define ASSERT_EQUALS(expected, actual) ::Tests::AssertEquals(__FILE__, __LINE__, #actual, expected, actual)

#define ASSERT_THROWN(expr)                                                                                            \
  try {                                                                                                                \
    expr;                                                                                                              \
    ::Tests::Fail(__FILE__, __LINE__, "Missing exception: " #expr);                                                    \
  } catch(...) {}

int RegisterTest(void (*f)(), const char *testName, int type, const char *filename, int line);

}
