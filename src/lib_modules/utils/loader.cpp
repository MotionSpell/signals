#include "lib_modules/core/module.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/os.hpp"
#include "lib_utils/tools.hpp"

#include <fstream>
#include <memory>
#include <vector>

using namespace std;

namespace Modules {

static string locatePlugin(const char *name) {

  auto const libName = name + string(".smd");

  vector<string> candidatePath = {
        currentDir() + libName, // search in the current directory
        thisExeDir() + libName, // search in the exe's directory
  };

  auto smdPath = getEnvironmentVariable("SIGNALS_SMD_PATH");
  if(!smdPath.empty())
    candidatePath.push_back(string(smdPath) + "/" + libName);

  for(auto path : candidatePath) {
    if(ifstream(path).is_open())
      return path;
  }

  if(Factory::hasModule(name))
    return ""; // load from internal factory

  // not found
  string msg = "Can't create module '";
  msg += name;
  msg += "'. ";

  bool first = true;
  msg += "(Tried:";
  for(auto path : candidatePath) {
    if(!first)
      msg += ',';
    first = false;
    msg += ' ';
    msg += '"';
    msg += path;
    msg += '"';
  }
  msg += ')';

  throw runtime_error(msg);
}

shared_ptr<IModule> loadModule(const char *name, KHost *host, const void *va) {
  auto const libPath = locatePlugin(name);

  // create plugin from our own static (internal) factory
  if(libPath.empty())
    return shared_ptr<IModule>(Factory::instantiateModule(name, host, const_cast<void *>(va)));

  // create plugin from the shared library's factory
  auto lib = shared_ptr<DynLib>(loadLibrary(libPath.c_str()));
  auto func = (decltype(instantiate) *)lib->getSymbol("instantiate");

  auto deleter = [lib](IModule *mod) { delete mod; };
  return shared_ptr<IModule>(func(name, host, const_cast<void *>(va)), deleter);
}

}
