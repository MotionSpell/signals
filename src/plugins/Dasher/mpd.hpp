#pragma once
#include <string>
#include <vector>

struct MPD {
	struct Representation {
		std::string id;
		std::string initialization, media;
		int bandwidth;
		std::string mimeType;
		std::string codecs;
		bool startWithSAP;
		int audioSamplingRate, width, height;
	};

	struct Entry {
		int64_t startTime, duration, repeatCount;
	};

	struct AdaptationSet {
		std::vector<Representation> representations;

		std::vector<Entry> entries;
		int startNumber;
		int duration;
		int timescale;
		int64_t availabilityTimeOffset;
		bool segmentAlignment;
		bool bitstreamSwitching;
		std::string lang;
		std::string supplementalProperty;
	};

	struct Period {
		std::string id;
		int64_t startTime;
		int64_t duration;
		std::vector<AdaptationSet> adaptationSets;
	};

	bool dynamic;
	bool timeline;
	int64_t mediaPresentationDuration;
	int64_t sessionStartTime;
	int64_t availabilityStartTime;
	int64_t timeShiftBufferDepth;
	int64_t minBufferTime;
	int64_t publishTime;
	int64_t minimum_update_period;
	std::string profiles;
	std::string id;
	std::vector<std::string> baseUrlPrefixes;
	std::vector<Period> periods;
};

std::string serializeMpd(MPD const& mpd);

