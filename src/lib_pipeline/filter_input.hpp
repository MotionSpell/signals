#pragma once

#include "lib_modules/core/module.hpp"

namespace Pipelines {

/* Wrapper around the module's inputs.
   Data is queued in the calling thread, then always dispatched by the executor
   Data is nullptr at completion. */
class FilterInput : public IInput {
	public:
		FilterInput(IInput *input,
		    const std::string &moduleName,
		    Signals::IExecutor* executor,
		    IStatsRegistry* statsRegistry,
		    IEventSink * const eventSink,
		    KHost* host
		)
			: delegate(input), eventSink(eventSink), m_host(host), executor(executor),
			  statsCumulated(statsRegistry->getNewEntry((moduleName + ".cumulated").c_str())),
			  statsPending(statsRegistry->getNewEntry((moduleName + ".pending").c_str())) {
		}

		void push(Data data) override {
			queue.push(data);
			statsPending->value ++;

			executor->call([this]() {
				doProcess();
			});
		}

		Data pop() override {
			assert(false);
			return {};
		}

		bool tryPop(Data&) override {
			assert(false);
			return {};
		}

		int isConnected() const override {
			return delegate->isConnected();
		}
		void connect() override {
			delegate->connect();
		}
		void disconnect() override {
			delegate->disconnect();
		}
		void clear() {
			queue.clear();
		}
		Metadata getMetadata() const override {
			return m_metadataCap.getMetadata();
		}
		void setMetadata(Metadata metadata) override {
			m_metadataCap.setMetadata(metadata);
		}
		bool updateMetadata(Data &data) override {
			return m_metadataCap.updateMetadata(data);
		}

	private:

		void doProcess() {
			try {
				Data data;
				if(!queue.tryPop(data))
					return; // spurious process: can occur at destruction
				statsPending->value --;
				statsCumulated->value = samplingCounter++;

				// receiving 'nullptr' means 'end of stream'
				if (!data) {
					m_host->log(Debug, "notify end-of-stream.");
					eventSink->endOfStream();
					return;
				}

				delegate->push(data);
			} catch(std::exception const& e) {
				m_host->log(Error, (std::string("Can't process data: ") + e.what()).c_str());
				throw;
			}
		}

		Queue<Data> queue;
		IInput *delegate;
		IEventSink * const eventSink;
		KHost * const m_host;
		Signals::IExecutor * const executor;
		decltype(StatsEntry::value) samplingCounter = 0;
		StatsEntry * const statsCumulated;
		StatsEntry * const statsPending;
		MetadataCap m_metadataCap;
};

}
