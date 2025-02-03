#include "socket.hpp"
#include <stdexcept>

#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h> // strerror
#include <stdio.h>

using namespace std;

// increasing this value increases throughput
// For a single 20Mbps output, 1Mb is too low.
// 2Mb: can handle 4 redundant 20Mbps outputs.
static auto const SEND_BUFFER_SIZE = 2 * 1024 * 1024;

namespace {
struct Socket : IOutputSocket {
	Socket(const char* address, int port) {
		m_socket = socket(AF_INET, SOCK_DGRAM, 0);

		if(m_socket < 0)
			throw runtime_error("socket failed");

		m_dstAddr.sin_family = AF_INET;
		m_dstAddr.sin_addr.s_addr = inet_addr(address);
		m_dstAddr.sin_port = htons(port);

		unsigned int size = SEND_BUFFER_SIZE;
		int err = setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF,  &size, sizeof(size));
		if (err != 0)
			throw runtime_error("setsockopt failed");

		socklen_t optlen = sizeof(size);
		err = getsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &size, &optlen);
		if (err != 0)
			throw runtime_error("getsockopt failed");

		if(size < SEND_BUFFER_SIZE) {
			char msg[256];
			sprintf(msg, "UDP send buffer is too small: %.2f kbytes", size/1024.0);
			throw runtime_error(msg);
		}
	}

	~Socket() {
		close(m_socket);
	}

	void send(const uint8_t* buffer, size_t len) override {
		if(::sendto(m_socket, buffer, len, 0, (sockaddr*)&m_dstAddr, sizeof(m_dstAddr)) < 0) {
			char msg[256];
			sprintf(msg, "UDP send %d bytes failed: %s", (int)len, strerror(errno));
			throw runtime_error(msg);
		}
	}

	int m_socket = -1;
	sockaddr_in m_dstAddr {};
};
}

std::unique_ptr<IOutputSocket> createOutputSocket(const char* address, int port) {
	return make_unique<Socket>(address, port);
}

