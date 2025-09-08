#pragma once

#include "lib_modules/utils/helper.hpp"

#include <ostream>

namespace Modules { namespace Out {

class Print : public ModuleS {
  public:
  Print(KHost *host, std::ostream &os);
  void processOne(Data data) override;

  private:
  KHost *const m_host;
  std::ostream &os;
};

}}
