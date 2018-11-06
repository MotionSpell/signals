#pragma once

// This is the binary boundary between:
// - binary applications (plugin host for binary 'signals' plugins).
// - binary third-party 'signals' modules ("UM").
//
// This is the only header file that third-parties are expected to include.
// Do not pass or return STL objects or concrete classes here.

#include "buffer.hpp"
#include "metadata.hpp"

namespace Signals {
template<typename T> struct ISignal;
}

namespace Modules {

// This is how user modules see the outside world.
struct IModuleHost {
	virtual ~IModuleHost() = default;
	virtual void log(int level, char const* msg) = 0;
};

struct NullHostType : IModuleHost {
	void log(int, char const*) override;
};

static NullHostType NullHost;

// This is how the framework sees custom module implementations.
//
// As these interfaces are implemented by third-parties,
// they should be kept small and leave almost no room for errors.
// (stuff like connection lists and pin lists should be kept in the framework).
//

struct IProcessor {
	virtual ~IProcessor() = default;
	virtual void process() = 0;
};

struct IInput : IProcessor, virtual IMetadataCap {
	virtual ~IInput() = default;

	virtual int isConnected() const = 0;
	virtual void connect() = 0;
	virtual void disconnect() = 0;

	virtual void push(Data) = 0;

	// TODO: remove this, should only be visible to the module implementations.
	virtual Data pop() = 0;
	virtual bool tryPop(Data &value) = 0;
};

struct IOutput : virtual IMetadataCap {
	virtual ~IOutput() = default;
	virtual void emit(Data data) = 0;
	virtual Signals::ISignal<Data>& getSignal() = 0;
};

struct IOutputCap {
		virtual ~IOutputCap() = default;

	protected:
		/*const*/ size_t allocatorSize = 0;
};

struct IModule : IProcessor,  virtual IOutputCap {
	virtual ~IModule() = default;

	virtual int getNumInputs() const = 0;
	virtual IInput* getInput(int i) = 0;

	virtual int getNumOutputs() const = 0;
	virtual IOutput* getOutput(int i) = 0;

	virtual void flush() {}
};

}
