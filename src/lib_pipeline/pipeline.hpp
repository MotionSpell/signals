#pragma once

#include "lib_modules/modules.hpp"
#include <vector>
#include <memory>
#include <string>

namespace Pipelines {

struct Graph;
struct IStatsRegistry;

struct IPipelinedModule {
	virtual ~IPipelinedModule() {};
	virtual int getNumInputs() const = 0;
	virtual int getNumOutputs() const = 0;
	virtual Modules::IInput* getInput(int i) = 0;  //TODO: hide this
	virtual std::shared_ptr<const Modules::IMetadata> getOutputMetadata(int i) = 0;
};

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

		struct ModuleHost : Modules::IModuleHost {
			void log(int level, char const* msg) override {
				Log::msg((Level)level, "[%s] %s", name.c_str(), msg);
			}
			std::string name;
		};

		template <typename InstanceType, int NumBlocks = 0, typename ...Args>
		IPipelinedModule * addModule(Args&&... args) {
			return addModuleInternal(
			        make_unique<Modules::NullHostType>(),
			        Modules::createModule<InstanceType>(
			            getNumBlocks(NumBlocks),
			            std::forward<Args>(args)...)
			    );
		}

		template <typename InstanceType, int NumBlocks = 0, typename ...Args>
		IPipelinedModule * addModuleWithHost(Args&&... args) {
			auto host = make_unique<ModuleHost>();
			host->name = format("%s (#%s)", typeid(InstanceType).name(), (int)modules.size());
			auto pHost = host.get();
			return addModuleInternal(
			        std::move(host),
			        Modules::createModule<InstanceType>(
			            getNumBlocks(NumBlocks),
			            pHost,
			            std::forward<Args>(args)...)
			    );
		}

		IPipelinedModule * add(char const* name, ...);

		/* @isLowLatency Controls the default number of buffers.
			@threading    Controls the threading. */
		Pipeline(bool isLowLatency = false, Threading threading = OnePerModule);
		virtual ~Pipeline();

		// Remove a module from a pipeline.
		// This is only possible when the module is disconnected and flush()ed
		// (which is the caller responsibility - FIXME)
		void removeModule(IPipelinedModule * module);
		void connect   (IPipelinedModule * prev, int outputIdx, IPipelinedModule * next, int inputIdx, bool inputAcceptMultipleConnections = false);
		void disconnect(IPipelinedModule * prev, int outputIdx, IPipelinedModule * next, int inputIdx);

		std::string dump(); /*dump pipeline using DOT Language*/

		void start();
		void waitForEndOfStream();
		void exitSync(); /*ask for all sources to finish*/

	private:
		IPipelinedModule * addModuleInternal(std::unique_ptr<Modules::IModuleHost> host, std::shared_ptr<Modules::IModule> rawModule);
		void computeTopology();
		void endOfStream();
		void exception(std::exception_ptr eptr);

		/*FIXME: the block below won't be necessary once we inject correctly*/
		int getNumBlocks(int numBlock) const {
			return numBlock ? numBlock : allocatorNumBlocks;
		}

		std::unique_ptr<IStatsRegistry> statsMem;
		std::vector<std::unique_ptr<IPipelinedModule>> modules;
		std::unique_ptr<Graph> graph;
		const int allocatorNumBlocks;
		const Threading threading;

		std::mutex remainingNotificationsMutex;
		std::condition_variable condition;
		size_t notifications = 0, remainingNotifications = 0;
		std::exception_ptr eptr;
};

}
