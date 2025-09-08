#include "lib_appcommon/options.hpp"
#include "lib_utils/log.hpp" // setGlobalLogLevel

#include <chrono>
#include <thread>

#include "config.hpp"
#include "pipeliner_player.hpp"

using namespace std;
using namespace Pipelines;

namespace {
Config parseCommandLine(int argc, char const *argv[]) {

  Config cfg;

  CmdLineOptions opt;
  opt.addFlag("l", "lowlatency", &cfg.lowLatency, "Use low latency");
  opt.add("g", "loglevel", &cfg.logLevel, "Log level");
  opt.add("a", "stop-after", &cfg.stopAfterMs, "Stop after X ms");
  opt.add("n", "no-renderer", &cfg.noRenderer, "Don't render anything (headless), decode only");
  opt.addFlag("h", "help", &cfg.help, "Print usage and exit.");

  auto files = opt.parse(argc, argv);

  if(cfg.help) {
    printf("Usage: %s [options] <URL>\nOptions:\n", argv[0]);
    opt.printHelp();
    return cfg;
  }

  if(files.size() != 1)
    throw std::runtime_error("invalid command line, use --help");

  cfg.url = files[0];

  return cfg;
}
}

int safeMain(int argc, char const *argv[]) {
  auto const cfg = parseCommandLine(argc, argv);
  if(cfg.help)
    return 0;

  setGlobalLogLevel((Level)cfg.logLevel);

  Pipeline pipeline(g_Log, cfg.lowLatency);
  declarePipeline(cfg, pipeline, cfg.url.c_str());
  pipeline.start();

  if(cfg.stopAfterMs >= 0)
    this_thread::sleep_for(chrono::milliseconds(cfg.stopAfterMs));
  else
    pipeline.waitForEndOfStream();

  return 0;
}

int main(int argc, char const *argv[]) {
  try {
    return safeMain(argc, argv);
  } catch(std::exception const &e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
}
