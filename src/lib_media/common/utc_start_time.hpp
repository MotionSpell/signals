#pragma once

#include <cstdint>

struct IUtcStartTimeQuery {
	virtual uint64_t query() const = 0;
};

struct NullStartTime : IUtcStartTimeQuery {
	uint64_t query() const override {
		return 0;
	}
};

static NullStartTime g_NullStartTime;

