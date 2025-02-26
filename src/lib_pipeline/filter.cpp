#include "filter.hpp"
#include "log_sink.hpp"
#include "format.hpp"
#include "tools.hpp" // enforce
#include "lib_signals/executor_threadpool.hpp"
#include "stats.hpp"
#include "filter_input.hpp"

using namespace Modules;
using Signals::IExecutor;

namespace Pipelines {

std::unique_ptr<IExecutor> createExecutor(Pipelines::Threading threading, const char* name) {
	if(int(threading & (Pipelines::Threading::Mono)))
		return make_unique<Signals::ExecutorSync>();
	else
		return make_unique<Signals::ExecutorThread>(name);
}

Filter::Filter(const char* name,
    LogSink* pLog,
    IEventSink *eventSink,
    Pipelines::Threading threading,
    IStatsRegistry *statsRegistry)
	: m_log(pLog),
	  m_name(name),
	  m_eventSink(eventSink),
	  eosCount(0),
	  statsRegistry(statsRegistry),
	  executor(createExecutor(threading, name)) {
	stopped = false;
}

Filter::~Filter() {
}

// KHost implementation
void Filter::log(int level, char const* msg) {
	m_log->log((Level)level, format("[%s] %s", m_name.c_str(), msg).c_str());
}

void Filter::activate(bool enable) {
	active = enable;
}

void Filter::setDelegate(std::shared_ptr<IModule> module) {
	delegate = module;
}

int Filter::getNumInputs() const {
	return delegate->getNumInputs();
}

int Filter::getNumOutputs() const {
	return delegate->getNumOutputs();
}

IOutput* Filter::getOutput(int i) {
	if (i >= delegate->getNumOutputs())
		throw std::runtime_error(format("Filter %s: no output %s.", m_name, i));
	return delegate->getOutput(i);
}

Metadata Filter::getOutputMetadata(int i) {
	return getOutput(i)->getMetadata();
}

/* source modules are stopped manually - then the message propagates to other connected modules */
bool Filter::isSource() {
	return active;
}

void Filter::connect(IOutput *output, int inputIdx, bool inputAcceptMultipleConnections) {
	auto input = getInput(inputIdx);
	if (!inputAcceptMultipleConnections && input->isConnected())
		throw std::runtime_error(format("Filter %s: input %s is already connected.", m_name, inputIdx));

	input->connect();

	CheckMetadataCompatibility(output, input);

	output->connect(input);

	connections++;
}

void Filter::disconnect(int inputIdx, IOutput* output) {
	getInput(inputIdx)->disconnect();
	output->disconnect();
	connections--;
}

void Filter::mimicInputs() {
	IEventSink* const pinEventSink = this;
	while ((int)inputs.size()< delegate->getNumInputs()) {
		auto idx = (int)inputs.size();
		auto dgInput = delegate->getInput(idx);
		auto name = format("%s, input (#%s)", m_name, idx);
		inputs.push_back(make_unique<FilterInput>(dgInput, name, executor.get(), statsRegistry, pinEventSink, this));
	}
}

IInput* Filter::getInput(int i) {
	mimicInputs();
	if (i >= (int)inputs.size())
		throw std::runtime_error(format("Filter %s: no input %s.", m_name, i));
	return inputs[i].get();
}

void Filter::processSource() {
	if(stopped || !active) {
		endOfStream();
		return; // don't reschedule
	}

	try {
		delegate->process();
	} catch(std::exception const& e) {
		log(Error, (std::string("Source error: ") + e.what()).c_str());
		exception(std::current_exception());
		if (active)
			stopSource();
	}

	reschedule();
}

void Filter::startSource() {
	assert(isSource());

	if (started) {
		log(Info, "Pipeline: source already started . Doing nothing.");
		return;
	}

	connections = 1;

	reschedule();

	started = true;
}

void Filter::reschedule() {
	/* if we are being destructed from another thread the unique_ptr may return null
	   if not we are safe because the executor destructor will join() before deletion occurs */
	auto e = executor.get();
	if (e)
		e->call(std::bind(&Filter::processSource, this));
}

void Filter::stopSource() {
	assert(isSource());

	if (!started) {
		log(Warning, "Pipeline: cannot stop unstarted source. Ignoring.");
		return;
	}

	stopped = true;
}

void Filter::destroyOutputs() {
	for(int i=0; i < delegate->getNumOutputs(); ++i) {
		auto o = delegate->getOutput(i);
		o->disconnect();
	}
}

// IEventSink implementation

void Filter::endOfStream() {
	++eosCount;

	if (eosCount > connections) {
		auto const msg = format("Filter %s: received too many EOS (%s/%s)", m_name, (int)eosCount, (int)connections);
		throw std::runtime_error(msg);
	}

	if (eosCount == connections) {
		delegate->flush();

		for (int i = 0; i < delegate->getNumOutputs(); ++i) {
			delegate->getOutput(i)->post(nullptr);
		}

		if (connections) {
			m_eventSink->endOfStream();
		}
	}
}

void Filter::exception(std::exception_ptr eptr) {
	m_eventSink->exception(eptr);
}

}
