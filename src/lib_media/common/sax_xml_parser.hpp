#pragma once

#include "span.hpp"
#include <functional>
#include <map>
#include <string>

typedef void NodeStartFunc(std::string /*id*/, std::map<std::string, std::string>& /*attributes*/);
typedef void NodeEndFunc(std::string /*id*/, std::string /*content*/);

void saxParse(span<const char> input, std::function<NodeStartFunc> onNodeStart, std::function<NodeEndFunc> onNodeEnd);

