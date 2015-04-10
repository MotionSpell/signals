#pragma once

#include <memory>
#include <vector>
#include "stranded_pool_executor.hpp"
#include "../core/module.hpp"

namespace Modules {

class Module;
class Data;
struct IPin;

struct ICompletionNotifier {
	virtual void finished() = 0;
};

class PipelinedModule : public IModule {
public:
	/* take ownership of module */
	PipelinedModule(Module *module, ICompletionNotifier *notify);
	void connect(IPin* pin);
	size_t getNumPin() const;
	IPin* getPin(size_t i) const;

	/* direct call: receiving nullptr stops the execution */
	void process(std::shared_ptr<const Data> data);

	/* same as process() but uses the executor (may defer the call) */
	void dispatch(std::shared_ptr<const Data> data);

	/* source modules are stopped manually - then the message propagates to other connected modules */
	void setSource(bool isSource);
	bool isSource() const;
	bool isSink() const;

private:
	void endOfStream();

	enum Type {
		None,
		Source
	};
	Type type;

	std::unique_ptr<Module> delegate;
	std::unique_ptr<IProcessExecutor> const localExecutor;
	IProcessExecutor &executor;
	ICompletionNotifier* const m_notify;
};

class Pipeline : public ICompletionNotifier {
public:
	Pipeline(bool isLowLatency = false);
	PipelinedModule* addModule(Module* rawModule, bool isSource = false);
	void connect(IPin* pin, PipelinedModule *module);
	void start();
	void waitForCompletion();
	void finished() override;

private:
	std::vector<std::unique_ptr<PipelinedModule>> modules;
	bool isLowLatency;

	std::mutex mutex;
	std::condition_variable condition;
	std::atomic<int> numRemainingNotifications;
};

}
