#pragma once

#include "span.hpp"

namespace Modules {

struct IBuffer {
	virtual ~IBuffer() = default;
	virtual Span data() = 0;
	virtual SpanC data() const = 0;
};

}
