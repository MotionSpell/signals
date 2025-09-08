#include "lib_utils/json.hpp"

#include "tests/tests.hpp"

#include <vector>

using namespace std;

namespace {
bool jsonOk(string text) {
  try {
    json::parse(text);
    return true;
  } catch(exception const &) {
    return false;
  }
}
}

unittest("Json parser: empty") {
  ASSERT(jsonOk("{}"));
  ASSERT(!jsonOk("{"));
}

unittest("Json parser: objectValue") {
  ASSERT(jsonOk("{ \"var\": 0 }"));
  ASSERT(jsonOk("{ \"var\": -10 }"));
  ASSERT(jsonOk("{ \"hello\": \"world\" }"));
  ASSERT(jsonOk("{ \"N1\": \"V1\", \"N2\": \"V2\" }"));

  ASSERT(!jsonOk("{ \"N1\" : : \"V2\" }"));
}

unittest("Json parser: booleans") {
  ASSERT(jsonOk("{ \"var\": true }"));
  ASSERT(jsonOk("{ \"var\": false }"));

  {
    auto o = json::parse("{ \"isCool\" : true }");
    auto s = o["isCool"];
    ASSERT_EQUALS((int)json::Value::Type::Boolean, (int)s.type);
    ASSERT_EQUALS(true, s.boolValue);
  }

  {
    auto o = json::parse("{ \"isSlow\" : false }");
    auto s = o["isSlow"];
    ASSERT_EQUALS((int)json::Value::Type::Boolean, (int)s.type);
    ASSERT_EQUALS(false, s.boolValue);
  }
}

unittest("Json parser: non-zero terminated") { ASSERT(!jsonOk("{ \"isCool\" : true } _invalid_json_token_")); }

unittest("Json parser: arrays") {
  ASSERT(jsonOk("{ \"A\": [] }"));
  ASSERT(jsonOk("{ \"A\": [ { }, { } ] }"));
  ASSERT(jsonOk("{ \"A\": [ \"hello\", \"world\" ] }"));

  ASSERT(!jsonOk("{ \"A\": [ }"));
  ASSERT(!jsonOk("{ \"A\": ] }"));
}

unittest("Json parser: returned value") {
  {
    auto o = json::parse("{}");
    ASSERT_EQUALS(0u, o.objectValue.size());
  }
  {
    auto o = json::parse("{ \"N\" : \"hello\"}");
    ASSERT_EQUALS(1u, o.objectValue.size());
    auto s = o.objectValue["N"];
    ASSERT_EQUALS("hello", s.stringValue);
  }
  {
    auto o = json::parse("{ \"N\" : -1234 }");
    ASSERT_EQUALS(1u, o.objectValue.size());
    auto s = o.objectValue["N"];
    ASSERT_EQUALS((int)json::Value::Type::Integer, (int)s.type);
    ASSERT_EQUALS(-1234, s.intValue);
  }
}
