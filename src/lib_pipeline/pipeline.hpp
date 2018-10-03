#pragma once

#include "lib_utils/log.hpp"
#include "lib_utils/format.hpp"
#include "lib_modules/modules.hpp"
#include <vector>
#include <memory>
#include <string>

namespace Pipelines {

struct Graph;
struct IStatsRegistry;

struct IFilter {
	virtual ~IFilter() {};
	virtual int getNumInputs() const = 0;
	virtual int getNumOutputs() const = 0;
	virtual std::shared_ptr<const Modules::IMetadata> getOutputMetadata(int i) = 0;
};

struct InputPin {
	InputPin(IFilter* m, int idx=0) : mod(m), index(idx) {};
	IFilter* mod;
	int index = 0;
};

struct OutputPin {
	OutputPin(IFilter* m, int idx=0) : mod(m), index(idx) {};
	IFilter* mod;
	int index = 0;
};

inline InputPin GetInputPin(IFilter* mod, int index=0) {
	return InputPin { mod, index };
}

inline OutputPin GetOutputPin(IFilter* mod, int index=0) {
	return OutputPin { mod, index };
}

struct IPipelineNotifier {
	virtual void endOfStream() = 0;
	virtual void exception(std::exception_ptr eptr) = 0;
};

/* not thread-safe */
class Pipeline : public IPipelineNotifier {
	public:
		enum Threading {
			Mono              = 1,
			OnePerModule      = 2,
		};

		std::unique_ptr<Modules::IModuleHost> createModuleHost(std::string name);

		template <typename InstanceType, int NumBlocks = 0, typename ...Args>
		IFilter * addModule(Args&&... args) {
			auto name = format("%s", modules.size());
			auto host = createModuleHost(name);
			auto mod = Modules::createModule<InstanceType>(
			        getNumBlocks(NumBlocks),
			        host.get(),
			        std::forward<Args>(args)...);
			return addModuleInternal(name, std::move(host), std::move(mod));
		}

		IFilter * add(char const* name, ...);

		/* @isLowLatency Controls the default number of buffers.
			@threading    Controls the threading. */
		Pipeline(LogSink* log = nullptr, bool isLowLatency = false, Threading threading = OnePerModule);
		virtual ~Pipeline();

		// Remove a module from a pipeline.
		// This is only possible when the module is disconnected and flush()ed
		// (which is the caller responsibility - FIXME)
		void removeModule(IFilter * module);
		void connect   (OutputPin out, InputPin in, bool inputAcceptMultipleConnections = false);
		void disconnect(IFilter * prev, int outputIdx, IFilter * next, int inputIdx);

		std::string dump(); /*dump pipeline using DOT Language*/

		void start();
		void waitForEndOfStream();
		void exitSync(); /*ask for all sources to finish*/

	private:
		IFilter * addModuleInternal(std::string name, std::unique_ptr<Modules::IModuleHost> host, std::shared_ptr<Modules::IModule> rawModule);
		void computeTopology();
		void endOfStream();
		void exception(std::exception_ptr eptr);

		/*FIXME: the block below won't be necessary once we inject correctly*/
		int getNumBlocks(int numBlock) const {
			return numBlock ? numBlock : allocatorNumBlocks;
		}

		std::unique_ptr<IStatsRegistry> statsMem;
		std::vector<std::unique_ptr<IFilter>> modules;
		std::unique_ptr<Graph> graph;
		LogSink* const m_log;
		const int allocatorNumBlocks;
		const Threading threading;

		std::mutex remainingNotificationsMutex;
		std::condition_variable condition;
		size_t notifications = 0, remainingNotifications = 0;
		std::exception_ptr eptr;
};

}
