#pragma once

#include <cstdint>

struct PresentationTime {
	enum { TypeId = 0x35A12022 };
	int64_t time;
};

struct DecodingTime {
	enum { TypeId = 0x5DF434D0 };
	int64_t time;
};

struct CueFlags {
	enum { TypeId = 0x172C1D4F };
	bool discontinuity;
	bool keyframe;
	bool unframed; // needs reframing
};

