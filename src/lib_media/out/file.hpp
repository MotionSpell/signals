#pragma once

#include "lib_modules/utils/helper.hpp"

namespace Modules {
namespace Out {

class File : public ModuleS {
	public:
		File(IModuleHost* host, std::string const& path);
		~File();
		void process(Data data) override;

	private:
		IModuleHost* const m_host;
		FILE *file;
};

}
}
