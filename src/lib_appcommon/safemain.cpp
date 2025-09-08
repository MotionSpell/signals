#include <csignal>
#include <iostream> // cerr
#include <profiler.hpp>

// user-provided
extern void safeMain(int argc, const char *argv[]);

extern const char *g_appName;
extern const char *g_version;

//////////////////////////////////////

#include "pipeline.hpp"

std::shared_ptr<Pipelines::Pipeline> g_Pipeline;

static void safeDelete() {
  if(g_Pipeline)
    g_Pipeline = nullptr;
}

static void safeStop() {
  if(g_Pipeline)
    g_Pipeline->exitSync();
}

//////////////////////////////////////

static void onInterruption() {
  static int numSig = 0;
  numSig++;
  if(numSig == 1) {
    std::cerr << "Caught signal, asking to stop." << std::endl;
    safeStop();
  } else if(numSig == 2) {
    std::cerr << "Caught signal, exiting now." << std::endl;
    safeDelete();
  } else if(numSig > 2) {
    std::cerr << "Caught " << numSig << " signals, hard exit." << std::endl;
    _Exit(3);
  }
}

static void sigTermHandler(int sig) {
  switch(sig) {
  case SIGINT:
  case SIGTERM:
    onInterruption();
    break;
  default:
    break;
  }
}

static void installSignalHandler() {
  std::signal(SIGINT, sigTermHandler);
  std::signal(SIGTERM, sigTermHandler);
}

int main(int argc, char const *argv[]) {
  try {
    Tools::Profiler profilerGlobal(std::string(g_appName) + " execution time");
    std::cerr << "BUILD:     " << g_appName << "-" << g_version << std::endl;

#ifdef ENABLE_BOMB
    if(checkTimebomb(BOMB_TIME_IN_DAYS)) {
      throw std::runtime_error("Your trial period expired");
    }
#endif

    installSignalHandler();
    safeMain(argc, argv);
    g_Pipeline = nullptr; // ensure deletion before global variables it may rely on

    return 0;
  } catch(std::exception const &e) {
    std::cerr << "[" << g_appName << "] " << "Error: " << e.what() << std::endl;
    return 1;
  }
}
