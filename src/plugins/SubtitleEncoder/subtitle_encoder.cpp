#include "subtitle_encoder.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/subtitle.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/log_sink.hpp" // Debug
#include "lib_utils/time.hpp" // timeInMsToStr
#include "lib_utils/tools.hpp" // enforce
#include "lib_utils/format.hpp"
#include "lib_utils/small_map.hpp"
#include <algorithm> // std::max
#include <cassert>
#include <sstream>

using namespace Modules;

namespace {

auto const DEBUG_DISPLAY_TIMESTAMPS = false;

std::string timecodeToString(int64_t timeInMs) {
	const size_t timecodeSize = 24;
	char timecode[timecodeSize] = { 0 };
	timeInMsToStr(timeInMs, timecode, ".");
	timecode[timecodeSize - 1] = 0;
	return timecode;
}

class SubtitleEncoder : public ModuleS {
	public:
		SubtitleEncoder(KHost* host, SubtitleEncoderConfig* cfg)
			: m_host(host), isWebVTT(cfg->isWebVTT), forceTtmlLegacy(cfg->forceTtmlLegacy), m_utcStartTime(cfg->utcStartTime),
			  forceEmptyPage(cfg->forceEmptyPage), lang(cfg->lang), timingPolicy(cfg->timingPolicy),
			  maxPageDurIn180k(std::max<int>(timescaleToClock(cfg->maxDelayBeforeEmptyInMs, 1000), timescaleToClock(cfg->splitDurationInMs, 1000))),
			  splitDurationIn180k(timescaleToClock(cfg->splitDurationInMs, 1000)) {
			enforce(cfg->utcStartTime != nullptr, "SubtitleEncoder: utcStartTime can't be NULL");
			output = addOutput();
			output->setMetadata(make_shared<MetadataPktSubtitle>());
		}

		void processOne(Data data) override {
			auto page = dynamic_cast<const DataSubtitle*>(data.get());
			enqueuePage(page);

			// TODO
			// 14. add flush() for ondemand samples
			// 15. UTF8 to TTML/WebVTT formatting? accent
			dispatch(data->get<PresentationTime>().time);
		}

	private:
		KHost* const m_host;
		const bool isWebVTT;
		const bool forceTtmlLegacy;
		IUtcStartTimeQuery const * const m_utcStartTime;
		OutputDefault* output;
		const bool forceEmptyPage;
		const std::string lang;
		const SubtitleEncoderConfig::TimingPolicy timingPolicy;
		int64_t intClock = 0;
		const int64_t maxPageDurIn180k, splitDurationIn180k;
		std::vector<Page> currentPages;

		void enqueuePage(const DataSubtitle *page) {
			if (!page)
				return;

			if (!currentPages.empty()) {
				auto &lastPage = currentPages.back();
				if (lastPage.hideTimestamp > page->page.showTimestamp) {
					m_host->log(Info, format("Detected timing overlap. Shortening previous page by %sms (duration was %sms).",
					    clockToTimescale(lastPage.hideTimestamp - page->page.showTimestamp, 1000), clockToTimescale(lastPage.hideTimestamp - lastPage.showTimestamp, 1000)).c_str());
					lastPage.hideTimestamp = page->page.showTimestamp;
				}
			}

			currentPages.push_back(page->page);
		}

		static bool isDisplayable(const Page& page, int64_t startTimeInMs, int64_t endTimeInMs) {
			return clockToTimescale(page.hideTimestamp, 1000) > startTimeInMs && clockToTimescale(page.showTimestamp, 1000) < endTimeInMs;
		};

		std::string serializePageToTtml(Page const& page, int regionId, int64_t startTimeInMs, int64_t endTimeInMs, bool useBr) const {
			auto const timecodeShow = timecodeToString(startTimeInMs);
			auto const timecodeHide = timecodeToString(endTimeInMs);

			assert(!page.lines.empty());

			std::stringstream ttml;

			if (useBr)
				ttml << "      <p region=\"Region" << regionId << "\" begin=\"" << timecodeShow << "\" end=\"" << timecodeHide << "\" style=\"textCenter\">\n";

			int lineIdx = 0;
			for (auto& line : page.lines) {
				if (line.text.empty() && !forceEmptyPage)
					continue;

				if (useBr) {
					ttml << "        <span style=\"text_" << lineIdx++ << "\">" << line.text << "</span>\n";
					if (&line != &page.lines.back())
						ttml << "        <br />\n";
				} else {
					ttml << "      <p region=\"Region" << regionId << "_" << line.region.row << "\" style=\"Style0_0";
					if (line.style.doubleHeight) ttml << "_double";
					ttml << "\" begin=\"" << timecodeShow << "\" end=\"" << timecodeHide << "\">\n";
					ttml << "        <span tts:color=\"" << line.style.color << "\" tts:backgroundColor=\"" << line.style.bgColor << "\">" << line.text << "</span>\n";
					ttml << "      </p>\n";
				}
			}

			if (useBr)
				ttml << "      </p>\n";

			return ttml.str();
		}

		std::string toTTML(int64_t startTimeInMs, int64_t endTimeInMs) const {
			int64_t offsetInMs;
			switch (timingPolicy) {
			case SubtitleEncoderConfig::AbsoluteUTC:
				offsetInMs = clockToTimescale(m_utcStartTime->query(), 1000);
				break;
			case SubtitleEncoderConfig::RelativeToMedia:
				offsetInMs = 0;
				break;
			case SubtitleEncoderConfig::RelativeToSplit:
				offsetInMs = -1 * startTimeInMs;
				break;
			default: throw error("Unknown timing policy (1)");
			}

			//TODO: remove legacy mode
			auto isLegacy = [&]() {
				if (DEBUG_DISPLAY_TIMESTAMPS)
					return false;

				for (auto& page : currentPages) {
					if (page.numRows != 25 || page.numCols != 40)
						return false;

					for (auto& line : page.lines)
						if (line.style.fontFamily != "monospaceSansSerif")
							return false;
				}

				return true;
			};
			auto legacyElementalMode = forceTtmlLegacy || isLegacy();

			auto hasDoubleHeight = [&]() {
				for (auto& page : currentPages)
					for (auto& line : page.lines)
						if (line.style.doubleHeight)
							return true;

				return false;
			};

			SmallMap<const Page*, int> pageToRegionId;

			std::stringstream ttml;
			ttml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

			if (legacyElementalMode) {
				ttml << "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:tt=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" xmlns:tts=\"http://www.w3.org/ns/ttml#styling\" xmlns:ttp=\"http://www.w3.org/ns/ttml#parameter\" xml:lang=\"" << lang << "\" ttp:cellResolution=\"50 30\" >\n";
				ttml << "  <head>\n";
				ttml << "    <styling>\n";
				if (!hasDoubleHeight())
					ttml << "      <style xml:id=\"Style0_0\" tts:fontSize=\"100%\" tts:fontFamily=\"monospaceSansSerif\" />\n";
				else
					ttml << "      <style xml:id=\"Style0_0_double\" tts:fontSize=\"100%\" tts:fontFamily=\"monospaceSansSerif\" />\n";
				ttml << "    </styling>\n";
				ttml << "    <layout>\n";

				// We currently assign one Region per line per page with positioning aligned on a 40x25 grid
				// Single or double height
				bool doubleHeight = false;
				for (auto& page : currentPages) {
					if (!isDisplayable(page, startTimeInMs, endTimeInMs))
						continue;

					pageToRegionId[&page] = pageToRegionId.size();

					for (auto& line : page.lines) {
						if (line.text.empty() && !forceEmptyPage)
							continue;

						if (line.style.doubleHeight) {
							doubleHeight = true;
						} else if (doubleHeight) {
							m_host->log(Warning, "Mixing single and double height is not handled. Contact your vendor.");
						}

						auto parsePercent = [this](const std::string &str) {
							int percent = 0;
							int ret = sscanf(str.c_str(), "%d", &percent);
							if (ret != 1)
								m_host->log(Warning, format("Could not parse percent in \"%s\".", str).c_str());
							return Fraction(percent, 100);
						};

						auto const height = Fraction(100.0, page.numRows - 1);
						auto const spacingFactor = parsePercent(line.style.lineHeight);
						auto const verticalOrigin = (double)((height * spacingFactor * page.numRows * -1 + 100) + height * spacingFactor * line.region.row); // Percentage. Promote the last rows for text display.
						if (verticalOrigin >= 0 && height * spacingFactor * (1 + (int)doubleHeight) + verticalOrigin <= 100) {
							auto const factorH = 1 + (int)doubleHeight;
							auto const margin = 10.0; //percentage
							auto const origin = (margin + (100 - 2 * margin) * line.region.col / (double)page.numCols) / factorH;
							auto const width = 100 - origin - margin;
							ttml << "      <region xml:id=\"Region" << pageToRegionId[&page] << "_" << line.region.row << "\" ";
							ttml << "tts:origin=\"" << origin << "% " << verticalOrigin << "%\" tts:extent=\"" << width << "% " << (double)height * factorH << "%\" ";
							ttml << "tts:displayAlign=\"center\" tts:textAlign=\"center\" />\n";
						} else if (forceTtmlLegacy) {
							ttml << "      <region xml:id=\"Region" << pageToRegionId[&page] << "_" << line.region.row << "\" tts:origin=\"10% 95.8333%\" tts:extent=\"80% 4.16667%\" tts:displayAlign=\"center\" tts:textAlign=\"center\" />\n";
						} else
							m_host->log(Warning, format("Impossible to compute text position for \"%s\". Contact your vendor.", line.text).c_str());
					}
				}

				ttml << "    </layout>\n";
				ttml << "  </head>\n";
				ttml << "  <body>\n";
				ttml << "    <div>\n";
			} else {
				if (currentPages.empty())
					ttml << "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:tt=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" xmlns:tts=\"http://www.w3.org/ns/ttml#styling\" xmlns:ttp=\"http://www.w3.org/ns/ttml#parameter\" xml:lang=\"" << lang << "\" ttp:cellResolution=\"50 30\" >\n";
				else
					ttml << "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:tt=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" xmlns:tts=\"http://www.w3.org/ns/ttml#styling\" xmlns:ttp=\"http://www.w3.org/ns/ttml#parameter\" xml:lang=\"" << lang << "\" ttp:cellResolution=\"" << currentPages.front().numCols << " " << currentPages.front().numRows << "\" >\n";
				ttml << "  <head>\n";
				ttml << "    <styling>\n";
				ttml << "      <style xml:id=\"defaultStyle\" tts:fontFamily=\"Verdana, Arial, Tiresias\" tts:fontSize=\"160%\" tts:lineHeight=\"125%\" />\n";
				for (auto& page : currentPages) {
					pageToRegionId[&page] = 0;
					int textIdx = 0;
					for (auto& line : page.lines) {
						ttml << "      <style xml:id=\"text_" << textIdx++ << "\" tts:color=\"" << line.style.color << "\" tts:backgroundColor=\"" << line.style.bgColor << "\" />\n";
					}
					ttml << "      <style xml:id=\"textCenter\" tts:textAlign=\"center\" />\n";
				}
				ttml << "    </styling>\n";
				ttml << "    <layout>\n";
				ttml << "      <region xml:id=\"Region0" << "\" tts:origin=\"10% 10%\" tts:extent=\"80% 80%\" tts:displayAlign=\"after\" />\n";
				ttml << "    </layout>\n";
				ttml << "  </head>\n";
				ttml << "  <body>\n";
				ttml << "    <div style=\"defaultStyle\">\n";
			}

			for(auto& page : currentPages) {
				if (isDisplayable(page, startTimeInMs, endTimeInMs)) {
					auto localStartTimeInMs = std::max<int64_t>(clockToTimescale(page.showTimestamp, 1000), startTimeInMs);
					auto localEndTimeInMs = std::min<int64_t>(clockToTimescale(page.hideTimestamp, 1000), endTimeInMs);
					m_host->log(Info, format("[TTML][%s-%s]: %s - %s: \"%s\"", startTimeInMs, endTimeInMs, localStartTimeInMs, localEndTimeInMs, page.toString()).c_str());
					ttml << serializePageToTtml(page, pageToRegionId[&page], localStartTimeInMs + offsetInMs, localEndTimeInMs + offsetInMs, !legacyElementalMode);
				}
			}
			ttml << "    </div>\n";
			ttml << "  </body>\n";
			ttml << "</tt>\n\n";

			if(0)
				m_host->log(Warning, ttml.str().c_str());

			return ttml.str();
		}

		std::string toWebVTT(int64_t startTimeInMs, int64_t endTimeInMs) const {
			std::stringstream vtt;
			vtt << "WEBVTT\n";
			vtt << "X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0\n";
			vtt << "\n";

			for(auto& page : currentPages) {
				if (isDisplayable(page, startTimeInMs, endTimeInMs)) {
					auto localStartTimeInMs = std::max<int64_t>(clockToTimescale(page.showTimestamp, 1000), startTimeInMs);
					auto localEndTimeInMs = std::min<int64_t>(clockToTimescale(page.hideTimestamp, 1000), endTimeInMs);
					m_host->log(Info, format("[WebVTT][%s-%s]: %s - %s: %s", startTimeInMs, endTimeInMs, localStartTimeInMs, localEndTimeInMs, page.toString()).c_str());

					auto const timecodeShow = timecodeToString(localStartTimeInMs);
					auto const timecodeHide = timecodeToString(localEndTimeInMs);
					vtt << timecodeShow << " --> " << timecodeHide << "\n";

					for (auto& line : page.lines) {
						if (line.text.empty())
							continue;

						vtt << line.text << "\n";
					}
				}

				vtt << "\n";
			}

			return vtt.str();
		}

		void removeOutdatedPages(int64_t endTimeInMs) {
			auto page = currentPages.begin();
			while(page != currentPages.end()) {
				if(clockToTimescale(page->hideTimestamp, 1000) <= endTimeInMs) {
					page = currentPages.erase(page);
				} else {
					++page;
				}
			}
		}

		void sendSample(const std::string& sample) {
			auto out = output->allocData<DataRaw>(sample.size());
			out->set(DecodingTime{ intClock });
			out->set(PresentationTime{ intClock });

			CueFlags flags {};
			flags.keyframe = true;
			out->set(flags);

			memcpy(out->buffer->data().ptr, (uint8_t*)sample.c_str(), sample.size());
			output->post(out);
		}

		void dispatch(int64_t extClock) {
			int64_t prevSplit = (intClock / splitDurationIn180k) * splitDurationIn180k;
			int64_t nextSplit = prevSplit + splitDurationIn180k;

			while(extClock - maxPageDurIn180k >= nextSplit) {
				auto const startInMs = clockToTimescale(prevSplit, 1000);
				auto const endInMs = clockToTimescale(nextSplit, 1000);

				if (DEBUG_DISPLAY_TIMESTAMPS) {
					Page pageOut;
					pageOut.showTimestamp = prevSplit;
					pageOut.hideTimestamp = nextSplit;
					for (int row = ((isWebVTT || forceTtmlLegacy) ? pageOut.numRows - 1 : 0); row < pageOut.numRows; ++row) {
						std::string line;
						line += "Lp ";
						line += std::to_string(row);
						line += std::string("[ ") + timecodeToString(startInMs) + " - " + timecodeToString(endInMs) + " ] ";
						for (int pos = line.length(); pos < pageOut.numCols; ++pos)
							line.push_back('X');

						Page::Line pageline;
						pageline.text = line;
						pageline.region.row = row;
						pageOut.lines.push_back(pageline);
					}
					currentPages.clear();
					currentPages.push_back(pageOut);
				}

				if (currentPages.empty() && forceEmptyPage) {
					Page page;
					page.showTimestamp = prevSplit;
					page.hideTimestamp = nextSplit;
					page.lines.push_back({});
					currentPages.push_back(page);
				}

				sendSample(isWebVTT ? toWebVTT(startInMs, endInMs) : toTTML(startInMs, endInMs));
				removeOutdatedPages(endInMs);

				intClock = nextSplit;
				prevSplit = (intClock / splitDurationIn180k) * splitDurationIn180k;
				nextSplit = prevSplit + splitDurationIn180k;
			}
		}
};

IModule* createObject(KHost* host, void* va) {
	auto config = (SubtitleEncoderConfig*)va;
	enforce(host, "SubtitleEncoder: host can't be NULL");
	enforce(config, "SubtitleEncoder: config can't be NULL");
	return createModule<SubtitleEncoder>(host, config).release();
}

auto const registered = Factory::registerModule("SubtitleEncoder", &createObject);
}
