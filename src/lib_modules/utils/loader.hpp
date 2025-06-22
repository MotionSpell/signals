#pragma once
#include "lib_modules/core/module.hpp"

#include <memory>

namespace Modules {
std::shared_ptr<IModule> loadModule(const char* name, KHost* host, const void* cfg);
}

