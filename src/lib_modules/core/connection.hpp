#include "module.hpp"

namespace Modules {

typedef Signals::IExecutor<void()> IProcessExecutor;
extern Signals::ExecutorSync<void()> g_executorSync;
#define defaultExecutor g_executorSync

template<typename Class, typename Function>
Signals::MemberFunctor<Class, void(Class::*)()>
Bind(Function func, Class* objectPtr) {
	return Signals::MemberFunctor<Class, void(Class::*)()>(objectPtr, func);
}

void ConnectOutputToInput(IOutput *prev, IInput *next, IProcessExecutor * const executor = &defaultExecutor);
void ConnectModules(IModule *prev, int outputIdx, IModule *next, int inputIdx, IProcessExecutor &executor = defaultExecutor);

}
