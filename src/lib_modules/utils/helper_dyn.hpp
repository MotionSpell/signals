#pragma once

#include <string>

#include "helper.hpp"

namespace Modules {

// dynamic input number specialized module
class ModuleDynI : public Module {
  public:
  int getNumInputs() const override {
    if(inputs.size() == 0)
      return 1;
    else if(!inputs.back()->isConnected())
      return (int)inputs.size();
    else
      return (int)inputs.size() + 1;
  }
  IInput *getInput(int i) override {
    if(i == (int)inputs.size())
      addInput();
    else if(i > (int)inputs.size())
      throw std::runtime_error("Incorrect port number " + std::to_string(i) + " for dynamic input.");

    return inputs[i].get();
  }
  std::vector<int> getInputs() const {
    std::vector<int> r;
    for(int i = 0; i < getNumInputs() - 1; ++i)
      r.push_back(i);
    return r;
  }
};

}
