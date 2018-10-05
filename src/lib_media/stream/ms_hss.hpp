#pragma once

#include "lib_modules/core/module.hpp"
#include "../out/http.hpp"

namespace Modules {
namespace Stream {

class MS_HSS : public ModuleS {
	public:
		MS_HSS(IModuleHost* host, const std::string &url);

		void process(Data data) override;
		void flush() override;

	private:
		std::unique_ptr<Out::HTTP> m_http;
		IModuleHost* const m_host;
};

}
}
