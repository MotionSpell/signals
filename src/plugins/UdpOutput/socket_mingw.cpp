#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "socket.hpp"

using namespace std;

namespace {
struct Socket : IOutputSocket {
  Socket(const char *address, int port) {
    WSADATA wsaData;
    auto res = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if(res)
      throw runtime_error("WSAStartup failed");

    m_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if(m_socket == INVALID_SOCKET)
      throw runtime_error("socket failed");

    m_dstAddr.sin_family = AF_INET;
    m_dstAddr.sin_addr.s_addr = inet_addr(address);
    m_dstAddr.sin_port = htons(port);
  }

  ~Socket() {
    closesocket(m_socket);
    WSACleanup();
  }

  void send(const uint8_t *buffer, size_t len) override {
    if(::sendto(m_socket, (const char *)buffer, len, 0, (sockaddr *)&m_dstAddr, sizeof(m_dstAddr)) < 0)
      throw runtime_error("UDP send failed");
  }

  SOCKET m_socket = INVALID_SOCKET;
  sockaddr_in m_dstAddr{};
};
}

std::unique_ptr<IOutputSocket> createOutputSocket(const char *address, int port) {
  return make_unique<Socket>(address, port);
}
