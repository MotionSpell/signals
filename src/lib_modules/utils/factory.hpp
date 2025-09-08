namespace Modules {

struct IModule;
struct KHost;

namespace Factory {
using CreationFunc = IModule *(KHost *, void *);
IModule *instantiateModule(const char *name, KHost *host, void *);
int registerModule(const char *name, CreationFunc *func);
bool hasModule(const char *name);
}

}

///////////////////////////////////////////////////////////////////////////////
// prototype for binary entry-point to the UM shared library
///////////////////////////////////////////////////////////////////////////////
#ifdef _MSC_VER
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
EXPORT Modules::IModule *instantiate(const char *name, Modules::KHost *host, void *cfg);
}
