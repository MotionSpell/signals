#include "lib_media/out/http.hpp"

#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "tests/tests.hpp"

#include <cstring> // memcpy

// To run the below tests, you must first launch the fake webserver:
// $ ./scripts/http-post-server.sh

using namespace Modules;

namespace {

std::shared_ptr<DataBase> createPacket(span<const char> contents) {
  auto r = std::make_shared<DataRaw>(contents.len);
  memcpy(r->buffer->data().ptr, contents.ptr, contents.len);
  return r;
}

}

secondclasstest("HTTP: should fail if the server doesn't exist") {
  HttpOutputConfig cfg{};
  cfg.flags.InitialEmptyPost = true;
  cfg.url = "http://unexisting_domain_name/nonexisting_page:1234";
  ASSERT_THROWN(loadModule("HTTP", &NullHost, &cfg));
}

secondclasstest("HTTP: empty post should succeed if the server exists") {
  HttpOutputConfig cfg{};
  cfg.flags.InitialEmptyPost = true;
  cfg.url = "http://127.0.0.1:9000";
  loadModule("HTTP", &NullHost, &cfg);
}

secondclasstest("HTTP: post data to real server") {
  HttpOutputConfig cfg{};
  cfg.flags.InitialEmptyPost = false;
  cfg.url = "http://127.0.0.1:9000";
  auto mod = loadModule("HTTP", &NullHost, &cfg);
  mod->getInput(0)->push(createPacket("Hello"));
  mod->getInput(0)->push(createPacket("Goodbye"));
  mod->process();
  mod->process();
  mod->flush();
}

secondclasstest("HTTP: post data to real server, suffix but no flush") {
  HttpOutputConfig cfg{};
  cfg.flags.InitialEmptyPost = false;
  cfg.url = "http://127.0.0.1:9000";
  cfg.endOfSessionSuffix = {'S', 'a', 'y', 'o', 'n', 'a', 'r', 'a'};
  auto mod = loadModule("HTTP", &NullHost, &cfg);
  mod->getInput(0)->push(createPacket("ThisIsPostData"));
  mod->process();
}

///////////////////////////////////////////////////////////////////////////////

#include "lib_media/common/http_sender.hpp"

secondclasstest("HttpSender: post data to real server") {
  HttpSenderConfig cfg{};
  cfg.url = "http://127.0.0.1:9000";

  auto sender = createHttpSender(cfg, &NullHost);

  {
    const uint8_t msg[] = "GutenTag";
    sender->send(msg);
  }

  {
    const uint8_t msg[] = "AufWiederSehen";
    sender->send(msg);
  }

  // flush
  sender->send({});
}

secondclasstest("HttpSender: post data to real server, no flush") {
  HttpSenderConfig cfg{};
  cfg.url = "http://127.0.0.1:9000";

  auto sender = createHttpSender(cfg, &NullHost);

  {
    const uint8_t msg[] = "GutenTag";
    sender->send(msg);
  }
}
