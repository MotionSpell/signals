#include "tests/tests.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_modules/modules.hpp"
#include "lib_media/in/file.hpp"
#include "lib_media/out/print.hpp"
#include "lib_utils/tools.hpp"
#include <iostream>

using namespace Tests;
using namespace Modules;

unittest("empty param test: File") {
	FileInputConfig fileInputConfig;
	fileInputConfig.filename = ""; // incorrect
	ASSERT_THROWN(loadModule("FileInput", &NullHost, &fileInputConfig));
}

unittest("empty param test: Out::Print") {
	auto p = createModule<Out::Print>(&NullHost, std::cout);
}

unittest("simple param test") {
	FileInputConfig fileInputConfig;
	fileInputConfig.filename = "data/beepbop.mp4";
	auto f = loadModule("FileInput", &NullHost, &fileInputConfig);
}

unittest("print packets size from file: File -> Out::Print") {
	FileInputConfig fileInputConfig;
	fileInputConfig.filename = "data/beepbop.mp4";
	auto f = loadModule("FileInput", &NullHost, &fileInputConfig);
	auto p = createModule<Out::Print>(&NullHost, std::cout);

	ConnectOutputToInput(f->getOutput(0), p->getInput(0));

	f->process();
}
