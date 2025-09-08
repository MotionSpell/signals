#include <memory>
#include <stdexcept>
#include <string>

#include "file_puller.hpp"
#include "span.hpp"

extern "C" {
#include <curl/curl.h>
}

struct HttpSource : Modules::In::IFilePuller {
  HttpSource()
      : curl(curl_easy_init()) {
    if(!curl)
      throw std::runtime_error("can't init curl");
  }

  ~HttpSource() { curl_easy_cleanup(curl); }

  void wget(const char *url, std::function<void(SpanC)> callback) override {
    struct HttpContext {
      HttpContext(bool &exiting)
          : exiting(exiting) {}

      static size_t curlCallback(void *stream, size_t size, size_t nmemb, void *ptr) {
        auto pThis = (HttpContext *)ptr;

        if(pThis->exiting)
          return CURL_READFUNC_ABORT;

        auto const bytes = size * nmemb;
        pThis->userCallback({(uint8_t *)stream, bytes});
        return bytes;
      }

      std::function<void(SpanC)> userCallback;
      bool &exiting;
    };

    HttpContext ctx(exiting);
    ctx.userCallback = callback;

    // some servers require a user-agent field
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    // follow redirections
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    // don't check certificates
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpContext::curlCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

    auto res = curl_easy_perform(curl);
    if(exiting)
      return;
    if(res == CURLE_HTTP_RETURNED_ERROR)
      return;
    if(res != CURLE_OK)
      throw std::runtime_error(std::string("HTTP download failed (url=\"") + url + "\"): " + curl_easy_strerror(res));
  }

  void askToExit() override { exiting = true; }

  CURL *const curl;
  bool exiting = false;
};

std::unique_ptr<Modules::In::IFilePuller> createHttpSource() { return std::make_unique<HttpSource>(); }
