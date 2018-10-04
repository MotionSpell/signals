#include "http.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_utils/log_sink.hpp" // Warning
#include "lib_utils/format.hpp"
#include <string.h> // memcpy
#include <thread>

extern "C" {
#include <curl/curl.h>
}

using namespace Modules;

namespace Modules {
namespace Out {

namespace {
size_t writeVoid(void *buffer, size_t size, size_t nmemb, void *userp) {
	(void)buffer;
	(void)userp;
	return size * nmemb;
}

size_t read(span<const uint8_t>& stream, uint8_t* dst, size_t dstLen) {
	auto readCount = std::min(stream.len, dstLen);
	memcpy(dst, stream.ptr, readCount);
	stream += readCount;
	return readCount;
}

CURL* createCurl(std::string url, bool usePUT) {
	auto curl = curl_easy_init();
	if (!curl)
		throw std::runtime_error("Couldn't init the HTTP stack.");

	// setup HTTP request
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	if (usePUT)
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	else
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

	// don't check certifcates
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	// keep execution silent
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeVoid);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

	return curl;
}

void enforceConnection(std::string url, bool usePUT) {
	std::shared_ptr<void> curl(createCurl(url, usePUT), &curl_easy_cleanup);

	if (usePUT)
		curl_easy_setopt(curl.get(), CURLOPT_INFILESIZE_LARGE, 0);
	else
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0);

	auto const res = curl_easy_perform(curl.get());
	if (res != CURLE_OK)
		throw std::runtime_error(format("Can't connect to '%s'", url));
}

static
bool startsWith(std::string s, std::string prefix) {
	return !s.compare(0, prefix.size(), prefix);
}

}

struct HTTP::Private {

	Private(std::string url, bool usePUT) {
		curl_global_init(CURL_GLOBAL_ALL);
		curl = createCurl(url, usePUT);
	}

	~Private() {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}
	curl_slist* headers {};
	CURL *curl;
	std::thread workingThread;

	// data to upload
	Queue<Data> m_fifo;
};

HTTP::HTTP(IModuleHost* host, HttpOutputConfig const& cfg)
	: m_host(host), url(cfg.url), endOfSessionSuffix(cfg.endOfSessionSuffix) {
	if (!startsWith(url, "http://") && !startsWith(url, "https://"))
		throw error(format("can only handle URLs starting with 'http://' or 'https://', not '%s'.", url));

	// before any other connection, make an empty POST to check the end point exists
	if (cfg.flags.InitialEmptyPost)
		enforceConnection(url, cfg.flags.UsePUT);

	// create pins
	addInput(this);
	outputFinished = addOutput<OutputDefault>();

	// initialize the sender object
	m_pImpl = make_unique<Private>(url, cfg.flags.UsePUT);

	auto& curl = m_pImpl->curl;

	curl_easy_setopt(curl, CURLOPT_USERAGENT, cfg.userAgent.c_str());
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, &HTTP::staticCurlCallback);
	curl_easy_setopt(curl, CURLOPT_READDATA, this);

	if (cfg.flags.Chunked) {
		m_pImpl->headers = curl_slist_append(m_pImpl->headers, "Transfer-Encoding: chunked");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_pImpl->headers);
	}
	for (auto &h : cfg.headers) {
		m_pImpl->headers = curl_slist_append(m_pImpl->headers, h.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_pImpl->headers);
	}

	m_pImpl->workingThread = std::thread(&HTTP::threadProc, this, cfg.flags.Chunked);
}

HTTP::~HTTP() {
	m_pImpl->m_fifo.push(nullptr);
	m_pImpl->workingThread.join();
}

void HTTP::endOfStream() {
	m_pImpl->m_fifo.push(nullptr);
}

void HTTP::flush() {
	endOfStream();

	auto out = outputFinished->getBuffer(0);
	outputFinished->emit(out);
}

void HTTP::process(Data data) {
	m_pImpl->m_fifo.push(data);
}

void HTTP::readTransferedBs(uint8_t* dst, size_t size) {
	auto n = read(m_currBs, dst, size);
	if (n != size)
		throw error("Short read on transfered bitstream");
}

void HTTP::clean() {
	m_currBs = {};
	m_currData = nullptr;
}

size_t HTTP::staticCurlCallback(void *buffer, size_t size, size_t nmemb, void *userp) {
	auto pThis = (HTTP*)userp;
	return pThis->fillBuffer(span<uint8_t>((uint8_t*)buffer, size * nmemb));
}

bool HTTP::loadNextData() {
	m_currData = m_pImpl->m_fifo.pop();
	if(!m_currData)
		return false;

	m_currBs = m_currData->data();
	return true;
}

size_t HTTP::fillBuffer(span<uint8_t> buffer) {
	if (state == RunNewConnection && m_currData) {
		// restart transfer of the current chunk from the beginning
		m_currBs = m_currData->data();
		m_host->log(Warning, "Reconnect");
	}

	if (!m_currData) {
		if (!loadNextData()) {
			if (state == Stop)
				return 0;

			state = Stop;
			enforce(buffer.len >= endOfSessionSuffix.size(), "The end-of-session suffix must fit into the buffer");
			memcpy(buffer.ptr, endOfSessionSuffix.data(), endOfSessionSuffix.size());
			return endOfSessionSuffix.size();
		}

		if (state != RunNewConnection) {
			state = RunNewFile; //on new connection, don't call newFileCallback()
		}
	}

	if (state == RunNewConnection) {
		state = RunResume;
	} else if (state == RunNewFile) {
		m_controller->newFileCallback(buffer);
		state = RunResume;
	}

	auto const desiredCount = std::min(m_currBs.len, buffer.len);
	auto const readCount = read(m_currBs, buffer.ptr, desiredCount);
	if (readCount == 0) {
		clean();
		return fillBuffer(buffer);
	}

	return readCount;
}

bool HTTP::performTransfer() {
	state = RunNewConnection;
	CURLcode res = curl_easy_perform(m_pImpl->curl);
	if (res != CURLE_OK) {
		m_host->log(Warning, format("Transfer failed for '%s': %s", url, curl_easy_strerror(res)).c_str());
	}

	if (state == Stop)
		return false;

	return true;
}

void HTTP::threadProc(bool chunked) {
	if (chunked) {
		while (state != Stop && performTransfer()) {
		}
	} else {
		performTransfer();
	}
}

}
}

namespace {

IModule* createObject(IModuleHost* host, va_list va) {
	auto cfg = va_arg(va, HttpOutputConfig*);
	enforce(host, "HTTP: host can't be NULL");
	return create<Out::HTTP>(host, *cfg).release();
}

auto const registered = Factory::registerModule("HTTP", &createObject);
}

