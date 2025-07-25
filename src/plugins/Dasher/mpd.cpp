#include "mpd.hpp"
#include "lib_utils/os.hpp"
#include "lib_utils/xml.hpp"
#include <cassert>
#include <ctime>

extern const char *g_version;

namespace {
std::string formatInt(int64_t val) {
	char buffer[256];
	snprintf(buffer, sizeof buffer, "%lld", (long long)val);
	return buffer;
}

std::string formatBool(bool val) {
	return val ? "true" : "false";
}

std::string formatDate(int64_t timestamp) {
	auto t = (time_t)timestamp;
	std::tm date = *std::gmtime(&t);

	char buffer[256];
	sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ",
	    1900 + date.tm_year,
	    1 + date.tm_mon,
	    date.tm_mday,
	    date.tm_hour,
	    date.tm_min,
	    date.tm_sec);
	return buffer;
}

std::string formatPeriod(int64_t t) {
	char buffer[256];

	auto const msecs = int(t % 1000);
	t /= 1000;

	auto const secs = int(t % 60);
	t /= 60;

	auto const mins = int(t % 60);
	t /= 60;

	auto const hours = int(t);

	snprintf(buffer, sizeof buffer, "PT%02dH%02dM%d.%03dS", hours, mins, secs, msecs);
	return buffer;
}

Tag mpdToTags(MPD const& mpd) {
	auto tMPD = Tag { "MPD" };
	tMPD["xmlns"] = "urn:mpeg:dash:schema:mpd:2011";
	tMPD["type"] = mpd.dynamic ? "dynamic" : "static";
	tMPD["id"] = mpd.id;
	tMPD["profiles"] = mpd.profiles;
	if (mpd.dynamic)
		tMPD["availabilityStartTime"] = formatDate(mpd.availabilityStartTime/1000);
	tMPD["publishTime"] = formatDate(mpd.publishTime/1000);
	tMPD["minBufferTime"] = formatPeriod(mpd.minBufferTime);
	if (mpd.dynamic)
		tMPD["minimumUpdatePeriod"] = formatPeriod(mpd.minimum_update_period);
	else
		tMPD["mediaPresentationDuration"] = formatPeriod(mpd.mediaPresentationDuration);

	{
		auto tProgramInformation = Tag { "ProgramInformation" };
		tProgramInformation["moreInformationURL"] = "http://signals.gpac-licensing.com";
		{
			auto tCopyright = Tag { "Copyright" };
			tCopyright.content = "Generated by Signals/" + std::string(g_version);
			tProgramInformation.add(tCopyright);
		}
		tMPD.add(tProgramInformation);
	}

	for(auto& period : mpd.periods) {
		auto tPeriod = Tag { "Period" };
		tPeriod["id"] = period.id;
		tPeriod["start"] = formatPeriod(period.startTime);

		if (!mpd.dynamic && period.duration)
			tPeriod["duration"] = formatPeriod(period.duration);

		//Base URLs
		assert(!mpd.baseUrlPrefixes.empty());
		for (auto& baseUrl : mpd.baseUrlPrefixes) {
			auto tBaseUrl = Tag{ "BaseURL" };

			if (!baseUrl.empty()) {
				tBaseUrl["serviceLocation"] = baseUrl;
				tPeriod.add(tBaseUrl);
			}
		}

		for(auto& adaptationSet : period.adaptationSets) {
			auto tAdaptationSet = Tag { "AdaptationSet" };
			tAdaptationSet["segmentAlignment"] = formatBool(adaptationSet.segmentAlignment);
			tAdaptationSet["bitstreamSwitching"] = formatBool(adaptationSet.bitstreamSwitching);

			if (!adaptationSet.lang.empty())
				tAdaptationSet["lang"] = adaptationSet.lang;

			if (!adaptationSet.supplementalProperty.empty()) {
				auto tSupplementalProperty = Tag { "SupplementalProperty" };
				tSupplementalProperty["schemeIdUri"] = "urn:mpeg:dash:srd:2014";
				tSupplementalProperty["value"] = adaptationSet.supplementalProperty;
				tAdaptationSet.add(tSupplementalProperty);
			}

			// segment template common to all adaptation sets
			{
				auto tSegmentTemplate = Tag { "SegmentTemplate" };
				tSegmentTemplate["timescale"] = formatInt(adaptationSet.timescale);
				tSegmentTemplate["duration"] = formatInt(adaptationSet.duration);
				if (mpd.dynamic)
					tSegmentTemplate["startNumber"] = formatInt(0);
				else
					tSegmentTemplate["startNumber"] = formatInt(adaptationSet.startNumber);

				tAdaptationSet.add(tSegmentTemplate);
			}

			for(auto& representation : adaptationSet.representations) {
				auto tRepresentation = Tag { "Representation" };
				tRepresentation["id"] = representation.id;
				tRepresentation["bandwidth"] = formatInt(representation.bandwidth);
				if(representation.audioSamplingRate)
					tRepresentation["audioSamplingRate"] = formatInt(representation.audioSamplingRate);
				if(representation.width)
					tRepresentation["width"] = formatInt(representation.width);
				if(representation.height)
					tRepresentation["height"] = formatInt(representation.height);
				tRepresentation["mimeType"] = representation.mimeType;
				tRepresentation["codecs"] = representation.codecs;
				tRepresentation["startWithSAP"] = formatInt(representation.startWithSAP);

				{
					auto tSegmentTemplate = Tag { "SegmentTemplate" };
					tSegmentTemplate["media"] = representation.media;
					tSegmentTemplate["initialization"] = representation.initialization;
					if (mpd.dynamic) {
						tSegmentTemplate["startNumber"] = formatInt(0);
					} else {
						tSegmentTemplate["startNumber"] = formatInt(adaptationSet.startNumber);

						auto const pto = (mpd.sessionStartTime + period.startTime) * adaptationSet.timescale / 1000;
						if (pto)
							tSegmentTemplate["presentationTimeOffset"] = formatInt(pto);
					}

					if(adaptationSet.entries.size()) {
						auto tSegmentTimeline = Tag { "SegmentTimeline" };

						for(auto& entry : adaptationSet.entries) {
							auto tS = Tag { "S" };
							if(entry.duration)
								tS["d"] = formatInt(entry.duration);
							if(entry.startTime)
								tS["t"] = formatInt(entry.startTime);
							if(entry.repeatCount)
								tS["r"] = formatInt(entry.repeatCount);
							tSegmentTimeline.add(tS);
						}

						tSegmentTemplate.add(tSegmentTimeline);
					}

					tRepresentation.add(tSegmentTemplate);
				}

				tAdaptationSet.add(tRepresentation);
			}

			tPeriod.add(tAdaptationSet);
		}

		tMPD.add(tPeriod);
	}

	return tMPD;
}
}

std::string serializeMpd(MPD const& mpd) {
	return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" + serializeXml(mpdToTags(mpd));
}

