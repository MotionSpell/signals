#include "http_input.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp" // enforce
#include "lib_media/common/file_puller.hpp"
#include <thread>

using namespace Modules;

std::unique_ptr<In::IFilePuller> createHttpSource();

namespace {

struct HttpInput : Module {
		HttpInput(KHost *host, HttpInputConfig const& cfg)
			: m_host(host), url(cfg.url) {
			out = addOutput();
			host->activate(true);
		}
		void flush() override {
			if (workingThread.joinable()) {
				source->askToExit();
				workingThread.join();
			}
		}
		void process() override {
			if (!source) {
				source = createHttpSource();

				workingThread = std::thread([&]() {
					auto onBuffer = [&](SpanC chunk) {
						auto data = out->allocData<DataRaw>(chunk.len);
						memcpy(data->buffer->data().ptr, chunk.ptr, chunk.len);
						outputs[0]->post(data);
					};
					m_host->log(Info, format("starting download of %s", url.c_str()).c_str());
					source->wget(url.c_str(), onBuffer);
					m_host->log(Info, format("download of %s completed", url.c_str()).c_str());
				});
			}
		}

	private:
		KHost * const m_host;
		OutputDefault *out;
		const std::string url;
		std::unique_ptr<In::IFilePuller> source;
		std::thread workingThread;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (HttpInputConfig*)va;
	enforce(host, "HttpInput: host can't be NULL");
	enforce(config, "HttpInput: config can't be NULL");
	return createModule<HttpInput>(host, *config).release();
}

auto const registered = Factory::registerModule("HttpInput", &createObject);

}
