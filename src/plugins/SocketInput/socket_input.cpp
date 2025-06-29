#include "socket_input.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/os.hpp" // setHighThreadPriority
#include "lib_utils/tools.hpp" // enforce
#include "lib_utils/socket.hpp"
#include <thread>

using namespace Modules;
using namespace std;

namespace {

struct SocketInput : Module {
	SocketInput(KHost* host, SocketInputConfig const& config)
		: m_host(host) {
		char buffer[256];
		sprintf(buffer, "%d.%d.%d.%d", config.ipAddr[0], config.ipAddr[1], config.ipAddr[2], config.ipAddr[3]);
		auto type = config.isTcp ? ISocket::TCP : ISocket::UDP;
		type = config.isMulticast ? ISocket::UDP_MULTICAST : type;
		m_socket = createSocket(buffer, config.port, type);

		m_highPriority = !config.isTcp;
		m_output = addOutput();
		m_host->activate(true);
	}

	// must be able to receive at least 35Mbps
	void process() override {
		if(m_highPriority == 1) {
			if (!setHighThreadPriority())
				m_host->log(Warning, "Couldn't change reception thread priority to realtime.");

			m_highPriority = 2;
		}

		auto const bufSize = 0x60000 / 188 * 188;
		auto buf = m_output->allocData<DataRawResizable>(bufSize);
		auto dst = buf->buffer->data();

		auto size = m_socket->receive(dst.ptr, dst.len);
		if(size > 0) {
			buf->resize(size);
			m_output->post(buf);
		} else
			std::this_thread::sleep_for(1ms);
	}

	KHost* const m_host;
	std::unique_ptr<ISocket> m_socket;
	OutputDefault* m_output;
	int m_highPriority = 0;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (SocketInputConfig*)va;
	enforce(host, "SocketInput: host can't be NULL");
	enforce(config, "SocketInput: config can't be NULL");
	return createModuleWithSize<SocketInput>(10000, host, *config).release();
}

auto const registered = Factory::registerModule("SocketInput", &createObject);

}
