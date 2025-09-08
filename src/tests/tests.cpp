#include "tests.hpp"

#include <algorithm> // sort
#include <climits>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using TestFunction = void (*)();

struct UnitTest {
  void (*fn)();
  std::string name;

  // To be "FIRST" class, a test must be:
  //
  // * Fast: the total time of the unit tests must be less than 30s,
  //         otherwise developers will run them less often.
  //
  // * Isolated: the outcome of the test must only depend on the behavior of a
  //             single module.
  //
  // * Repeatable: tests must be deterministic, and must not depend on the
  //               environment (e.g must not interact with the outside world).
  //
  // * Self-Validating: the test result must be "Yes" or "No". It must not be
  //                    "Maybe", or some number that must be manually checked.
  //
  // * Timely: tests must look like they were written *before* the production
  //           code: this way, the API of the production code is designed
  //           from the point of view of the user of the API, not the implementer.
  //
  int type; // 0:first class, 1:second class, 2:fuzztest

  // for sorting
  std::string file;
  int line;
};

std::vector<UnitTest> &allTests() {
  static std::vector<UnitTest> all;
  return all;
}

struct Filter {
  int minIdx = 0;
  int maxIdx = INT_MAX;
  bool noSecondClass = true;
  int fuzzIdx = -1;
  std::string fuzzPath;
};

void listAll() {
  int i = 0;
  for(auto &test : allTests())
    std::cout << "Test #" << i++ << ": " << test.name << std::endl;
}

bool startsWith(std::string s, std::string prefix) { return s.substr(0, prefix.size()) == prefix; }

bool matches(Filter filter, int idx) {
  if(idx < filter.minIdx)
    return false;
  if(idx > filter.maxIdx)
    return false;
  if(allTests()[idx].type == 2)
    return false;
  if(filter.noSecondClass && allTests()[idx].type == 1)
    return false;
  if(startsWith(allTests()[idx].name, "[DISABLED]"))
    return false;
  return true;
}

static uint8_t fuzzBuffer[1024];

void RunAll(Filter filter) {
  if(filter.fuzzIdx >= 0) {
    std::ifstream fp(filter.fuzzPath);
    fp.read((char *)fuzzBuffer, sizeof fuzzBuffer);
    allTests()[filter.fuzzIdx].fn();
  } else {
    auto const nTests = allTests().size(); // Apple clang version 16.0.0 error if placed in the loop
    for(int i = 0; i < (int)nTests; ++i) {
      if(matches(filter, i)) {
        std::cout << "#" << i << ": " << allTests()[i].name << std::endl;
        allTests()[i].fn();
      }
    }
  }
}

void SortTests() {
  auto byName = [](UnitTest const &a, UnitTest const &b) -> bool {
    if(a.file != b.file)
      return a.file < b.file;
    return a.line < b.line;
  };
  std::sort(allTests().begin(), allTests().end(), byName);
}
}

namespace Tests {

void Fail(char const *file, int line, const char *msg) {
  std::cerr << "TEST FAILED: " << file << "(" << line << "): " << msg << std::endl;
  std::raise(SIGABRT);
}

void FailAssertEquals(char const *file, int line, std::string caption, std::string expected, std::string actual) {

  bool multiline = expected.size() > 40 || actual.size() > 40;

  std::string msg;
  msg += "assertion failed for expression: '" + caption + "'. ";
  if(multiline)
    msg += "\n";
  msg += "expected '" + expected + "' ";
  if(multiline)
    msg += "\n     ";
  msg += "got '" + actual + "'";
  Fail(file, line, msg.c_str());
}

int RegisterTest(void (*fn)(), const char *testName, int type, const char *filename, int line) {
  UnitTest test{};
  test.fn = fn;
  test.name = testName;
  test.type = type;
  test.file = filename;
  test.line = line;
  allTests().push_back(test);
  return 0;
}

void GetFuzzTestData(uint8_t const *&ptr, size_t &len) {
  ptr = fuzzBuffer;
  len = sizeof fuzzBuffer;
}
}

int main(int argc, const char *argv[]) {
  int i = 1;
  auto popWord = [&]() -> std::string {
    if(i >= argc)
      throw std::runtime_error("unexpected end of command line");
    return argv[i++];
  };

  SortTests();

  Filter filter;

  while(i < argc) {
    auto const word = popWord();

    if(word == "--list" || word == "-l") {
      listAll();
      return 0;
    } else if(word == "--only") {
      auto idx = atoi(popWord().c_str());
      filter.minIdx = idx;
      filter.maxIdx = idx;
      filter.noSecondClass = false;
    } else if(word == "--range") {
      filter.minIdx = atoi(popWord().c_str());
      filter.maxIdx = atoi(popWord().c_str());
    } else if(word == "--second-class") {
      filter.noSecondClass = false;
    } else if(word == "--fuzz") {
      filter.fuzzIdx = atoi(popWord().c_str());
      filter.fuzzPath = popWord().c_str();
    }
  }

  RunAll(filter);
  return 0;
}
