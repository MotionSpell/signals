#pragma once

#include "span.hpp"
#include <functional>
#include <memory>
#include <cstdint>

namespace Modules {
namespace In {

struct IFilePuller {
	virtual ~IFilePuller() = default;
	virtual void wget(const char* url, std::function<void(SpanC)> callback) = 0;
	virtual void askToExit() = 0;
};

struct IFilePullerFactory {
	virtual ~IFilePullerFactory() = default;
	virtual std::unique_ptr<IFilePuller> create() = 0;
};

}
}

#include <vector>

namespace Modules {
namespace In {

inline std::vector<uint8_t> download(Modules::In::IFilePuller* puller, const char* url) {
	std::vector<uint8_t> r;
	auto onBuffer = [&](SpanC buf) {
		r.insert(r.end(), buf.ptr, buf.ptr + buf.len);
	};
	puller->wget(url, onBuffer);
	return r;
}

}
}

