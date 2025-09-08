#include "tests/tests.hpp"

#include <map>
std::string expandVars(std::string input, std::map<std::string, std::string> const &values);

using namespace std;

unittest("expandVars: passthrough") {
  map<string, string> empty;
  ASSERT_EQUALS("hello", expandVars("hello", empty));
}

unittest("expandVars: simple") {
  map<string, string> vars;
  vars["who"] = "world";
  ASSERT_EQUALS("hello world", expandVars("hello $who$", vars));
}

unittest("expandVars: many") {
  map<string, string> vars;
  vars["who1"] = "John";
  vars["who2"] = "Doe";
  vars["when"] = "today";
  vars["what"] = "happy";
  ASSERT_EQUALS("JohnDoe is happy today", expandVars("$who1$$who2$ is $what$ $when$", vars));
}

unittest("expandVars: unexisting") {
  map<string, string> vars;
  vars["who"] = "world";
  ASSERT_THROWN(expandVars("hello $what$", vars));
}

unittest("expandVars: incomplete") {
  map<string, string> vars;
  vars["my_very_long_variable_name"] = "world";
  ASSERT_THROWN(expandVars("hello $my_very_long_vari", vars));
}

///////////////////////////////////////////////////////////////////////////////

int64_t parseIso8601Period(std::string input);

unittest("iso8601 parser: invalid") { ASSERT_THROWN(parseIso8601Period("I_AM_INVALID")); }

unittest("iso8601 parser: invalid, another") { ASSERT_THROWN(parseIso8601Period("PT7X")); }

unittest("iso8601 parser: zero") { ASSERT_EQUALS(0, parseIso8601Period("PT0S")); }

unittest("iso8601 parser: one minute") { ASSERT_EQUALS(60, parseIso8601Period("PT1M")); }

unittest("iso8601 parser: half an hour and one second") { ASSERT_EQUALS(1801, parseIso8601Period("PT0.5HM1S")); }

unittest("iso8601 parser: year only") { ASSERT_EQUALS(365 * 100 * 24 * 3600LL, parseIso8601Period("P100Y")); }

unittest("iso8601 parser: all") { ASSERT_EQUALS(48551, parseIso8601Period("PT13H29M11S")); }
