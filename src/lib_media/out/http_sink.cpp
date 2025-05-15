#include "http_sink.hpp"

#include "lib_modules/utils/helper.hpp" // ModuleS
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp" // safe_cast
#include "../common/metadata_file.hpp"
#include "http.hpp"
#include <string>
#include <memory>
#include <map>
#include <thread>
#include <atomic>
#include <vector>

using namespace std;

namespace {

template<typename T, typename V>
bool exists(T const& container, V const& val) {
	return container.find(val) != container.end();
}

struct HttpSink : Modules::ModuleS {
		HttpSink(Modules::KHost* host, string baseURL, string userAgent, const vector<string> &headers)
			: m_host(host),
			  baseURL(baseURL), userAgent(userAgent), headers(headers) {
			done = false;

			HttpOutputConfig httpConfig {};
			httpConfig.flags.InitialEmptyPost = true; // we want immediate failure if the URL is not reachable
			httpConfig.flags.request = POST;
			httpConfig.url = baseURL;
			httpConfig.userAgent = userAgent;
			httpConfig.headers = headers;
			auto http = Modules::createModule<Modules::Out::HTTP>(m_host, httpConfig);
		}
		~HttpSink() {
			done = true;
		}
		void processOne(Modules::Data data) override {
			auto const meta = safe_cast<const Modules::MetadataFile>(data->getMetadata());
			auto const url = baseURL + meta->filename;

			HttpOutputConfig httpConfig {};
			httpConfig.flags.InitialEmptyPost = false;
			httpConfig.flags.request = POST;
			httpConfig.url = url;
			httpConfig.userAgent = userAgent;
			httpConfig.headers = headers;

			if (meta->filesize == INT64_MAX) {
				m_host->log(Debug, format("Delete at URL: \"%s\"", url).c_str());
				httpConfig.flags.request = DELETEX;
				auto http = Modules::createModule<Modules::Out::HTTP>(m_host, httpConfig);
			} else if (meta->filesize == 0 && !meta->EOS) {
				if (exists(zeroSizeConnections, url))
					throw Modules::error(format("Received zero-sized metadata but transfer is already initialized for URL: \"%s\"", url));

				m_host->log(Debug, format("Initialize transfer for URL: \"%s\"", url).c_str());
				auto http = Modules::createModule<Modules::Out::HTTP>(m_host, httpConfig);
				http->getInput(0)->push(data);
				http->process();
				zeroSizeConnections[url] = std::move(http);
			} else {
				if (!exists(zeroSizeConnections, url)) {
					m_host->log(Debug, format("Starting transfer to URL: \"%s\"", url).c_str());
					zeroSizeConnections[url] = Modules::createModule<Modules::Out::HTTP>(m_host, httpConfig);
				}

				m_host->log(Debug, format("Continue transfer (%s bytes) for URL: \"%s\"", meta->filesize, url).c_str());
				if (meta->filesize) {
					zeroSizeConnections[url]->getInput(0)->push(data);
				}
				if (meta->EOS) {
					m_host->log(Debug, format("Ending transfer for URL: \"%s\"", url).c_str());
					zeroSizeConnections[url]->getInput(0)->push(nullptr);
					zeroSizeConnections[url]->flush();
					zeroSizeConnections.erase(url);
				}
			}
		}

	private:
		Modules::KHost* const m_host;
		atomic_bool done;
		map<string, shared_ptr<Modules::Out::HTTP>> zeroSizeConnections;
		const string baseURL, userAgent;
		const vector<string> headers;
};

using namespace Modules;

Modules::IModule* createObject(KHost* host, void* va) {
	auto config = (HttpSinkConfig*)va;
	enforce(host, "HttpSink: host can't be NULL");
	enforce(config, "HttpSink: config can't be NULL");
	return Modules::createModule<HttpSink>(host, config->baseURL, config->userAgent, config->headers).release();
}

auto const registered = Factory::registerModule("HttpSink", &createObject);
}
