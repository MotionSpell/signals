#pragma once

// This is the binary boundary between:
// - binary applications (plugin host for binary 'signals' plugins).
// - binary third-party 'signals' modules ("UM").
//
// This is the only header file that third-parties are expected to include.
// Do not pass or return STL objects or concrete classes here.

#include "buffer.hpp"
#include "metadata.hpp"

// FIXME: remove this
#include <memory>

namespace Modules {

class DataBase;
typedef std::shared_ptr<const DataBase> Data;
typedef std::shared_ptr<const IMetadata> Metadata;

// This is how user modules see the outside world.
// 'K' interfaces are called by the user module implementations.

// This is how a user module sees each one of its inputs.
struct KInput {
	virtual ~KInput() = default;

	// dequeue input data
	virtual Data pop() = 0;
	virtual bool tryPop(Data &value) = 0;

	// publish metadata for this input
	virtual void setMetadata(Metadata metadata) = 0;
};

// This is how a user module sees each one of its outputs.
struct KOutput {
	virtual ~KOutput() = default;

	// send 'data' downstream
	virtual void post(Data data) = 0;

	// publish metadata for this output
	virtual void setMetadata(Metadata metadata) = 0;
};

// This is how a user module sees its host.
struct KHost {
	virtual ~KHost() = default;

	// send a text message to the host
	virtual void log(int level, char const* msg) = 0;

	// if 'enable' is true, will cause 'process' to be called repeatedly
	virtual void activate(bool enable) = 0;
};

}

// This is how the framework sees custom module implementations.
//
// As these interfaces are implemented by third-parties,
// they should be kept small and leave almost no room for errors.
// (stuff like connection lists and pin lists should be kept in the framework).
//

namespace Signals {
template<typename T> struct ISignal;
}

namespace Modules {

struct IProcessor {
	virtual ~IProcessor() = default;
	virtual void process() = 0;
};

struct IInput : IProcessor, KInput {
	virtual ~IInput() = default;

	virtual int isConnected() const = 0;
	virtual void connect() = 0;
	virtual void disconnect() = 0;

	virtual void push(Data) = 0;

	virtual std::shared_ptr<const IMetadata> getMetadata() const = 0;
	virtual bool updateMetadata(Data&) = 0;
};

struct IOutput : KOutput {
	virtual ~IOutput() = default;
	virtual Signals::ISignal<Data>& getSignal() = 0;
	virtual std::shared_ptr<const IMetadata> getMetadata() const = 0;
	virtual bool updateMetadata(Data&) {
		return false;
	};
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
