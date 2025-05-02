#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

struct ISocket {
	virtual ~ISocket() = default;
	virtual void joinMulticastGroup(const char* address, int port) = 0;
	virtual size_t receive(uint8_t* dst, size_t len) = 0; // non-blocking
};

std::unique_ptr<ISocket> createSocket();

