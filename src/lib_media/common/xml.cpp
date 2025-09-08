#include "xml.hpp"

#include <cassert>
#include <cstdarg> // va_list
#include <cstdio> // vsnprintf
#include <stdexcept>

namespace {
std::string escapeXmlEntities(std::string const &s) {
  std::string r;

  for(auto cIt = s.cbegin(); cIt != s.cend(); cIt++) {
    auto peek = [&](std::string str) -> bool {
      for(auto it = cIt + 1, strIt = str.cbegin(); strIt != str.cend(); ++it, ++strIt) {
        if(it == s.cend())
          /*remaning is shorter than str*/
          return false;

        if(*it != *strIt)
          return false;
      }

      return true;
    };

    switch(*cIt) {
    case '"':
      r += "&quot;";
      break;
    case '\'':
      r += "&apos;";
      break;
    case '<':
      r += "&lt;";
      break;
    case '>':
      r += "&gt;";
      break;
    case '&':
      if(peek("quot;")) {
        r += "&quot;";
        cIt += 5;
      } else if(peek("apos;")) {
        r += "&apos;";
        cIt += 5;
      } else if(peek("lt;")) {
        r += "&lt;";
        cIt += 3;
      } else if(peek("gt;")) {
        r += "&gt;";
        cIt += 3;
      } else if(peek("amp;")) {
        r += "&amp;";
        cIt += 4;
      } else
        r += "&amp;";
      break;
    default:
      r += *cIt;
      break;
    }
  }

  return r;
}

template<typename EmitType>
void serializeTag(Tag const &tag, bool prettify, int depth, EmitType emit) {
  if(tag.name.empty())
    throw std::runtime_error("tag name can't be empty");

  if(prettify)
    emit("%*s", depth * 2, "");

  emit("<%s", tag.name.c_str());

  for(auto &a : tag.attr)
    emit(" %s=\"%s\"", a.name.c_str(), a.value.c_str());

  if(tag.name[0] == '?')
    emit("?>");
  else if(tag.children.empty() && tag.content.empty()) {
    emit("/>");
  } else {
    emit(">");

    if(tag.children.empty()) {
      emit("%s", escapeXmlEntities(tag.content).c_str());
    } else {
      if(prettify)
        emit("\n");

      for(auto &child : tag.children)
        serializeTag(child, prettify, depth + 1, emit);

      if(prettify)
        emit("%*s", depth * 2, "");
    }

    if(tag.name[0] != '?')
      emit("</%s>", tag.name.c_str());
  }

  if(prettify)
    emit("\n");
}
}

std::string serializeXml(Tag const &tag, bool prettify) {
  std::string r;

  auto emit = [&](const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer, sizeof buffer, format, args);
    (void)n;
    assert(n < int(sizeof buffer));
    va_end(args);

    r += buffer;
  };

  serializeTag(tag, prettify, 0, emit);

  return r;
}
