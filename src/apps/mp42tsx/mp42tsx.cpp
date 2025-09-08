#include "lib_utils/profiler.hpp"

#include <iostream> // cerr

#include "pipeliner_mp42ts.hpp"

using namespace Pipelines;

int safeMain(int argc, char const *argv[]) {
  auto opt = parseCommandLine(argc, argv);

  Pipeline pipeline;
  declarePipeline(pipeline, opt);

  pipeline.start();
  pipeline.waitForEndOfStream();

  return 0;
}

int main(int argc, char const *argv[]) {
  try {
    return safeMain(argc, argv);
  } catch(std::exception const &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
