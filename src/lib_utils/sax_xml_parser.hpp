#pragma once

#include <functional>
#include <string>

#include "small_map.hpp"
#include "span.hpp"

typedef void NodeStartFunc(std::string /*id*/, SmallMap<std::string, std::string> & /*attributes*/);
typedef void NodeEndFunc(std::string /*id*/, std::string /*content*/);

void saxParse(span<const char> input, std::function<NodeStartFunc> onNodeStart, std::function<NodeEndFunc> onNodeEnd);
