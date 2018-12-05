#include "factory.hpp"
#include <cstring> // strcmp
#include <string>
#include <stdexcept>

using namespace std;

namespace Modules {

struct Entry {
	const char* name;
	Factory::CreationFunc* func;
};

static Entry registry[1024];

Entry* findEntry(const char* name) {
	for(auto& entry : registry)
		if(entry.name && strcmp(entry.name, name) == 0)
			return &entry;
	return nullptr;
}

Entry* findFreeEntry() {
	for(auto& entry : registry)
		if(!entry.name)
			return &entry;
	return nullptr;
}

namespace Factory {
int registerModule(const char* name, CreationFunc* func) {

	if(findEntry(name))
		throw runtime_error("Module '" + string(name) + "' is already registered");

	auto entry = findFreeEntry();
	if(!entry)
		throw runtime_error("Too many registered modules");

	entry->name = name;
	entry->func = func;

	return 0;
}

IModule* instantiateModule(const char* name, KHost* host, void* va) {
	auto entry = findEntry(name);
	if(!entry)
		throw runtime_error("Unknown module '" + string(name) + "'");

	return entry->func(host, va);
}
}

}

Modules::IModule* instantiate(const char* name, Modules::KHost* host, void* va) {
	return Modules::Factory::instantiateModule(name, host, va);
}
