#pragma once

#include "i_filter.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_modules/core/module.hpp"

using namespace Modules;

namespace Pipelines {

struct IStatsRegistry;

/* wrapper around the module */
class Filter :
	public IFilter,
	public KHost,
	private IPipelineNotifier {
	public:
		Filter(const char* name,
		    LogSink* pLog,
		    IPipelineNotifier *notify,
		    Pipelines::Threading threading,
		    IStatsRegistry *statsRegistry);

		void setDelegate(std::shared_ptr<IModule> module);

		void connect(IOutput *output, int inputIdx, bool inputAcceptMultipleConnections);
		void disconnect(int inputIdx, IOutput* output);

		int getNumInputs() const override;
		int getNumOutputs() const override;
		IInput* getInput(int i);
		IOutput* getOutput(int i);
		Metadata getOutputMetadata(int i) override;

		/* source modules are stopped manually - then the message propagates to other connected modules */
		bool isSource();

		void startSource();
		void stopSource();

	private:
		void mimicInputs();
		void processSource();

		// KHost implementation
		void log(int level, char const* msg) override;

		// IPipelineNotifier implementation
		void endOfStream() override;
		void exception(std::exception_ptr eptr) override;

		LogSink* const m_log;
		std::string const m_name;
		std::shared_ptr<IModule> delegate;
		std::unique_ptr<IExecutor> const executor;

		bool started = false;
		bool stopped = false;

		IPipelineNotifier * const m_notify;
		int connections = 0;
		std::atomic<int> eosCount;

		IStatsRegistry * const statsRegistry;

		std::vector<std::unique_ptr<IInput>> inputs;
};

}
