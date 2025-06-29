#pragma once

#include <string>
#include <vector>
#include "lib_media/common/resolution.hpp"

struct Video {
	Resolution res;
	int bitrate;
	int type;
};

struct Config {
	std::string input;
	std::string workingDir = ".";
	std::string publishUrl = "";
	std::vector<Video> v;
	std::string logoPath;
	int segmentDurationInMs = 2000;
	int timeshiftInSegNum = 0;
	bool isLive = false;
	bool loop = false;
	bool ultraLowLatency = false;
	bool autoRotate = false;
	bool help = false;
	bool debugMonitor = false;
	bool dumpGraph = false;
};

