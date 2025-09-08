#include <cassert>
#include <map>
#include <stdexcept>
#include <string>

using namespace std;

// usage example:
// map<string, string> vars;
// vars["Name"] = "john";
// assert("hello john" == expandVars("hello $Name$", vars));
string expandVars(string input, map<string, string> const &values) {
  int i = 0;
  auto front = [&]() { return input[i]; };
  auto pop = [&]() { return input[i++]; };
  auto empty = [&]() { return i >= (int)input.size(); };

  auto parseVarName = [&]() -> string {
    auto leadingDollar = pop();
    (void)leadingDollar;
    assert(leadingDollar == '$');

    string name;
    while(!empty() && front() != '$') {
      name += pop();
    }

    if(empty())
      throw runtime_error("unexpected end of string found when parsing variable name");

    pop(); // pop terminating '$'
    return name;
  };

  string r;

  while(!empty()) {
    auto const head = front();
    if(head == '$') {
      auto name = parseVarName();
      if(values.find(name) == values.end())
        throw runtime_error("unknown variable name '" + name + "'");

      r += values.at(name);
    } else {
      r += pop();
    }
  }

  return r;
}
