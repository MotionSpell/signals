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
	if(readCount > 0)
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

bool startsWith(std::string s, std::string prefix) {
	return !s.compare(0, prefix.size(), prefix);
}

Data createData(std::vector<uint8_t> const& contents) {
	auto r = make_shared<DataRaw>(contents.size());
	if(contents.size())
		memcpy(r->data().ptr, contents.data(), contents.size());
	return r;
}
}

struct Private {

	Private(std::string url, bool usePUT) {
		curl_global_init(CURL_GLOBAL_ALL);
		curl = createCurl(url, usePUT);
	}

	~Private() {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}

	void send(Data data) {
		m_fifo.push(data);
	}

	void threadProc();

	bool finished;

	Data m_prefixData;
	Data m_currData;
	span<const uint8_t> m_currBs {}; // points to the contents of m_currData/m_suffixData

	IModuleHost* m_log {};
	curl_slist* headers {};
	CURL *curl;
	std::thread workingThread;

	// data to upload
	Queue<Data> m_fifo;
};

HTTP::HTTP(IModuleHost* host, HttpOutputConfig const& cfg)
	: m_host(host), m_suffixData(createData(cfg.endOfSessionSuffix)) {
	if (!startsWith(cfg.url, "http://") && !startsWith(cfg.url, "https://"))
		throw error(format("can only handle URLs starting with 'http://' or 'https://', not '%s'.", cfg.url));

	// before any other connection, make an empty POST to check the end point exists
	if (cfg.flags.InitialEmptyPost)
		enforceConnection(cfg.url, cfg.flags.UsePUT);

	// create pins
	addInput(this);
	outputFinished = addOutput<OutputDefault>();

	// initialize the sender object
	m_pImpl = make_unique<Private>(cfg.url, cfg.flags.UsePUT);
	m_pImpl->m_log = host;

	curl_easy_setopt(m_pImpl->curl, CURLOPT_USERAGENT, cfg.userAgent.c_str());
	curl_easy_setopt(m_pImpl->curl, CURLOPT_READFUNCTION, &HTTP::staticCurlCallback);
	curl_easy_setopt(m_pImpl->curl, CURLOPT_READDATA, this);

	for (auto &h : cfg.headers) {
		m_pImpl->headers = curl_slist_append(m_pImpl->headers, h.c_str());
		curl_easy_setopt(m_pImpl->curl, CURLOPT_HTTPHEADER, m_pImpl->headers);
	}

	m_pImpl->workingThread = std::thread(&Private::threadProc, m_pImpl.get());
}

HTTP::~HTTP() {
	m_pImpl->send(m_suffixData);
	m_pImpl->send(nullptr);
	m_pImpl->workingThread.join();
}

void HTTP::setPrefix(span<const uint8_t> prefix) {
	m_pImpl->m_prefixData = createData({prefix.ptr, prefix.ptr+prefix.len});
}

void HTTP::flush() {
	m_pImpl->send(nullptr);

	auto out = outputFinished->getBuffer(0);
	outputFinished->emit(out);
}

void HTTP::process(Data data) {
	m_pImpl->send(data);
}

size_t HTTP::staticCurlCallback(void *buffer, size_t size, size_t nmemb, void *userp) {
	auto pThis = (HTTP*)userp;
	return pThis->fillBuffer(span<uint8_t>((uint8_t*)buffer, size * nmemb));
}

bool HTTP::loadNextBs() {
	m_pImpl->m_currData = m_pImpl->m_fifo.pop();
	if(!m_pImpl->m_currData)
		return false;

	m_pImpl->m_currBs = m_pImpl->m_currData->data();
	return true;
}

size_t HTTP::fillBuffer(span<uint8_t> buffer) {
	// curl stops calling us when we return 0.
	if(m_pImpl->finished)
		return 0;

	auto writer = buffer;
	while(writer.len > 0) {
		if (m_pImpl->m_currBs.len == 0) {
			if (!loadNextBs()) {
				m_pImpl->finished = true;
				break;
			}
		}

		writer += read(m_pImpl->m_currBs, writer.ptr, writer.len);
	}

	return writer.ptr - buffer.ptr;
}

void Private::threadProc() {
	headers = curl_slist_append(headers, "Transfer-Encoding: chunked");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	do {
		finished = false;
		m_currBs = {};

		// load prefix, if any
		if(m_prefixData)
			m_currBs = m_prefixData->data();

		auto res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			m_log->log(Warning, format("Transfer failed: %s", curl_easy_strerror(res)).c_str());
	} while(!finished);
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

