#pragma once

#include "i_filter.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_modules/modules.hpp"
#include <condition_variable>
#include <functional>
#include <vector>
#include <memory>
#include <string>

namespace Pipelines {

struct IStatsRegistry;
struct Graph;
class Filter;

using CreationFunc = std::function<std::shared_ptr<Modules::IModule>(Modules::KHost*)>;

// A set of interconnected processing filters.
class Pipeline : public IEventSink {
	public:
		template <typename InstanceType, int NumBlocks = 0, typename ...Args>
		IFilter * addModule(Args&&... args) {
			auto name = std::to_string(modules.size());
			return addNamedModule<InstanceType, NumBlocks>(name.c_str(), std::forward<Args>(args)...);
		}

		template <typename InstanceType, int NumBlocks = 0, typename ...Args>
		IFilter * addNamedModule(const char* instanceName, Args&&... args) {
			auto createModule = [&](Modules::KHost* host) {
				return Modules::createModuleWithSize<InstanceType>(
				        getNumBlocks(NumBlocks),
				        host,
				        std::forward<Args>(args)...);
			};

			return addModuleInternal(instanceName, createModule);
		}

		IFilter * add(char const* typeName, const void* va);

		/* @isLowLatency Controls the default number of buffers.
		   @threading    Controls the threading. */
		Pipeline(LogSink* log = nullptr, bool isLowLatency = false, Threading threading = Threading::OnePerModule);
		virtual ~Pipeline();

		// Remove a module from a pipeline.
		// This is only possible when the module is disconnected and flush()ed
		// (which is the caller responsibility - FIXME)
		void removeModule(IFilter * module);
		void connect(OutputPin out, InputPin in, bool inputAcceptMultipleConnections = false);
		void disconnect(IFilter * prev, int outputIdx, IFilter * next, int inputIdx);

		std::string dumpDOT() const; // dump pipeline using DOT Language

		void start();
		void waitForEndOfStream();
		void exitSync(); /*ask for all sources to finish*/

		void registerErrorCallback(std::function<bool(const char*)>);

	private:
		IFilter * addModuleInternal(std::string name, CreationFunc createModule);
		void computeTopology();
		void endOfStream();
		bool/*handled*/ exception(std::exception_ptr eptr);

		/*FIXME: the block below won't be necessary once we inject correctly*/
		int getNumBlocks(int numBlock) const {
			return numBlock ? numBlock : allocatorNumBlocks;
		}

		std::unique_ptr<IStatsRegistry> statsMem;
		std::vector<std::unique_ptr<Filter>> modules;
		std::unique_ptr<Graph> graph;
		LogSink* const m_log;
		const int allocatorNumBlocks;
		const Threading threading;

		std::mutex remainingNotificationsMutex;
		std::condition_variable condition;
		size_t notifications = 0, remainingNotifications = 0;
		std::exception_ptr eptr;
		std::function<bool(const char*)> errorCbk;
};

}
