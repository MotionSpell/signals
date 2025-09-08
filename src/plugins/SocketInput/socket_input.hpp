#pragma once

struct SocketInputConfig {
  int ipAddr[4]{};
  int port = 0;
  bool isTcp = false;
  bool isMulticast = false;
};
