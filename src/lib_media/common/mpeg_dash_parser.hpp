#pragma once

#include "span.hpp"
#include <memory>
#include <string>
#include <vector>

struct SegmentTemplate {
	bool active = false;
	std::string initialization;
	std::string media;
	int startNumber = 0;
	int duration = 0;
	int timescale = 1;
};

struct DashMpd;
struct Representation;

struct AdaptationSet {
	std::string contentType;
	std::string srd;
	SegmentTemplate segTpl;
	std::vector<Representation> representations;
};

struct Representation {
	const AdaptationSet& set(DashMpd const * const mpd) const;
	std::string id;
	std::string codecs;
	std::string mimeType;
	int setIdx = -1;

	std::string initialization(DashMpd const * const mpd) const;
	std::string media(DashMpd const * const mpd) const;
	int startNumber(DashMpd const * const mpd) const;
	int duration(DashMpd const * const mpd) const;
	int timescale(DashMpd const * const mpd) const;
	SegmentTemplate segTpl;
};

struct DashMpd {
	bool dynamic = false;
	int64_t availabilityStartTime = 0; // in ms
	int64_t publishTime = 0; // in ms
	int64_t periodDuration = 0; // in seconds
	int64_t minUpdatePeriod = 0;
	std::vector<AdaptationSet> sets;
};

std::unique_ptr<DashMpd> parseMpd(span<const char> text);

