#include "pipeline.hpp"
#include "stats.hpp"
#include "graph.hpp"
#include "filter.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/loader.hpp"
#include "log.hpp" // g_Log
#include "os.hpp"
#include "format.hpp"
#include "tools.hpp" // safe_cast
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

#define COMPLETION_GRANULARITY_IN_MS 200

static const size_t ALLOC_NUM_BLOCKS_LOW_LATENCY = 2;

namespace Pipelines {

struct StatsRegistry : IStatsRegistry {
	StatsRegistry() : shmem(createSharedMemory(size, std::to_string(getPid()).c_str(), true)), entryIdx(0) {
		memset(shmem->data(), 0, size);
	}

	StatsEntry* getNewEntry(const char* name) override {
		entryIdx++;
		if (entryIdx >= maxNumEntry)
			throw std::runtime_error(format("SharedMemory: accessing too far (%s with max=%s).", entryIdx - 1, maxNumEntry - 1));

		auto entry = (StatsEntry*)shmem->data() + entryIdx - 1;

		strncpy(entry->name, name, sizeof(entry->name)-1);
		entry->name[sizeof(entry->name)-1] = 0;

		return entry;
	}

	std::unique_ptr<SharedMemory> shmem;
	static const auto size = 256 * sizeof(StatsEntry);
	static const int maxNumEntry = size / sizeof(StatsEntry);
	StatsEntry **entries;
	int entryIdx;
};

Pipeline::Pipeline(LogSink* log, bool isLowLatency, Threading threading)
	: statsMem(new StatsRegistry), graph(new Graph),
	  m_log(log ? log : g_Log),
	  allocatorNumBlocks(isLowLatency ? ALLOC_NUM_BLOCKS_LOW_LATENCY : Modules::ALLOC_NUM_BLOCKS_DEFAULT),
	  threading(threading) {
}

Pipeline::~Pipeline() {
	// Prevent modules from communicating.
	// this allows to destroy them safely,
	// without having to topological-sort them.
	for(auto& m : modules)
		m->destroyOutputs();

	modules.clear();
}

IFilter* Pipeline::addModuleInternal(std::string name, CreationFunc createModule) {
	auto filter = make_unique<Filter>(name.c_str(), m_log, this, threading, statsMem.get());
	filter->setDelegate(createModule(filter.get()));
	auto pFilter = filter.get();
	modules.push_back(std::move(filter));
	graph->nodes.push_back(Graph::Node{pFilter, name});
	return pFilter;
}

IFilter * Pipeline::add(char const* type, const void* va) {
	auto name = format("%s (#%s)", type, (int)modules.size());

	auto createModule = [&](Modules::KHost* host) {
		return vLoadModule(type, host, va);
	};

	return addModuleInternal(name, createModule);
}

void Pipeline::removeModule(IFilter *module) {
	auto findIf = [module](Pipelines::Graph::Connection const& c) {
		return c.src.id == module || c.dst.id == module;
	};
	auto i_conn = std::find_if(graph->connections.begin(), graph->connections.end(), findIf);
	if (i_conn != graph->connections.end())
		throw std::runtime_error("Could not remove module: connections found");

	auto removeIt = [module](std::unique_ptr<Filter> const& m) {
		return m.get() == module;
	};
	auto i_mod = std::find_if(modules.begin(), modules.end(), removeIt);
	if (i_mod == modules.end())
		throw std::runtime_error("Could not remove from pipeline: module not found");
	modules.erase(i_mod);

	auto i_node = std::find_if(graph->nodes.begin(), graph->nodes.end(), [module](Pipelines::Graph::Node const& n) {
		return n.id == module;
	});
	assert(i_node != graph->nodes.end());
	graph->nodes.erase(i_node);

	computeTopology();
}

void Pipeline::connect(OutputPin prev, InputPin next, bool inputAcceptMultipleConnections) {
	if (!next.mod || !prev.mod) return;
	auto n = safe_cast<Filter>(next.mod);
	auto p = safe_cast<Filter>(prev.mod);

	{
		std::unique_lock<std::mutex> lock(remainingNotificationsMutex);
		if (remainingNotifications != notifications)
			throw std::runtime_error("Connection but the topology has changed. Not supported yet."); //TODO: to change that, we need to store a state of the Filter.
	}

	n->connect(p->getOutput(prev.index), next.index, inputAcceptMultipleConnections);
	computeTopology();

	graph->connections.push_back(Graph::Connection{graph->nodeFromId(prev.mod), prev.index, graph->nodeFromId(next.mod), next.index});
}

void Pipeline::disconnect(IFilter * prev, int outputIdx, IFilter * next, int inputIdx) {
	if (!prev) return;
	auto n = safe_cast<Filter>(next);
	auto p = safe_cast<Filter>(prev);

	auto removeIf = [prev, outputIdx, next, inputIdx](Pipelines::Graph::Connection const& c) {
		return c.src.id == prev && c.srcPort == outputIdx && c.dst.id == next && c.dstPort == inputIdx;
	};
	auto i_conn = std::find_if(graph->connections.begin(), graph->connections.end(), removeIf);
	if (i_conn == graph->connections.end())
		throw std::runtime_error("Could not disconnect: connection not found");
	graph->connections.erase(i_conn);

	{
		std::unique_lock<std::mutex> lock(remainingNotificationsMutex);
		if (remainingNotifications != notifications)
			throw std::runtime_error("Disconnection but the topology has changed. Not supported yet.");
	}
	n->disconnect(inputIdx, p->getOutput(outputIdx));
	computeTopology();
}

std::string Pipeline::dump() const {
	std::stringstream ss;
	ss << "digraph {" << std::endl;
	ss << "\trankdir = \"LR\";" << std::endl;

	for (auto &node : graph->nodes)
		ss << "\t\"" << node.caption << "\";" << std::endl;

	for (auto &conn : graph->connections)
		ss << "\t\"" << conn.src.caption << "\" -> \"" << conn.dst.caption << "\";" << std::endl;

	ss << "}" << std::endl;
	return ss.str();
}

void Pipeline::start() {
	computeTopology();
	for (auto &module : modules) {
		auto m = safe_cast<Filter>(module.get());
		if (m->isSource()) {
			m->startSource();
		}
	}
	m_log->log(Info, "Pipeline: started");
}

void Pipeline::waitForEndOfStream() {
	m_log->log(Info, "Pipeline: waiting for completion");
	std::unique_lock<std::mutex> lock(remainingNotificationsMutex);
	while (remainingNotifications > 0) {
		m_log->log(Debug, format("Pipeline: condition (remaining: %s) (%s modules in the pipeline)", remainingNotifications, modules.size()).c_str());
		condition.wait_for(lock, std::chrono::milliseconds(COMPLETION_GRANULARITY_IN_MS));
	}
	m_log->log(Info, "Pipeline: completed");
}

void Pipeline::exitSync() {
	m_log->log(Info, "Pipeline: asked to exit now.");
	for (auto &module : modules) {
		auto m = safe_cast<Filter>(module.get());
		if (m->isSource()) {
			m->stopSource();
		}
	}
}

void Pipeline::computeTopology() {
	auto hasAtLeastOneInputConnected = [](Filter* m) {
		for (int i = 0; i < m->getNumInputs(); ++i) {
			if (m->getInput(i)->isConnected())
				return true;
		}
		return false;
	};

	notifications = 0;
	for (auto &module : modules) {
		auto m = safe_cast<Filter>(module.get());
		if (m->isSource() || hasAtLeastOneInputConnected(m))
			notifications++;
	}

	std::unique_lock<std::mutex> lock(remainingNotificationsMutex);
	remainingNotifications = notifications;
}

void Pipeline::endOfStream() {
	{
		std::unique_lock<std::mutex> lock(remainingNotificationsMutex);
		assert(remainingNotifications > 0);
		--remainingNotifications;
	}

	condition.notify_one();
}

void Pipeline::registerErrorCallback(std::function<void(const char*)> cbk) {
	errorCbk = cbk;
}

void Pipeline::exception(std::exception_ptr eptr) {
	try {
		std::rethrow_exception(eptr);
	} catch (const std::exception &e) {
		m_log->log(Error, format("Pipeline: exception caught: %s", e.what()).c_str());
		if (errorCbk)
			errorCbk(e.what());
	}
}

}
