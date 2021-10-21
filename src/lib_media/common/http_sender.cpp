#include "http_sender.hpp"
#include <algorithm> // std::min
#include <string.h> // memcpy
#include <thread>
#include <mutex>
#include <condition_variable>
#include "lib_utils/log_sink.hpp" // Warning

extern "C" {
#include <curl/curl.h>
}

using namespace Modules;

namespace {
size_t writeVoid(void*, size_t size, size_t nmemb, void*) {
	return size * nmemb;
}

struct CurlScope {
	CurlScope() {
		curl_global_init(CURL_GLOBAL_ALL);
	}
	~CurlScope() {
		curl_global_cleanup();
	}
};

std::shared_ptr<CURL> createCurl(std::string url, HttpRequest request) {
	auto curl = std::shared_ptr<CURL>(curl_easy_init(), &curl_easy_cleanup);
	if (!curl)
		throw std::runtime_error("Couldn't init the HTTP stack.");

	// setup HTTP request
	curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
	if (request == PUT)
		curl_easy_setopt(curl.get(), CURLOPT_UPLOAD, 1L);
	else if (request == POST)
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
	else if (request == DELETEX)
		curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "DELETE");
	else
		throw std::runtime_error("Unknown HTTP request.");

	// don't check certifcates
	curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);

	// keep execution silent
	curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeVoid);
	curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 0L);

	return curl;
}

void append(std::vector<uint8_t>& dst, SpanC data) {
	auto offset = dst.size();
	dst.resize(offset + data.len);
	if(data.len)
		memcpy(&dst[offset], data.ptr, data.len);
}

struct CurlHttpSender : HttpSender {

		HttpSenderConfig const m_cfg;
		CurlHttpSender(HttpSenderConfig const& cfg, Modules::KHost* log) : m_cfg(cfg) {
			m_log = log;
			curlThread = std::thread(&CurlHttpSender::threadProc, this);
		}

		~CurlHttpSender() {
			destroying = true;
			curlThread.join();
		}

		void send(span<const uint8_t> data) override {
			if (m_cfg.request == DELETEX)
				// don't try to send anything on DELETE
				return;

			{
				std::unique_lock<std::mutex> lock(m_mutex);
				if(data.len) {
					append(m_fifo, data);
				} else {
					endOfDataFlag = true;
				}
				m_dataReady.notify_one();
			}

			if(!data.len) {
				// wait for flush finished, before returning
				std::unique_lock<std::mutex> lock(m_mutex);
				while(!allDataSent)
					m_allDataSent.wait(lock);
			}
		}

		void setPrefix(span<const uint8_t>  prefix) override {
			m_prefixData.clear();
			append(m_prefixData, prefix);
		}

	private:
		void perform(CURL *curl) {
			auto res = curl_easy_perform(curl);
			if (res != CURLE_OK)
				m_log->log(Warning, (std::string("Transfer failed: ") + curl_easy_strerror(res)).c_str());

			long http_code = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
			if (http_code >= 400)
				m_log->log(Warning, ("HTTP error: " + std::to_string(http_code) + " (" + m_cfg.url + ")").c_str());
		}

		void threadProc() {
#ifdef SIGNALS_SET_THREAD_NAME
            pthread_setname_np("signals::CurlHttpSender");
#endif
			auto curl = createCurl(m_cfg.url, m_cfg.request);

			curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, m_cfg.userAgent.c_str());

			for (auto &h : m_cfg.extraHeaders) {
				headers = curl_slist_append(headers, h.c_str());
				curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
			}

			if (m_cfg.request == DELETEX) {
				perform(curl.get());
				return;
			}

			headers = curl_slist_append(headers, "Transfer-Encoding: chunked");
			curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

			curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, &CurlHttpSender::staticCurlCallback);
			curl_easy_setopt(curl.get(), CURLOPT_READDATA, this);

			while(!destroying && !allDataSent) {
				// load prefix, if any
				if(m_prefixData.size())
					append(m_fifo, {m_prefixData.data(), m_prefixData.size()});

				perform(curl.get());
			}

			curl_slist_free_all(headers);
		}

		static size_t staticCurlCallback(void *buffer, size_t size, size_t nmemb, void *userp) {
			auto pThis = (CurlHttpSender*)userp;
			return pThis->fillBuffer(span<uint8_t>((uint8_t*)buffer, size * nmemb));
		}

		size_t fillBuffer(span<uint8_t> buffer) {
			// curl stops calling us when we return 0.

			std::unique_lock<std::mutex> lock(m_mutex);

			// if we're destroying, early-finish the transfer
			if(destroying)
				return 0;

			// wait for new data
			while(m_fifo.empty() && !endOfDataFlag)
				m_dataReady.wait(lock);

			auto const N = std::min<int>(buffer.len, m_fifo.size());
			if(N > 0) {
				memcpy(buffer.ptr, m_fifo.data(), N);
				memmove(m_fifo.data(), m_fifo.data()+N, m_fifo.size()-N);
				m_fifo.resize(m_fifo.size()-N);
			}

			if(m_fifo.empty() && endOfDataFlag) {
				allDataSent = true;
				m_allDataSent.notify_one();
			}

			return N;
		}

		CurlScope m_curlScope;
		bool destroying = false;

		std::condition_variable m_allDataSent;
		bool allDataSent = false;

		// data to send first at the beginning of each connection
		std::vector<uint8_t> m_prefixData;

		Modules::KHost* m_log {};
		curl_slist* headers {};
		std::thread curlThread;

		// data to upload
		std::mutex m_mutex;
		std::condition_variable m_dataReady;
		bool endOfDataFlag = false; // 'true' means 'm_fifo will not grow anymore'
		std::vector<uint8_t> m_fifo;
};
}

std::unique_ptr<HttpSender> createHttpSender(HttpSenderConfig const& config, Modules::KHost* log) {
	return std::make_unique<CurlHttpSender>(config, log);
}

void enforceConnection(std::string url, HttpRequest request) {
	CurlScope curlScope;

	auto curl = createCurl(url, request);

	if (request == PUT)
		curl_easy_setopt(curl.get(), CURLOPT_INFILESIZE_LARGE, 0);
	else if (request == POST)
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0);

	auto const res = curl_easy_perform(curl.get());
	if (res != CURLE_OK)
		throw std::runtime_error("Can't connect to '" + url + "'");
}

