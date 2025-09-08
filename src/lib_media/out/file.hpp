#pragma once

#include "lib_modules/utils/helper.hpp"

namespace Modules { namespace Out {

class File : public ModuleS {
  public:
  File(KHost *host, std::string const &path);
  ~File();
  void processOne(Data data) override;

  private:
  KHost *const m_host;
  FILE *file;
};

}}
