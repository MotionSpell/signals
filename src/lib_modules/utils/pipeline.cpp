#include "pipeline.hpp"
#include "stranded_pool_executor.hpp"
#include <typeinfo>
#include "helper.hpp"

#define EXECUTOR_SYNC         Signals::ExecutorSync<void()>
#define EXECUTOR_ASYNC_THREAD Signals::ExecutorThread<void()>(getDelegateName())
#define EXECUTOR_ASYNC_POOL   StrandedPoolModuleExecutor
#define EXECUTOR EXECUTOR_ASYNC_THREAD

using namespace Modules;

namespace {
	class FakeOutput : public ModuleS {
	public:
		FakeOutput() {
			addInput(new Input<DataBase>(this));
		}
		void process(Data data) override final {}
	};
}

namespace Pipelines {

namespace {
template<typename Class>
Signals::MemberFunctor<void, Class, void(Class::*)()>
MEMBER_FUNCTOR_NOTIFY_FINISHED(Class* objectPtr) {
	return Signals::MemberFunctor<void, Class, void(Class::*)()>(objectPtr, &ICompletionNotifier::finished);
}
}

/* Wrapper around the module's inputs. Data is queued in the calling thread, then always dispatched by the executor. */
class PipelinedInput : public IInput {
	public:
		PipelinedInput(IInput *input, IProcessExecutor &executor, ICompletionNotifier * const notify)
			: delegate(input), notify(notify), executor(executor) {}
		virtual ~PipelinedInput() noexcept(false) {}

		/* receiving nullptr stops the execution */
		void process() override {
			auto data = pop();
			if (data) {
				Log::msg(Debug, "Module %s: dispatch data for time %s", typeid(delegate).name(), data->getTime() / (double)IClock::Rate);
				delegate->push(data);
				executor(MEMBER_FUNCTOR_PROCESS(delegate));
			} else {
				Log::msg(Debug, "Module %s: notify finished.", typeid(delegate).name());
				executor(MEMBER_FUNCTOR_NOTIFY_FINISHED(notify));
			}
		}

		size_t getNumConnections() const override {
			return delegate->getNumConnections();
		}
		void connect() override {
			delegate->connect();
		}

	private:
		IInput *delegate;
		ICompletionNotifier * const notify;
		IProcessExecutor &executor;
};

/* Wrapper around the module. */
class PipelinedModule : public ICompletionNotifier, public IPipelinedModule, public InputCap {
public:
	/* take ownership of module */
	PipelinedModule(IModule *module, ICompletionNotifier *notify)
		: delegate(module), localExecutor(new EXECUTOR), executor(*localExecutor), m_notify(notify) {
	}
	~PipelinedModule() noexcept(false) {}
	std::string getDelegateName() const {
		return typeid(*delegate).name();
	}

	size_t getNumInputs() const override {
		return delegate->getNumInputs();
	}
	size_t getNumOutputs() const override {
		return delegate->getNumOutputs();
	}
	IOutput* getOutput(size_t i) const override {
		if (i >= delegate->getNumOutputs())
			throw std::runtime_error(format("PipelinedModule %s: no output %s.", getDelegateName(), i));
		return delegate->getOutput(i);
	}

	/* source modules are stopped manually - then the message propagates to other connected modules */
	bool isSource() const override {
		if (delegate->getNumInputs() == 0) {
			return true;
		} else if (delegate->getNumInputs() == 1 && dynamic_cast<Input<DataLoose, IProcessor>*>(delegate->getInput(0))) {
			return true;
		} else {
			return false;
		}
	}
	bool isSink() const override {
		return delegate->getNumOutputs() == 0;
	}

private:
	void connect(IOutput *output, size_t inputIdx) override {
		auto input = getInput(inputIdx);
		ConnectOutputToInput(output, input, &g_executorSync);
		if (input->getNumConnections() != 1)
			throw std::runtime_error(format("PipelinedModule %s: input %s has %s connections.", getDelegateName(), inputIdx, input->getNumConnections()));
	}

	void mimicInputs() {
		auto const delegateInputs = delegate->getNumInputs();
		auto const thisInputs = inputs.size();
		if (thisInputs < delegateInputs) {
			for (size_t i = thisInputs; i < delegateInputs; ++i) {
				addInput(new PipelinedInput(delegate->getInput(i), this->executor, this));
			}
		}
	}

	IInput* getInput(size_t i) override {
		mimicInputs();
		if (i >= inputs.size())
			throw std::runtime_error(format("PipelinedModule %s: no input %s.", getDelegateName(), i));
		return inputs[i].get();
	}

	/* uses the executor (i.e. may defer the call) */
	void process() override {
		Log::msg(Debug, "Module %s: dispatch data", getDelegateName());

		if (isSource()) {
			if (getNumInputs() == 0) {
				/*first time: create a fake pin and push null to trigger execution*/
				delegate->addInput(new Input<DataLoose>(delegate.get()));
				getInput(0)->push(nullptr);
				delegate->getInput(0)->push(nullptr);
				executor(MEMBER_FUNCTOR_PROCESS(delegate.get()));
				executor(MEMBER_FUNCTOR_PROCESS(getInput(0)));
				return;
			} else {
				/*the source is likely processing: push null in the loop to exit and let things follow their way*/
				delegate->getInput(0)->push(nullptr);
				return;
			}
		}

		Data data = getInput(0)->pop();
		for (size_t i = 0; i < getNumInputs(); ++i) {
			getInput(i)->push(data);
			getInput(i)->process();
		}
	}

	void finished() override {
		delegate->flush();
		if (isSink()) {
			m_notify->finished();
		} else {
			for (size_t i = 0; i < delegate->getNumOutputs(); ++i) {
				delegate->getOutput(i)->emit(nullptr);
			}
		}
	}

	std::unique_ptr<IModule> delegate;
	std::unique_ptr<IProcessExecutor> const localExecutor;
	IProcessExecutor &executor;
	ICompletionNotifier* const m_notify;
};

Pipeline::Pipeline(bool isLowLatency) : isLowLatency(isLowLatency), numRemainingNotifications(0) {
}

IPipelinedModule* Pipeline::addModuleInternal(IModule *rawModule) {
	auto module = uptr(new PipelinedModule(rawModule, this));
	auto ret = module.get();
	modules.push_back(std::move(module));
	return ret;
}

void Pipeline::connect(IModule *prev, size_t outputIdx, IModule *n, size_t inputIdx) {
	if (!n) return;
	auto next = safe_cast<IPipelinedModule>(n);
	if (next->isSink()) {
		numRemainingNotifications++;
	}
	next->connect(prev->getOutput(outputIdx), inputIdx);
}

void Pipeline::start() {
	Log::msg(Info, "Pipeline: starting");
	Log::msg(Debug, "Pipeline: check for unconnected outputs");
	for (size_t m = 0; m < modules.size(); ++m) {
		for (size_t i = 0; i < modules[m]->getNumOutputs(); ++i) {
			auto output = modules[m]->getOutput(i);
			if (output->getSignal().getNumConnections() == 0) {
				Log::msg(Debug, "Pipeline: connecting fake input to output %s of module %s", i, safe_cast<PipelinedModule>(modules[m].get())->getDelegateName());
				connect(modules[m].get(), i, addModule<FakeOutput>(), 0);
			}
		}
	}
	Log::msg(Debug, "Pipeline: start sources");
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
	while (numRemainingNotifications > 0) {
		Log::msg(Debug, "Pipeline: completion (remaining: %s) (%s modules in the pipeline)", (int)numRemainingNotifications, modules.size());
		condition.wait(lock);
	}
	Log::msg(Info, "Pipeline: completed");
}

void Pipeline::exitSync() {
	Log::msg(Warning, "Pipeline: asked to exit now.");
	for (auto &m : modules) {
		if (m->isSource()) {
			m->process();
		}
	}
}

void Pipeline::finished() {
	std::unique_lock<std::mutex> lock(mutex);
	assert(numRemainingNotifications > 0);
	--numRemainingNotifications;
	condition.notify_one();
}

}
