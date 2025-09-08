#include "lib_appcommon/options.hpp"
#include "lib_media/out/file.hpp"
#include "lib_media/out/null.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "plugins/SocketInput/socket_input.hpp"

using namespace std;
using namespace Modules;
using namespace Pipelines;

struct Config {
  SocketInputConfig mcast;
  std::string outputPath;
  bool help = false;
};

namespace {
Config parseCommandLine(int argc, char const *argv[]) {
  Config cfg;
  cfg.mcast.isTcp = false;
  cfg.mcast.isMulticast = true;

  CmdLineOptions opt;
  opt.addFlag("h", "help", &cfg.help, "Print usage and exit");
  opt.add("o", "output", &cfg.outputPath, "Output file path");

  auto files = opt.parse(argc, argv);

  if(cfg.help) {
    printf("Usage: %s [options] <mcast address>:<UDP port>\nOptions:\n", argv[0]);
    opt.printHelp();
    return cfg;
  }

  if(files.size() != 1)
    throw std::runtime_error("invalid command line, use --help");

  sscanf(files[0].c_str(), "%d.%d.%d.%d:%d", &cfg.mcast.ipAddr[0], &cfg.mcast.ipAddr[1], &cfg.mcast.ipAddr[2],
        &cfg.mcast.ipAddr[3], &cfg.mcast.port);

  return cfg;
}

void declarePipeline(Config const &cfg, Pipeline &pipeline) {
  auto receiver = pipeline.add("SocketInput", &cfg.mcast);
  IFilter *sink;
  if(cfg.outputPath.empty())
    sink = pipeline.addModule<Out::Null>();
  else
    sink = pipeline.addModule<Out::File>(cfg.outputPath);
  pipeline.connect(receiver, sink);
}

}

int safeMain(int argc, char const *argv[]) {
  auto const cfg = parseCommandLine(argc, argv);
  if(cfg.help)
    return 0;

  Pipeline pipeline(nullptr, true);
  declarePipeline(cfg, pipeline);
  pipeline.start();
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
