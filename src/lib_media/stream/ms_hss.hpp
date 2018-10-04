#pragma once

#include "lib_modules/core/module.hpp"
#include "../out/http.hpp"

namespace Modules {
namespace Stream {

class MS_HSS : public Module, private Out::HTTP::Controller {
	public:
		MS_HSS(IModuleHost* host, const std::string &url);

	private:
		void newFileCallback(span<uint8_t> out) final; //remove ftyp/moov

		std::unique_ptr<Out::HTTP> m_http;
		IModuleHost* const m_host;
};

}
}
