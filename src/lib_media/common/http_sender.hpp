#pragma once

#include "lib_modules/core/module.hpp" // KHost
#include "lib_modules/utils/helper.hpp" // span

// Single long running POST/PUT connection
//|This interface is meant to be used the following way:
//|
//|  {
//|    auto s = createHttpSender(...);
//|    s->appendPrefix(prefix_part1); // sent once per (re)connection
//|    s->appendPrefix(prefix_part2); // append
//|    s->send(data1);
//|    s->send(data2);
//|    s->send({}); // flush (this line is optional, but guarantees that all data will be transfered)
//|    // 'send' cannot be called anymore after flushing.
//|  } // here the connection gets destroyed (whether all data was transfered or not)
//|
struct HttpSender {
	virtual ~HttpSender() = default;
	virtual void send(span<const uint8_t> data) = 0; // (send an empty span to flush)
	virtual void appendPrefix(span<const uint8_t> prefix) = 0; // may be called multiple times
};

#include <string>
#include <vector>

enum HttpRequest {
	POST,
	PUT,
	DELETEX,
};

// make an empty POST (used to check the end point exists)
void enforceConnection(std::string url, HttpRequest request);

struct HttpSenderConfig {
	std::string url;
	std::string userAgent;
	HttpRequest request = POST;
	std::vector<std::string> extraHeaders;
};

std::unique_ptr<HttpSender> createHttpSender(HttpSenderConfig const& config, Modules::KHost* log);

