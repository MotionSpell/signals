#pragma once

// Helpers to make it easier to implement custom modules.
// The application code should not depend on this.

#include "../core/module.hpp"
#include "../core/allocator.hpp"
#include "../core/error.hpp"
#include "../core/database.hpp" // Data, Metadata
#include "lib_signals/helper.hpp"
#include "lib_signals/signals.hpp" // Signals::Signal
#include "lib_utils/tools.hpp" // uptr
#include <memory>

namespace Modules {

class MetadataCap {
	public:
		MetadataCap(Metadata metadata = nullptr);

		Metadata getMetadata() const {
			return m_metadata;
		}
		void setMetadata(Metadata metadata);
		bool updateMetadata(Data &data);

	private:
		Metadata m_metadata;
};

struct Output : public IOutput {
	void post(Data data) override {
		m_metadataCap.updateMetadata(data);
		signal.emit(data);
	}

	void connect(IInput* next) override {
		signal.connect([=](Data data) {
			next->push(data);
		});
	}

	void disconnect() override {
		signal.disconnectAll();
	}

	Metadata getMetadata() const override {
		return m_metadataCap.getMetadata();
	}

	void setMetadata(Metadata metadata) override {
		m_metadataCap.setMetadata(metadata);
	}

	Signals::Signal<Data> signal;
	MetadataCap m_metadataCap;
};

template<typename DataType>
class OutputWithAllocator : public Output {
	public:
		OutputWithAllocator(size_t allocatorMaxSize, Metadata metadata = nullptr)
			: allocator(createMemoryAllocator(allocatorMaxSize)) {
			m_metadataCap.setMetadata(metadata);
		}
		virtual ~OutputWithAllocator() {
			allocator->unblock();
		}

		std::shared_ptr<DataType> getBuffer(size_t size) {
			return alloc<DataType>(allocator, size);
		}

		void resetAllocator(size_t allocatorSize) {
			allocator = createMemoryAllocator(allocatorSize);
		}

	private:
		std::shared_ptr<IAllocator> allocator;
};

typedef OutputWithAllocator<DataRaw> OutputDefault;

class OutputCap : public virtual IOutputCap {
	public:
		OutputCap(size_t allocatorSize) {
			this->allocatorSize = allocatorSize;
		}
};

class Module : public IModule {
	public:
		int getNumInputs() const override {
			return (int)inputs.size();
		}
		IInput* getInput(int i) override {
			return inputs[i].get();
		}

		int getNumOutputs() const override {
			return (int)outputs.size();
		}
		IOutput* getOutput(int i) override {
			return outputs[i].get();
		}

	protected:
		KInput* addInput();

		template <typename InstanceType>
		InstanceType* addOutput() {
			auto p = new InstanceType(allocatorSize);
			outputs.push_back(uptr(p));
			return p;
		}

		std::vector<std::unique_ptr<IInput>> inputs;
		std::vector<std::unique_ptr<IOutput>> outputs;
};

/* this default factory creates output ports with the default output - create another one for other uses such as low latency */
template <class InstanceType>
struct ModuleDefault : public OutputCap, public InstanceType {
	template <typename ...Args>
	ModuleDefault(size_t allocatorSize, Args&&... args)
		: OutputCap(allocatorSize), InstanceType(std::forward<Args>(args)...) {
	}
};

template <typename InstanceType, typename ...Args>
std::unique_ptr<InstanceType> createModuleWithSize(size_t allocatorSize, Args&&... args) {
	return make_unique<ModuleDefault<InstanceType>>(allocatorSize, std::forward<Args>(args)...);
}

template <typename InstanceType, typename ...Args>
std::unique_ptr<InstanceType> createModule(Args&&... args) {
	return make_unique<ModuleDefault<InstanceType>>(ALLOC_NUM_BLOCKS_DEFAULT, std::forward<Args>(args)...);
}

//single input specialized module
class ModuleS : public Module {
	public:
		ModuleS() : input(Module::addInput()) {
		}
		virtual void processOne(Data data) = 0;
		void process() override {
			Data data;
			if(input->tryPop(data))
				processOne(data);
		}

		// prevent derivatives from trying to add inputs
		void addInput() {}
	protected:
		KInput* const input;
};

// used by unit tests
void ConnectOutput(IOutput* o, std::function<void(Data)> f);

struct NullHostType : KHost {
	void log(int, char const*) override;
	void activate(bool) override {};
};

static NullHostType NullHost;
}
