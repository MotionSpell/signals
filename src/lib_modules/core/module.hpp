#pragma once

#include "clock.hpp"
#include "data.hpp"
#include "error.hpp"
#include "metadata.hpp"
#include "lib_utils/queue.hpp"
#include "lib_signals/signals.hpp"

namespace Modules {

struct IProcessor {
	virtual ~IProcessor() noexcept(false) {}
	virtual void process() = 0;
};

class ConnectedCap {
	public:
		ConnectedCap() : connections(0) {}
		virtual ~ConnectedCap() noexcept(false) {}
		virtual size_t getNumConnections() const {
			return connections;
		}
		virtual void connect() {
			connections++;
		}
		virtual void disconnect() {
			connections--;
		}

	private:
		std::atomic_size_t connections;
};

struct IInput : public IProcessor, public ConnectedCap, public MetadataCap, public Queue<Data> {
	virtual ~IInput() noexcept(false) {}
};

struct IInputCap {
	virtual ~IInputCap() noexcept(false) {}
	virtual IInput* addInput(IInput* p) = 0;
	virtual size_t getNumInputs() const = 0;
	virtual IInput* getInput(size_t i) = 0;
};

struct IOutput : virtual IMetadataCap {
	virtual ~IOutput() noexcept(false) {}
	virtual size_t emit(Data data) = 0;
	virtual Signals::ISignal<void(Data)>& getSignal() = 0;
};

// FIXME: remove this (see below)
#include <memory>
#include <vector>

struct IOutputCap {
		virtual ~IOutputCap() noexcept(false) {}
		virtual size_t getNumOutputs() const = 0;
		virtual IOutput* getOutput(size_t i) = 0;

	protected:
		/*FIXME: we need to have factories to move these back to the implementation - otherwise ports created from the constructor may crash*/
		std::vector<std::unique_ptr<IOutput>> outputs;
		/*const*/ size_t allocatorSize = 0;
};

struct IModule : IProcessor, virtual IInputCap, virtual IOutputCap, virtual IClockCap {
	virtual ~IModule() noexcept(false) {}
	virtual void flush() {}
};

}
