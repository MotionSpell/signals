#include "lib_appcommon/options.hpp"

#include "tests/tests.hpp"

#define NELEMENTS(a) sizeof(a) / sizeof(*(a))

unittest("CmdLineOptions: no flags") {
  int a, b;
  CmdLineOptions opt;
  opt.add("a", "aaa", &a);
  opt.add("b", "bbb", &b);

  const char *argv[] = {"progname", "myfile.dat"};
  auto res = opt.parse(NELEMENTS(argv), argv);
  ASSERT_EQUALS(std::vector<std::string>({"myfile.dat"}), res);
}

unittest("CmdLineOptions: short names") {
  std::string s;
  int n = -1;
  CmdLineOptions opt;
  opt.add("i", "inputPath", &s);
  opt.add("n", "numCores", &n);

  const char *argv[] = {"progname", "-i", "hello", "-n", "34"};
  opt.parse(NELEMENTS(argv), argv);
  ASSERT_EQUALS("hello", s);
  ASSERT_EQUALS(34, n);
}

unittest("CmdLineOptions: long names") {
  std::string s;
  int n = -1;
  CmdLineOptions opt;
  opt.add("i", "inputPath", &s);
  opt.add("n", "numCores", &n);

  const char *argv[] = {"progname", "--inputPath", "/dev/null", "--numCores", "8"};
  opt.parse(NELEMENTS(argv), argv);
  ASSERT_EQUALS("/dev/null", s);
  ASSERT_EQUALS(8, n);
}

unittest("CmdLineOptions: vector and flag") {
  std::vector<int> values;
  bool keepGoing = false;
  CmdLineOptions opt;
  opt.add("v", "value", &values);
  opt.addFlag("k", "keepGoing", &keepGoing);

  const char *argv[] = {"cmd", "--value", "1", "--value", "3", "-v", "7", "-k"};
  opt.parse(NELEMENTS(argv), argv);
  ASSERT_EQUALS(std::vector<int>({1, 3, 7}), values);
  ASSERT_EQUALS(true, keepGoing);
}

unittest("CmdLineOptions: unknown option") {
  bool keepGoing = false;
  CmdLineOptions opt;
  opt.add("k", "keepGoing", &keepGoing);

  const char *argv[] = {"cmd", "--evil"};
  ASSERT_THROWN(opt.parse(NELEMENTS(argv), argv));
}

unittest("CmdLineOptions: unexpected end of command line") {
  std::string inputPath;
  CmdLineOptions opt;
  opt.add("i", "inputPath", &inputPath);

  const char *argv[] = {"progname", "-i"};
  ASSERT_THROWN(opt.parse(NELEMENTS(argv), argv));
}

struct MySize {
  int width, height;
};

static inline void parseValue(MySize &var, ArgQueue &args) {
  auto s = safePop(args);
  // fake parsing
  var.width = 128;
  var.height = 32;
}

unittest("CmdLineOptions: custom parsing") {
  MySize mySize{};
  CmdLineOptions opt;
  opt.add("s", "size", &mySize);

  const char *argv[] = {"progname", "-s", "128x32"};
  opt.parse(NELEMENTS(argv), argv);
  ASSERT_EQUALS(128, mySize.width);
}
