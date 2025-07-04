#include "lib_utils/queue.hpp"

namespace Modules {

class Input : public IInput {
	public:
		Input(IProcessor * const processor) : processor(processor) {}

		virtual int isConnected() const override {
			return connections > 0;
		}

		virtual void connect() override {
			connections++;
		}

		virtual void disconnect() override {
			connections--;
		}

		void push(Data data) override {
			queue.push(data);
			processor->process();
		}

		Data pop() override {
			return queue.pop();
		}

		bool tryPop(Data& data) override {
			return queue.tryPop(data);
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

		void clear() {
			return queue.clear();
		}

	private:
		bool setMetadataInternal(Metadata metadata);

		MetadataCap m_metadataCap;
		IProcessor * const processor;
		Queue<Data> queue;
		int connections = 0;
};

}

