#pragma once

#include <cstddef>
#include <cstdint> //uint8_t
#include <memory>

struct ISocket {
	enum Type {
		UDP,
		UDP_MULTICAST,
		TCP
	};

	virtual ~ISocket() = default;
	virtual size_t receive(uint8_t* dst, size_t len) = 0; // non-blocking
};

std::unique_ptr<ISocket> createSocket(const char* address, int port, ISocket::Type type);

