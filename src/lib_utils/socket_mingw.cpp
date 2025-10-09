#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "format.hpp"
#include "log.hpp"
#include "socket.hpp"

using namespace std;

struct Socket : ISocket {
  Socket(const char *ipAddr, int port, ISocket::Type type)
      : m_type(type) {
    WSADATA wsaData;
    auto res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if(res)
      throw runtime_error("WSAStartup failed");

    m_socket = socket(AF_INET, type == TCP ? SOCK_STREAM : SOCK_DGRAM, 0);

    if(m_socket == INVALID_SOCKET)
      throw runtime_error("socket failed");

    if(type != TCP) {
      // share port number
      int one = 1;

      if(setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof one) < 0)
        throw runtime_error("setsockopt failed");
    }

    // set non-blocking
    unsigned long Yes = TRUE;
    ioctlsocket(m_socket, FIONBIO, &Yes);

    sockaddr_in dstAddr{};
    dstAddr.sin_family = AF_INET;
    dstAddr.sin_addr.s_addr = INADDR_ANY;
    dstAddr.sin_port = htons(port);

    if(::bind(m_socket, (sockaddr *)&dstAddr, sizeof(dstAddr)) == SOCKET_ERROR)
      throw runtime_error("bind failed");

    switch(type) {
    case UDP_MULTICAST:
      joinMulticastGroup(ipAddr);
      break;
    case TCP: {
      if(listen(m_socket, 5) < 0)
        throw runtime_error("client socket 'listen' failed");

      ensureAccept();
      break;
    }
    case UDP:
    default:
      break;
    }
  }

  ~Socket() {
    closesocket(m_socket);
    closesocket(m_socket_client);
    WSACleanup();
  }

  size_t receive(uint8_t *buffer, size_t dstlen) {
    if(!ensureAccept())
      return {}; // when needed, connection not established

    sockaddr_in addr;
    auto addrSize = (int)sizeof(addr);

    auto len = recvfrom(m_socket, (char *)buffer, dstlen,
          0, // TODO: MSG_DONTWAIT
          (sockaddr *)&addr, &addrSize);

    if(len == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
      return {}; // no data available yet

    if(len < 0)
      throw runtime_error("recvfrom failed");

    return len;
  }

  private:
  void joinMulticastGroup(const char *ipAddr) {
    // send IGMP join request
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(ipAddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if(setsockopt(m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0)
      throw runtime_error("can't join multicast address");

    uint32_t socketBufferSize = {0x60000};
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, (char *)&socketBufferSize, (socklen_t)sizeof(socketBufferSize));

    uint32_t nsize = 0;
    int psize = (uint32_t)sizeof(socketBufferSize);
    getsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, (char *)&nsize, &psize);

    if(nsize < socketBufferSize)
      g_Log->log(Error,
            format("Asked for a %s bytes socket buffer size but was allocated only %s", socketBufferSize, nsize)
                  .c_str());
  }

  bool ensureAccept() {
    if(m_type != TCP)
      return true;

    if(!(m_socket_client == INVALID_SOCKET))
      return true;

    sockaddr_in clientAddr{};
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = inet_addr("0.0.0.0");
    clientAddr.sin_port = htons(0);
    auto const clientAddrLen = sizeof(clientAddr);

    m_socket_client = accept(m_socket, (sockaddr *)&clientAddr, (socklen_t *)&clientAddrLen);

    if(m_socket_client == INVALID_SOCKET)
      return false; // client socket 'accept' failed

    return true;
  }

  SOCKET m_socket = INVALID_SOCKET;
  SOCKET m_socket_client = INVALID_SOCKET;
  ISocket::Type m_type;
};

std::unique_ptr<ISocket> createSocket(const char *ipAddr, int port, ISocket::Type type) {
  return make_unique<Socket>(ipAddr, port, type);
}
