#include "lib_appcommon/options.hpp"
#include "lib_appcommon/timebomb.hpp"
#include "lib_utils/profiler.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log.hpp" // setGlobalLogLevel
#include "lib_utils/format.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "config.hpp"
#include <cstdio> // sscanf

const char *g_appName = "dashcastx";

static void parseValue(Video& var, ArgQueue& args) {
	auto word = safePop(args);

	auto const parsed = sscanf(word.c_str(), "%dx%d:%d:%d",
	        &var.res.width, &var.res.height, &var.bitrate, &var.type);

	if (parsed < 2 || parsed > 4) //bitrate is optional
		throw std::runtime_error("Invalid video format, expected WxH[:b[:t]] format (e.g 1920x1080:128)");
}

namespace {
Config parseCommandLine(int argc, char const* argv[]) {

	Config cfg;

	int logLevel = -1;

	CmdLineOptions opt;
	opt.add("o", "output-dir", &cfg.workingDir, "Set the destination directory.");
	opt.add("p", "publish", &cfg.publishUrl, "Publication URL (HTTP POST)");
	opt.add("s", "seg-dur", &cfg.segmentDurationInMs, "Set the segment duration (in ms) (default value: 2000).");
	opt.add("t", "dvr", &cfg.timeshiftInSegNum, "Set the timeshift buffer depth in segment number (default value: infinite(0)).");
	opt.add("v", "video", &cfg.v, "Set a video resolution and optionally bitrate (wxh[:b[:t]]) (enables resize and/or transcoding) and encoder type (supported 0 (software (default)), 1 (QuickSync), 2 (NVEnc).");
	opt.add("g", "loglevel", &logLevel, "Log level");
	opt.add("y", "logo", &cfg.logoPath, "Path to a logo file that will be overlayed on the picture.");
	opt.addFlag("u", "ultra-low-latency", &cfg.ultraLowLatency, "Lower the latency as much as possible (quality may be degraded).");
	opt.addFlag("r", "autorotate", &cfg.autoRotate, "Auto-rotate if the input height is bigger than the width.");
	opt.addFlag("h", "help", &cfg.help, "Print usage and exit.");
	opt.addFlag("l", "live", &cfg.isLive, "Run at system clock pace (otherwise runs as fast as possible).");
	opt.addFlag("i", "loop", &cfg.loop, "Loops the input indefinitely.");
	opt.addFlag("m", "monitor", &cfg.debugMonitor, "Show the transcoded video in monitor window (slows the transcoding down to live speed)");
	opt.addFlag("d", "dump", &cfg.dumpGraph, "Dump the processing graph as .dot file");

	auto args = opt.parse(argc, argv);

	if(cfg.help) {
		printf("Usage: %s [options] <URL>\nOptions:\n", g_appName);

		auto name = std::string(g_appName);
		opt.printHelp();
		auto const examples =
		    "\nExamples:\n"
		    "No transcode:\n"
		    "  " + name + " -l -s 10000 file.mp4\n"
		    "  " + name + " file.ts\n"
		    "  " + name + " udp://226.0.0.1:1234\n"
		    "Transcode:\n"
		    "  " + name + " --live --loop --seg-dur 10000 --dvr 10 --autorotate --video 320x180:50000 --video 640x360:300000 http://server.com/file.mp4\n"
		    "  " + name + " --live -v 1280x720:1000000 webcam:video=/dev/video0:audio=/dev/audio1\n"
		    "  " + name + " --live --working-dir workdir -v 640x360:300000 -v 1280x720:1000000 webcam:video=/dev/video0:audio=/dev/audio1\n"
		    "  " + name + " -i -l -r -w tmp -t 10 -v 640x360:300000:0 udp://226.0.0.1:1234\n";
		printf("%s\n", examples.c_str());

		return cfg;
	}

	if(args.size() != 1)
		throw std::runtime_error("Must give exactly one input file");

	cfg.input = args[0];

	if(logLevel != -1)
		setGlobalLogLevel((Level)logLevel);

	return cfg;
}
}

extern std::unique_ptr<Pipelines::Pipeline> buildPipeline(const Config &config);
static Pipelines::Pipeline *g_Pipeline = nullptr;

void safeMain(int argc, const char* argv[]) {
	auto config = parseCommandLine(argc, argv);
	if(config.help)
		return;
	auto pipeline = buildPipeline(config);
	if(config.dumpGraph)
		printf("%s\n", pipeline->dump().c_str());

	g_Pipeline = pipeline.get();

	Tools::Profiler profilerProcessing(format("%s - processing time", g_appName));
	pipeline->start();
	pipeline->waitForEndOfStream();
}

void safeStop() {
	if (g_Pipeline) {
		g_Pipeline->exitSync();
		g_Pipeline = nullptr;
	}
}

