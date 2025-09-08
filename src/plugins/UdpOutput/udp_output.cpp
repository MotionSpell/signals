#include "udp_output.hpp"

#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/tools.hpp" // enforce

#include "socket.hpp"

using namespace Modules;

namespace {

struct UdpOutput : ModuleS {
  UdpOutput(KHost *host, UdpOutputConfig const &config)
      : m_host(host) {
    char buffer[256];
    sprintf(buffer, "%d.%d.%d.%d", config.ipAddr[0], config.ipAddr[1], config.ipAddr[2], config.ipAddr[3]);
    m_socket = createOutputSocket(buffer, config.port);
  }

  void processOne(Data data) override { m_socket->send(data->data().ptr, data->data().len); }

  KHost *const m_host;
  std::unique_ptr<IOutputSocket> m_socket;
};

IModule *createObject(KHost *host, void *va) {
  auto config = (UdpOutputConfig *)va;
  enforce(host, "UdpOutput: host can't be NULL");
  enforce(config, "UdpOutput: config can't be NULL");
  return createModule<UdpOutput>(host, *config).release();
}

auto const registered = Factory::registerModule("UdpOutput", &createObject);

}
