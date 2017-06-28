#include "pipelined_module.hpp"
#include "pipeline.hpp"
#include "lib_modules/utils/helper.hpp"

#define COMPLETION_GRANULARITY_IN_MS 200

namespace Pipelines {

Pipeline::Pipeline(bool isLowLatency, double clockSpeed, Threading threading)
: allocatorNumBlocks(isLowLatency ? Modules::ALLOC_NUM_BLOCKS_LOW_LATENCY : Modules::ALLOC_NUM_BLOCKS_DEFAULT),
  clock(new Modules::Clock(clockSpeed)), threading(threading), remainingNotifications(0) {
}

IPipelinedModule* Pipeline::addModuleInternal(IModule *rawModule) {
	auto module = uptr(new PipelinedModule(rawModule, this, clock.get(), threading));
	auto ret = module.get();
	modules.push_back(std::move(module));
	return ret;
}

void Pipeline::connect(IModule *p, size_t outputIdx, IModule *n, size_t inputIdx, bool inputAcceptMultipleConnections) {
	if (!n || !p) return;
	std::unique_lock<std::mutex> lock(mutex);
	if (remainingNotifications != notifications)
		throw std::runtime_error("Connection but the topology has changed. Not supported yet.");
	auto next = safe_cast<IPipelinedModule>(n);
	auto prev = safe_cast<IPipelinedModule>(p);
	next->connect(prev->getOutput(outputIdx), inputIdx, prev->isSource(), inputAcceptMultipleConnections);
	computeTopology();
}

void Pipeline::disconnect(IModule *p, size_t outputIdx) {
	if (!p) return;
	std::unique_lock<std::mutex> lock(mutex);
	if (remainingNotifications != notifications)
		throw std::runtime_error("Disconnection but the topology has changed. Not supported yet.");
	auto prev = safe_cast<IPipelinedModule>(p);
	prev->disconnect(prev->getOutput(outputIdx));
	computeTopology();
}

void Pipeline::removeModule(IPipelinedModule *module) {
	std::unique_lock<std::mutex> lock(mutex);
	for (auto &m : modules) {
		if (m.get() == module) {
			m = nullptr;
			return;
		}
	}
	throw std::runtime_error("Could not remove module from pipeline");
}

void Pipeline::start() {
	Log::msg(Info, "Pipeline: starting");
	computeTopology();
	for (auto &m : modules) {
		if (m->isSource()) {
			m->process();
		}
	}
	Log::msg(Info, "Pipeline: started");
}

void Pipeline::waitForCompletion() {
	Log::msg(Info, "Pipeline: waiting for completion");
	std::unique_lock<std::mutex> lock(mutex);
	while (remainingNotifications > 0) {
		Log::msg(Debug, "Pipeline: condition (remaining: %s) (%s modules in the pipeline)", (size_t)remainingNotifications, modules.size());
		condition.wait_for(lock, std::chrono::milliseconds(COMPLETION_GRANULARITY_IN_MS));
		try {
			if (eptr)
				std::rethrow_exception(eptr);
		} catch (const std::exception &e) {
			Log::msg(Error, "Pipeline: exception caught: %s. Exiting.", e.what());
			exitSync();
			std::rethrow_exception(eptr); //FIXME: at this point the exception forward in submit() already lost some data
		}
	}
	Log::msg(Info, "Pipeline: completed");
}

void Pipeline::exitSync() {
	Log::msg(Warning, "Pipeline: asked to exit now.");
	for (auto &m : modules) {
		if (m->isSource()) {
			m->process(); //Romain: ok but then we don't flush() so it may be stuck in waitForCompletion
		}
	}
}

void Pipeline::computeTopology() {
	notifications = 0;
	for (auto &m : modules) {
		if (m->isSink()) {
			if (m->isSource()) {
				notifications++;
			} else {
				for (size_t i = 0; i < m->getNumInputs(); ++i) {
					if (m->getInput(i)->getNumConnections()) { //Romain: we will fail here on disconnection
						notifications++;
						break;
					}
				}
			}
		}
	}
	remainingNotifications = notifications;
}

void Pipeline::finished() {
	std::unique_lock<std::mutex> lock(mutex);
	assert(remainingNotifications > 0);
	--remainingNotifications;
	condition.notify_one();
}

void Pipeline::exception(std::exception_ptr e) {
	std::unique_lock<std::mutex> lock(mutex);
	eptr = e;
	condition.notify_one();
}

}
