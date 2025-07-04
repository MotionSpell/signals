#pragma once

#include <functional>
#include <string>

struct DemuxConfig {
	typedef std::function<int(uint8_t* buf, int bufSize)> ReadFunc;
	std::string url; // may be a file, a remote URL, or a webcam (set "webcam" to list the available devices)
	bool loop = false;
	std::string avformatCustom;
	uint64_t seekTimeInMs = 0;
	std::string formatName;
	ReadFunc func = nullptr;
	int64_t ioTimeoutInMs = 30000; /*0 is infinite*/
	bool preserveInitialData = false; /*default is to dispatch from the time point where all streams have data*/
};
