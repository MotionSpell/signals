// XML generator from a tree
#pragma once

///////////////////////////////////////////////////////////////////////////////
// XML tree definition
#include <string>
#include <vector>

struct Attribute {
  std::string name, value;
};

struct Tag {
  std::string name;

  std::vector<Attribute> attr{};
  std::vector<Tag> children{};
  std::string content{};

  std::string &operator[](const char *attributeName) {
    for(auto &a : attr) {
      if(attributeName == a.name)
        return a.value;
    }

    attr.push_back({attributeName, ""});
    return attr.back().value;
  }

  void add(Tag const &other) { children.push_back(other); }
};

///////////////////////////////////////////////////////////////////////////////
// XML tree serialization

std::string serializeXml(Tag const &tag, bool prettify = true);
