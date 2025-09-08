#include "sax_xml_parser.hpp"

#include <cassert>
#include <stdexcept>

void saxParse(span<const char> input, std::function<NodeStartFunc> onNodeStart, std::function<NodeEndFunc> onNodeEnd) {
  using namespace std;

  std::string content;
  bool voidContent = false;

  auto front = [&]() -> int {
    if(input.len == 0)
      throw runtime_error("Unexpected end of file");

    return input[0];
  };

  auto accept = [&](char c) {
    if(c != front())
      return false;

    input += 1;
    return true;
  };

  auto skipSpaces = [&]() {
    while(isspace(front()))
      input += 1;
  };

  auto parseString = [&]() {
    string r;
    accept('"');
    while(front() != '"') {
      r += front();
      input += 1;
    }
    accept('"');
    return r;
  };

  auto parseIdentifier = [&]() {
    string r;
    skipSpaces();
    while(isalnum(front()) || front() == ':' || front() == '_' || front() == '-') {
      r += front();
      input += 1;
    }
    return r;
  };

  auto parseNextTag = [&]() {
    while(input.len && front() != '<' && front() != '>' && front() != '/')
      input += 1;

    if(input.len == 0)
      return;

    if(accept('<')) {
      if(accept('?')) {
        // XML stuff
      } else if(accept('!')) {
        // comment
      } else if(accept('/')) {
        // closing tag
        auto id = parseIdentifier();
        onNodeEnd(id, content);
        content.clear();
        voidContent = true;
      } else {
        // opening tag
        content.clear();
        voidContent = false;
        auto id = parseIdentifier();
        skipSpaces();

        map<string, string> attr;

        while(front() != '>' && front() != '/' && front() != -1) {
          auto name = parseIdentifier();
          if(name.empty()) {
            string msg = "expected an XML attribute, got '";
            msg += front();
            msg += "'";
            throw runtime_error(msg);
          }
          skipSpaces();
          if(accept('='))
            attr[name] = parseString();
          skipSpaces();
        }

        onNodeStart(id, attr);
      }
    } else if(accept('>')) {
      // content
      while(input.len && front() != '<') {
        if(!voidContent)
          content += front();

        input += 1;
      }
    } else if(accept('/')) {
      if(accept('>')) {
        // self closing tag
        auto id = parseIdentifier();
        assert(content == "");
        onNodeEnd(id, content);
      }
    } else
      assert(0); // char is dispatched but not processed: this is likely a programming error
  };

  while(input.len)
    parseNextTag();
}
