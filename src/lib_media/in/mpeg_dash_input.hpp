#pragma once

#include <string>
#include "lib_modules/utils/helper.hpp"

struct DashMpd;

namespace Modules {
namespace In {

struct IFilePuller {
	virtual std::string get(std::string url) = 0;
};

class MPEG_DASH_Input : public Module {
	public:
		MPEG_DASH_Input(IFilePuller* filePuller, std::string const &url);
		~MPEG_DASH_Input();
		void process() override;
		bool wakeUp();

	private:
		IFilePuller* const m_source;
		std::unique_ptr<DashMpd> mpd;
};

}
}

