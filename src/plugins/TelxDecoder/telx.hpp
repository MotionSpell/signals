// Parse raw teletext data, and produce 'Page' objects
#pragma once

#include "span.hpp"
#include "lib_media/common/subtitle.hpp"
#include <vector>

namespace Modules {
struct KHost;
}

struct ITeletextParser {
	virtual ~ITeletextParser() = default;
	virtual std::vector<Modules::Page> parse(SpanC data, int64_t time) = 0;
};

ITeletextParser* createTeletextParser(Modules::KHost* host, int pageNum);
