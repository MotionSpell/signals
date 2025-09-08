#pragma once

#include "lib_modules/utils/helper.hpp"
#include "lib_utils/queue.hpp"

namespace Modules { namespace Utils {

class Recorder : public ModuleS {
  public:
  Recorder(KHost *host);
  void processOne(Data data) override;
  void flush() override;

  Data pop();
  bool tryPop(Data &data);

  private:
  Queue<Data> record;
};

}}
