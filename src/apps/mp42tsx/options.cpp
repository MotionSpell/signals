#include <iostream>
#include "lib_appcommon/options.hpp"
#include "options.hpp"

mp42tsXOptions parseCommandLine(int argc, char const* argv[]) {
	mp42tsXOptions cfg;

	CmdLineOptions opt;

	opt.addFlag("l", "live", &cfg.isLive, "Use live mode");
	opt.add("o", "output", &cfg.output, "Output path (default: 'output.ts')");

	auto files = opt.parse(argc, argv);
	if (files.size() != 1) {
		std::cerr << "Usage: mp42tsx <input.mp4>" << std::endl;
		opt.printHelp();
		throw std::runtime_error("invalid command line");
	}

	cfg.url = files[0];

	return cfg;
}
