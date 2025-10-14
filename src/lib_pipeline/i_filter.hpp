#pragma once

// Filters are interconnected pipeline elements.
// They're seen as such by the applications.

#include "lib_modules/modules.hpp"

namespace Pipelines {

enum class Threading {
  Mono = 1,
  OnePerModule = 2,
};

struct IFilter {
  virtual ~IFilter(){};
  virtual int getNumInputs() const = 0;
  virtual int getNumOutputs() const = 0;
  virtual Modules::Metadata getOutputMetadata(int i) = 0;
};

struct InputPin {
  InputPin(IFilter *m, int idx = 0)
      : mod(m)
      , index(idx){};
  IFilter *mod;
  int index = 0;
};

struct OutputPin {
  OutputPin(IFilter *m, int idx = 0)
      : mod(m)
      , index(idx){};
  IFilter *mod;
  int index = 0;
};

inline InputPin GetInputPin(IFilter *mod, int index = 0) { return InputPin{mod, index}; }

inline OutputPin GetOutputPin(IFilter *mod, int index = 0) { return OutputPin{mod, index}; }

struct IEventSink {
  virtual void endOfStream() = 0;

  /*returns true when the exception is handled properly and doesn't require subsequent actions (e.g. stopping the
   * module)*/
  virtual bool exception(std::exception_ptr eptr) = 0;
};

}
