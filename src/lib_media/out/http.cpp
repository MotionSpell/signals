#include "http.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_utils/format.hpp"
#include "../common/http_sender.hpp"

using namespace Modules;

namespace {
bool startsWith(std::string s, std::string prefix) {
	return !s.compare(0, prefix.size(), prefix);
}
}

namespace Modules {
namespace Out {

HTTP::HTTP(KHost* host, HttpOutputConfig const& cfg)
	: m_host(host), m_suffixData(cfg.endOfSessionSuffix) {
	if (!startsWith(cfg.url, "http://") && !startsWith(cfg.url, "https://"))
		throw error(format("can only handle URLs starting with 'http://' or 'https://', not '%s'.", cfg.url));

	// before any other connection, make an empty POST to check the end point exists
	if (cfg.flags.InitialEmptyPost)
		enforceConnection(cfg.url, cfg.flags.UsePUT);

	// create pins
	outputFinished = addOutput<OutputDefault>();

	m_sender = createHttpSender({cfg.url, cfg.userAgent, cfg.flags.UsePUT, cfg.headers}, m_host);
}

HTTP::~HTTP() {
	m_sender->send({m_suffixData.data(), m_suffixData.size()});
}

void HTTP::flush() {
	m_sender->send({});

	auto out = outputFinished->getBuffer<DataRaw>(0);
	outputFinished->post(out);
}

void HTTP::processOne(Data data) {
	if(data)
		m_sender->send(data->data());
}

}
}

namespace {
IModule* createObject(KHost* host, void* va) {
	auto cfg = (HttpOutputConfig*)va;
	enforce(host, "HTTP: host can't be NULL");
	return createModule<Out::HTTP>(host, *cfg).release();
}

auto const registered = Factory::registerModule("HTTP", &createObject);
}

