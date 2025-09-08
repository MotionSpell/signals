#pragma once

#include <string>
#include <vector>

struct HttpSinkConfig {
  std::string baseURL;
  std::string userAgent;
  std::vector<std::string> headers;
};
