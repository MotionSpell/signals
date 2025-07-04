#pragma once

#include <string>
#include <vector>
#include "../common/http_sender.hpp"

struct HttpOutputConfig {
	struct Flags {
		bool InitialEmptyPost = true;
		HttpRequest request = POST;
	};

	std::string url;
	std::string userAgent = "GPAC Signals/";
	std::vector<std::string> headers {};

	// optional: data to transfer just before closing the connection
	std::vector<uint8_t> endOfSessionSuffix {};

	Flags flags {};
};
