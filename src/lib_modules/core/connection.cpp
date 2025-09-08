#include "connection.hpp"

#include "lib_signals/signal.hpp"
#include "lib_utils/log.hpp" // g_Log

#include <stdexcept>

#include "metadata.hpp"

namespace Modules {

void CheckMetadataCompatibility(IOutput *prev, IInput *next) {
  auto prevMetadata = prev->getMetadata();
  auto nextMetadata = next->getMetadata();
  if(prevMetadata && nextMetadata) {
    if(prevMetadata->type != next->getMetadata()->type)
      throw std::runtime_error("Module connection: incompatible types");
    g_Log->log(Debug, "--------- Connect: metadata OK");
  } else {
    if(prevMetadata && !nextMetadata) {
      g_Log->log(Debug, "--------- Connect: metadata doesn't propagate to next (forward)");
    } else if(!prevMetadata && nextMetadata) {
      prev->setMetadata(nextMetadata);
      g_Log->log(Debug, "--------- Connect: metadata propagates to previous (backward).");
    } else {
      g_Log->log(Debug, "--------- Connect: no metadata");
    }
  }
}

void ConnectOutputToInput(IOutput *prev, IInput *next) {
  CheckMetadataCompatibility(prev, next);

  next->connect();
  prev->connect(next);
}

void ConnectModules(IModule *prev, int outputIdx, IModule *next, int inputIdx) {
  auto output = prev->getOutput(outputIdx);
  ConnectOutputToInput(output, next->getInput(inputIdx));
}

}
