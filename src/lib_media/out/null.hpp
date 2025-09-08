#pragma once

#include "lib_modules/utils/helper.hpp"

namespace Modules { namespace Out {

// Open bar output. Thread-safe by design �
struct Null : public ModuleS {
  Null(KHost *host);
  void processOne(Data data) override;

  private:
  KHost *const m_host;
};

}}
