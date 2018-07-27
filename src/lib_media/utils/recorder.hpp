#pragma once

#include "lib_modules/utils/helper.hpp"

namespace Modules {
namespace Utils {

class Recorder : public ModuleS, private LogCap {
	public:
		Recorder();
		void process(Data data) override;
		void flush() override;

		Data pop();

	private:
		Queue<Data> record;
};

}
}
