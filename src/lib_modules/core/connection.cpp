#include "connection.hpp"
#include "metadata.hpp"
#include "lib_signals/signal.hpp"
#include "lib_signals/helper.hpp"
#include "lib_utils/log.hpp" // g_Log
#include <stdexcept>

namespace Modules {

void CheckMetadataCompatibility(IOutput *prev, IInput *next) {
	auto prevMetadata = prev->getMetadata();
	auto nextMetadata = next->getMetadata();
	if (prevMetadata && nextMetadata) {
		if (prevMetadata->type != next->getMetadata()->type)
			throw std::runtime_error("Module connection: incompatible types");
		g_Log->log(Info, "--------- Connect: metadata OK");
	} else {
		if (prevMetadata && !nextMetadata) {
			g_Log->log(Debug, "--------- Connect: metadata doesn't propagate to next (forward)");
		} else if (!prevMetadata && nextMetadata) {
			prev->setMetadata(nextMetadata);
			g_Log->log(Info, "--------- Connect: metadata propagates to previous (backward).");
		} else {
			g_Log->log(Debug, "--------- Connect: no metadata");
		}
	}
}

void ConnectOutputToInput(IOutput *prev, IInput *next) {
	CheckMetadataCompatibility(prev, next);

	next->connect();

	prev->getSignal().connect([=](Data data) {
		next->push(data);
		next->process();
	});
}

void ConnectModules(IModule *prev, int outputIdx, IModule *next, int inputIdx) {
	auto output = prev->getOutput(outputIdx);
	ConnectOutputToInput(output, next->getInput(inputIdx));
}

}
