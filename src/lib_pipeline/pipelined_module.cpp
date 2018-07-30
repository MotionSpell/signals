#include "lib_signals/executor_threadpool.hpp"
#include "stats.hpp"
#include "pipelined_module.hpp"
#include "pipelined_input.hpp"

#define EXECUTOR_SYNC              Signals::ExecutorSync
#define EXECUTOR_ASYNC_THREAD      Signals::ExecutorThread(getDelegateName())

#define EXECUTOR                   EXECUTOR_ASYNC_THREAD
#define EXECUTOR_LIVE              EXECUTOR_SYNC
#define EXECUTOR_INPUT_DEFAULT     (&g_executorSync)

using namespace Modules;

/* automatic inputs have a loose datatype */
struct DataLoosePipeline : public DataBase {};

namespace Pipelines {

/* take ownership of module and executor */
PipelinedModule::PipelinedModule(std::unique_ptr<Modules::IModuleHost> host, std::shared_ptr<IModule> module, IPipelineNotifier *notify, Pipeline::Threading threading, IStatsRegistry *statsRegistry)
	: m_host(std::move(host)),
	  delegate(std::move(module)),
	  executor(threading & Pipeline::Mono ? (IExecutor*)new EXECUTOR_LIVE : (IExecutor*)new EXECUTOR),
	  m_notify(notify),
	  eosCount(0),
	  statsRegistry(statsRegistry) {
}

PipelinedModule::~PipelinedModule() {
	// FIXME: we shouldn't do semantics here, but it is needed as long as
	//        the ActiveModule loop is not included in PipelinedModule
	if (isSource())
		stopSource();

	// inputs, which hold the executors,
	// must be destroyed *before* the 'delegate' module.
	inputs.clear();
}

std::string PipelinedModule::getDelegateName() const {
	auto const &dref = *delegate.get();
	return typeid(dref).name();
}

int PipelinedModule::getNumInputs() const {
	return delegate->getNumInputs();
}
int PipelinedModule::getNumOutputs() const {
	return delegate->getNumOutputs();
}
IOutput* PipelinedModule::getOutput(int i) {
	if (i >= delegate->getNumOutputs())
		throw std::runtime_error(format("PipelinedModule %s: no output %s.", getDelegateName(), i));
	return delegate->getOutput(i);
}

std::shared_ptr<const IMetadata> PipelinedModule::getOutputMetadata(int i) {
	return getOutput(i)->getMetadata();
}

class HackInput : public Input {
	public:
		HackInput(IProcessor* proc) : Input(proc) {
		}
};

/* source modules are stopped manually - then the message propagates to other connected modules */
bool PipelinedModule::isSource() {
	if (delegate->getNumInputs() == 0) {
		return true;
	} else if (delegate->getNumInputs() == 1 && dynamic_cast<HackInput*>(delegate->getInput(0))) {
		return true;
	} else {
		return false;
	}
}

void PipelinedModule::connect(IOutput *output, int inputIdx, bool inputAcceptMultipleConnections) {
	auto input = safe_cast<PipelinedInput>(getInput(inputIdx));
	ConnectOutputToInput(output, input, inputExecutor[inputIdx]);
	if (!inputAcceptMultipleConnections && (input->getNumConnections() != 1))
		throw std::runtime_error(format("PipelinedModule %s: input %s has %s connections.", getDelegateName(), inputIdx, input->getNumConnections()));
	connections++;
}

void PipelinedModule::disconnect(int inputIdx, IOutput * const output) {
	getInput(inputIdx)->disconnect();
	auto &sig = output->getSignal();
	auto const numConn = sig.getNumConnections();
	for (size_t i = 0; i < numConn; ++i) {
		sig.disconnect(i);
	}
	connections--;
}

void PipelinedModule::mimicInputs() {
	while ((int)inputs.size()< delegate->getNumInputs()) {
		auto const i = (int)inputs.size();
		inputExecutor.push_back(EXECUTOR_INPUT_DEFAULT);
		addInput(new PipelinedInput(delegate->getInput(i), getDelegateName(), *executor, statsRegistry->getNewEntry(), this));
	}
}

IInput* PipelinedModule::getInput(int i) {
	mimicInputs();
	if (i >= (int)inputs.size())
		throw std::runtime_error(format("PipelinedModule %s: no input %s.", getDelegateName(), i));
	return inputs[i].get();
}

/* uses the executor (i.e. may defer the call) */
void PipelinedModule::startSource() {
	assert(isSource());

	if (started) {
		Log::msg(Info, "Pipeline: source already started . Doing nothing.");
		return;
	}

	Log::msg(Debug, "Module %s: start source - dispatching data", getDelegateName());

	// first time: create a fake input port
	// and push null to trigger execution
	safe_cast<InputCap>(delegate.get())->addInput(new HackInput(delegate.get()));
	connections = 1;
	auto input = getInput(0);
	input->push(nullptr);
	delegate->getInput(0)->push(nullptr);
	(*executor)(Bind(&IProcessor::process, delegate.get()));
	(*executor)(Bind(&IProcessor::process, input));

	started = true;
}

void PipelinedModule::stopSource() {
	assert(isSource());

	if (started) {
		// the source is likely processing: push EOS in the loop
		// and let things follow their way
		delegate->getInput(0)->push(nullptr);
	} else {
		Log::msg(Warning, "Pipeline: cannot stop unstarted source. Ignoring.");
	}
}

// IPipelineNotifier implementation

void PipelinedModule::endOfStream() {
	++eosCount;

	if (eosCount > connections) {
		auto const msg = format("PipelinedModule %s: received too many EOS (%s/%s)", getDelegateName(), (int)eosCount, (int)connections);
		throw std::runtime_error(msg);
	}

	if (eosCount == connections) {
		delegate->flush();

		for (int i = 0; i < delegate->getNumOutputs(); ++i) {
			delegate->getOutput(i)->emit(nullptr);
		}

		if (connections) {
			m_notify->endOfStream();
		}
	}
}

void PipelinedModule::exception(std::exception_ptr eptr) {
	m_notify->exception(eptr);
}

}
