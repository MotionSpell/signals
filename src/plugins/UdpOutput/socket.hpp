#pragma once

#include <cstddef>
#include <memory>

struct IOutputSocket {
	virtual ~IOutputSocket() = default;
	virtual void send(uint8_t const* data, size_t len) = 0;
};

std::unique_ptr<IOutputSocket> createOutputSocket(const char* address, int port);

