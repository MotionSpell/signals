#include "mpeg_dash_parser.hpp"
#include "sax_xml_parser.hpp"
#include <lib_utils/time.hpp>
#include <algorithm> // max
#include <stdexcept>

using namespace std;

int64_t parseIso8601Period(string input);

const AdaptationSet& Representation::set(DashMpd const * const mpd) const {
	return mpd->sets[setIdx];
}

std::string Representation::initialization(DashMpd const * const mpd) const {
	if (segTpl.active)
		return segTpl.initialization;
	else
		return set(mpd).segTpl.initialization;
}
std::string Representation::media(DashMpd const * const mpd) const {
	if (segTpl.active)
		return segTpl.media;
	else
		return set(mpd).segTpl.media;
}
int Representation::startNumber(DashMpd const * const mpd) const {
	if (segTpl.active)
		return segTpl.startNumber;
	else
		return set(mpd).segTpl.startNumber;
}
int Representation::duration(DashMpd const * const mpd) const {
	if (segTpl.active)
		return segTpl.duration;
	else
		return set(mpd).segTpl.duration;
}
int Representation::timescale(DashMpd const * const mpd) const {
	if (segTpl.active)
		return segTpl.timescale;
	else
		return set(mpd).segTpl.timescale;
}

unique_ptr<DashMpd> parseMpd(span<const char> text) {
	auto mpd = make_unique<DashMpd>();

	auto onNodeStart = [&mpd](string name, map<string, string>& attr) {
		if(name == "AdaptationSet") {
			AdaptationSet set;
			set.contentType = attr["contentType"];
			mpd->sets.push_back(set);
		} else if(name == "Period") {
			if(!attr["duration"].empty())
				mpd->periodDuration = parseIso8601Period(attr["duration"]);
		} else if(name == "MPD") {
			mpd->dynamic = attr["type"] == "dynamic";

			if(!attr["availabilityStartTime"].empty())
				mpd->availabilityStartTime = parseDate(attr["availabilityStartTime"]);

			if(!attr["publishTime"].empty())
				mpd->publishTime = parseDate(attr["publishTime"]);

			if(!attr["minimumUpdatePeriod"].empty())
				mpd->minUpdatePeriod = parseIso8601Period(attr["minimumUpdatePeriod"]);
		} else if(name == "SegmentTemplate") {
			auto getSegTpl = [&]() -> SegmentTemplate& {
				auto &set = mpd->sets.back();

				if (set.representations.empty())
					return set.segTpl;

				auto &rep = set.representations.back();
				rep.segTpl = set.segTpl;
				return rep.segTpl;
			};

			auto &segTpl = getSegTpl();
			segTpl.active = true;

			if(attr.find("initialization") != attr.end())
				segTpl.initialization = attr["initialization"];

			if(attr.find("media") != attr.end())
				segTpl.media = attr["media"];

			int startNumber = atoi(attr["startNumber"].c_str());
			segTpl.startNumber = std::max<int>(segTpl.startNumber, startNumber);
			if(attr.find("duration") != attr.end())
				segTpl.duration = atoi(attr["duration"].c_str());
			if(!attr["timescale"].empty())
				segTpl.timescale = atoi(attr["timescale"].c_str());
		} else if(name == "Representation") {
			Representation rep;
			rep.id = attr["id"];
			rep.codecs = attr["codecs"];
			rep.mimeType = attr["mimeType"];
			rep.setIdx = mpd->sets.size() - 1;

			auto &set = mpd->sets.back();
			set.representations.push_back(rep);
		} else if(name == "SupplementalProperty") {
			if(attr.find("schemeIdUri") != attr.end()) {
				if(attr["schemeIdUri"] == "urn:mpeg:dash:srd:2014") {
					if(attr.find("value") != attr.end()) {
						auto &set = mpd->sets.back();
						set.srd = attr["value"];
					}
				}
			}
		}
	};

	saxParse(text, onNodeStart, [](string, string) {});

	return mpd;
}

