#include "mpeg_dash.hpp"
#include "lib_media/common/metadata_file.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/time.hpp"
#include "lib_utils/tools.hpp" // safe_cast, enforce
#include "lib_utils/log_sink.hpp"
#include "lib_utils/format.hpp"
#include <algorithm> //std::max
#include <map>
#include <cassert>

#include "mpd.hpp"

#define DASH_TIMESCALE 1000 // /!\ there are some ms already hardcoded from the GPAC calls
#define MIN_UPDATE_PERIOD_FACTOR 1 //should be 0, but dash.js doesn't support MPDs with no refresh time
#define MIN_BUFFER_TIME_IN_MS_VOD  3000
#define MIN_BUFFER_TIME_IN_MS_LIVE 2000
#define AVAILABILITY_TIMEOFFSET_IN_S 0.0
#define PERIOD_NAME "1"

static auto const g_profiles = "urn:mpeg:dash:profile:isoff-live:2011, http://dashif.org/guidelines/dash264";

using namespace Modules;

namespace {

enum AdaptiveStreamingCommonFlags {
	None = 0,
	SegmentsNotOwned     = 1 << 0, // don't touch files
	PresignalNextSegment = 1 << 1, // speculative, allows prefetching on player side
	ForceRealDurations   = 1 << 2
};

struct Quality {
	std::shared_ptr<const MetadataFile> getMeta() const {
		return lastData ? safe_cast<const MetadataFile>(lastData->getMetadata()) : nullptr;
	};

	uint64_t curSegDurIn180k = 0;
	Data lastData;
	uint64_t avg_bitrate_in_bps = 0;
	std::string prefix; // typically a subdir, ending with a dir separator '/'

	struct PendingSegment {
		uint64_t durationIn180k;
		std::string filename;
	};
	std::vector<PendingSegment> timeshiftSegments;
};

struct AdaptiveStreamer : ModuleDynI {
		virtual void onNewSegment() = 0;
		virtual void onEndOfStream() = 0;

		AdaptiveStreamer(KHost* host, bool live, uint64_t segDurationInMs, const std::string &manifestDir, AdaptiveStreamingCommonFlags flags)
			: m_host(host),
			  live(live),
			  segDurationInMs(segDurationInMs),
			  segDurationIn180k(timescaleToClock(segDurationInMs, 1000)),
			  manifestDir(manifestDir),
			  flags(flags) {
			if ((flags & ForceRealDurations) && !segDurationInMs)
				throw error("Inconsistent parameters: ForceRealDurations flag requires a non-null segment duration.");
			if (!manifestDir.empty() && (flags & SegmentsNotOwned))
				throw error(format("Inconsistent parameters: manifestDir (%s) should be empty when segments are not owned.", manifestDir));
			addInput();
			outputSegments = addOutput();
			outputManifest = addOutput();
		}

		void process() override {
			if (!wasInit && (startTimeInMs == -1)) {
				startTimeInMs = -2;
				qualities.resize(numInputs());
				wasInit = true;
			}

			while(schedule()) { }
		}

		void flush() override {
			while(schedule()) { }
			onEndOfStream();
		}

	protected:
		KHost* const m_host;

		const bool live;
		int64_t startTimeInMs = -1;
		int64_t totalDurationInMs = 0;
		const uint64_t segDurationInMs;
		const uint64_t segDurationIn180k;
		const std::string manifestDir;
		const AdaptiveStreamingCommonFlags flags;
		std::vector<Quality> qualities;
		OutputDefault* outputSegments;
		OutputDefault* outputManifest;
		bool wasInit = false;

		void processInitSegment(Quality const& quality, size_t index) {
			auto const meta = quality.getMeta();
			auto out = clone(quality.lastData);
			std::string initFn = safe_cast<const MetadataFile>(quality.lastData->getMetadata())->filename;

			if (initFn.empty() || (!(flags & SegmentsNotOwned)))
				initFn = manifestDir + getInitName(quality, index);

			auto metaFn = make_shared<MetadataFile>(SEGMENT);
			metaFn->filename = initFn;
			metaFn->mimeType = meta->mimeType;
			metaFn->codecName = meta->codecName;
			metaFn->durationIn180k = meta->durationIn180k;
			metaFn->filesize = meta->filesize;
			metaFn->latencyIn180k = meta->latencyIn180k;
			metaFn->startsWithRAP = meta->startsWithRAP;

			out->setMetadata(metaFn);
			out->setMediaTime(totalDurationInMs, 1000);
			outputSegments->post(out);
		}

		std::string getInitName(Quality const& quality, size_t index) const {
			return getPrefix(quality, index) + "-init.mp4";
		}

		std::string getSegmentName(Quality const& quality, size_t index, const std::string &segmentNumSymbol) const {
			return getPrefix(quality, index) + "-" + segmentNumSymbol + ".m4s";
		}

		std::string getPrefix(Quality const& quality, size_t index) const {
			switch (quality.getMeta()->type) {
			case AUDIO_PKT:    return quality.prefix + format("a_%s", index);
			case VIDEO_PKT:    return quality.prefix + format("v_%s_%sx%s", index, quality.getMeta()->resolution.width, quality.getMeta()->resolution.height);
			case SUBTITLE_PKT: return quality.prefix + format("s_%s", index);
			default: return "";
			}
		}

		uint64_t getCurSegNum() const {
			return (startTimeInMs + totalDurationInMs) / segDurationInMs;
		}

		std::shared_ptr<DataBase> getPresignalledData(uint64_t size, Data data, bool EOS) {
			if (!(flags & PresignalNextSegment)) {
				return clone(data);
			}
			if (!safe_cast<const MetadataFile>(data->getMetadata())->filename.empty() && !EOS) {
				return nullptr;
			}

			static constexpr uint8_t mp4StaticHeader[] = {
				0x00, 0x00, 0x00, 0x18, 's', 't', 'y', 'p',
				'm', 's', 'd', 'h', 0x00, 0x00, 0x00, 0x00,
				'm', 's', 'd', 'h', 'm', 's', 'i', 'x',
			};
			auto constexpr headerSize = sizeof(mp4StaticHeader);
			if (size == 0 && !EOS) {
				auto out = outputSegments->allocData<DataRaw>(0);
				out->buffer->resize(headerSize);
				memcpy(out->buffer->data().ptr, mp4StaticHeader, headerSize);
				return out;
			} else if (data->data().len >= headerSize && !memcmp(data->data().ptr, mp4StaticHeader, headerSize)) {
				auto out = outputSegments->allocData<DataRaw>(0);
				auto const size = (size_t)(data->data().len - headerSize);
				out->buffer->resize(size);
				memcpy(out->buffer->data().ptr, data->data().ptr + headerSize, size);
				return out;
			} else {
				assert(data->data().len < 8 || *(uint32_t*)(data->data().ptr + 4) != (uint32_t)0x70797473);
				return clone(data);
			}
		}

		int numInputs() {
			return getNumInputs() - 1;
		}

		bool isComplete(int repIdx) const {
			uint64_t minIncompletSegDur = std::numeric_limits<uint64_t>::max();
			for (auto& quality : qualities) {
				auto const &segDur = quality.curSegDurIn180k;
				if ( (segDur < minIncompletSegDur) &&
				    ((segDur < segDurationIn180k) || (!quality.getMeta() || !quality.getMeta()->EOS))) {
					minIncompletSegDur = segDur;
				}
			}
			return (minIncompletSegDur == std::numeric_limits<uint64_t>::max()) || (qualities[repIdx].curSegDurIn180k > minIncompletSegDur);
		}

		void ensureStartTime(Data currData) {
			if (startTimeInMs == -2)
				startTimeInMs = clockToTimescale(currData->get<PresentationTime>().time, DASH_TIMESCALE);
		}

		void sendLocalData(Data currData, int repIdx, uint64_t size, bool EOS) {
			ensureStartTime(currData);
			auto out = getPresignalledData(size, currData, EOS);
			if (out) {
				auto const &meta = qualities[repIdx].getMeta();

				auto metaFn = make_shared<MetadataFile>(SEGMENT);
				metaFn->filename = getSegmentName(qualities[repIdx], repIdx, std::to_string(getCurSegNum()));
				metaFn->mimeType = meta->mimeType;
				metaFn->codecName = meta->codecName;
				metaFn->durationIn180k = meta->durationIn180k;
				metaFn->filesize = size;
				metaFn->latencyIn180k = meta->latencyIn180k;
				metaFn->startsWithRAP = meta->startsWithRAP;
				metaFn->EOS = EOS;

				out->setMetadata(metaFn);
				out->setMediaTime(totalDurationInMs + timescaleToClock(qualities[repIdx].curSegDurIn180k, 1000));
				outputSegments->post(out);
			}
		}

		bool segmentReady() {
			for (int i = 0; i < numInputs(); ++i) {
				if (!qualities[0].curSegDurIn180k)
					qualities[0].curSegDurIn180k = segDurationIn180k;
			}
			for (int i = 0; i < numInputs(); ++i) {
				if (qualities[i].curSegDurIn180k < segDurationIn180k) {
					return false;
				}
				if (!qualities[i].getMeta()->EOS) {
					return false;
				}
			}
			return true;
		}

		Data currData; // TODO: completely remove this member

		bool scheduleRepresentation(int repIdx) {
			if (isComplete(repIdx))
				return true;

			if (!inputs[repIdx]->tryPop(currData) || !currData)
				return false;

			auto& quality = qualities[repIdx];

			quality.lastData = currData;
			auto const &meta = quality.getMeta();
			if (!meta)
				throw error(format("Unknown data received on input %s", repIdx));

			if (quality.prefix.empty())
				quality.prefix = format("%s/", getPrefix(quality, repIdx));

			auto const curDurIn180k = meta->durationIn180k;
			if (curDurIn180k == 0 && quality.curSegDurIn180k == 0) {
				processInitSegment(quality, repIdx);
				if (flags & PresignalNextSegment)
					sendLocalData(currData, repIdx, 0, false);
				currData = nullptr;
				return true;
			}

			// update average bitrate
			if (segDurationInMs && curDurIn180k) {
				auto const numSeg = totalDurationInMs / segDurationInMs;
				quality.avg_bitrate_in_bps = ((meta->filesize * 8 * IClock::Rate) / meta->durationIn180k + quality.avg_bitrate_in_bps * numSeg) / (numSeg + 1);
			}

			// update current segment duration
			if (flags & ForceRealDurations) {
				quality.curSegDurIn180k += meta->durationIn180k;
			} else {
				quality.curSegDurIn180k = segDurationIn180k ? segDurationIn180k : meta->durationIn180k;
			}

			if (quality.curSegDurIn180k < segDurationIn180k || !meta->EOS)
				sendLocalData(currData, repIdx, meta->filesize, meta->EOS);

			return true;
		}

		bool schedule() {

			for (int repIdx = 0; repIdx < numInputs(); ++repIdx) {
				if(!scheduleRepresentation(repIdx))
					break;
			}

			if (!currData) {
				return false; // nothing was done
			}

			ensureStartTime(currData);
			currData = nullptr;

			if (segmentReady()) {
				for (auto& quality : qualities)
					quality.curSegDurIn180k -= segDurationIn180k;

				onNewSegment();
				totalDurationInMs += segDurationInMs;
				m_host->log(Info, format("Processes segment (total processed: %ss,", totalDurationInMs / 1000.0).c_str());
			}

			return true;
		}
};

AdaptiveStreamingCommonFlags getFlags(DasherConfig* cfg) {
	uint32_t r = 0;

	if(cfg->segmentsNotOwned)
		r |= SegmentsNotOwned;
	if(cfg->presignalNextSegment)
		r |= PresignalNextSegment;
	if(cfg->forceRealDurations)
		r |= ForceRealDurations;

	return AdaptiveStreamingCommonFlags(r);
}

DasherConfig complementConfig(DasherConfig cfg) {
	if(cfg.minBufferTimeInMs == 0)
		cfg.minBufferTimeInMs = cfg.live ? MIN_BUFFER_TIME_IN_MS_LIVE : MIN_BUFFER_TIME_IN_MS_VOD;

	if(cfg.minUpdatePeriodInMs == 0)
		cfg.minUpdatePeriodInMs = cfg.segDurationInMs ? cfg.segDurationInMs : 1000;

	return cfg;
}

class Dasher : public AdaptiveStreamer {
	public:
		Dasher(KHost* host, DasherConfig* cfg)
			: AdaptiveStreamer(host, cfg->live, cfg->segDurationInMs, cfg->mpdDir, getFlags(cfg)),
			  m_host(host),
			  m_cfg(complementConfig(*cfg)),
			  useSegmentTimeline(m_cfg.segDurationInMs == 0) {
			if (useSegmentTimeline && (m_cfg.presignalNextSegment || m_cfg.segmentsNotOwned))
				throw error("'Next segment pre-signalling' or 'segments not owned' cannot be used with a segment timeline.");
		}

	protected:

		KHost* const m_host;
		DasherConfig const m_cfg;
		const bool useSegmentTimeline = false;

		void postManifest(std::string contents) {
			auto out = outputManifest->allocData<DataRaw>(contents.size());
			auto metadata = make_shared<MetadataFile>(PLAYLIST);
			metadata->filename = manifestDir + m_cfg.mpdName;
			metadata->durationIn180k = segDurationIn180k;
			metadata->filesize = contents.size();
			out->setMetadata(metadata);
			out->setMediaTime(totalDurationInMs, 1000);
			memcpy(out->buffer->data().ptr, contents.data(), contents.size());
			outputManifest->post(out);
		}

		std::string getPrefixedSegmentName(Quality const& quality, size_t index, int64_t segmentNum) const {
			return manifestDir + getSegmentName(quality, index, std::to_string(segmentNum));
		}

		void onNewSegment() {
			auto xml = createManifest(m_cfg);

			if (live)
				postManifest(xml);
		}

		std::string createManifest(DasherConfig m_cfg) {
			MPD mpd {};
			if (live)
				mpd.minimum_update_period = 1000;
			mpd.timeline = useSegmentTimeline;
			mpd.dynamic = m_cfg.live;
			mpd.id = m_cfg.id;
			mpd.profiles = g_profiles;
			mpd.minBufferTime = m_cfg.minBufferTimeInMs;
			mpd.mediaPresentationDuration = totalDurationInMs + segDurationInMs;
			mpd.sessionStartTime = startTimeInMs;
			mpd.availabilityStartTime = segDurationInMs/*time at which the first segment is available*/ + m_cfg.initialOffsetInMs;
			mpd.timeShiftBufferDepth = m_cfg.timeShiftBufferDepthInMs;
			mpd.publishTime = int64_t(m_cfg.utcClock->getTime() * 1000);
			mpd.baseUrls = m_cfg.baseURLs;

			MPD::Period period;
			period.id = PERIOD_NAME;

			std::map<int, MPD::AdaptationSet> adaptationSets;

			for(auto repIdx : getInputs()) {
				auto& quality = qualities[repIdx];
				auto const &meta = quality.getMeta();
				if (!meta)
					continue;

				auto& as = adaptationSets[meta->type];
				as.duration = segDurationInMs;
				as.timescale = DASH_TIMESCALE;
				as.availabilityTimeOffset = AVAILABILITY_TIMEOFFSET_IN_S;

				//FIXME: arbitrary: should be set by the app, or computed
				as.segmentAlignment = true;
				as.bitstreamSwitching = true;

				MPD::Representation rep {};
				rep.id = format("%s", repIdx);
				rep.bandwidth = quality.avg_bitrate_in_bps;

				std::string templateName;
				if (useSegmentTimeline) {
					templateName = "$Time$";
					if (live)
						mpd.minimum_update_period = m_cfg.minUpdatePeriodInMs;
				} else {
					templateName = "$Number$";
					mpd.minimum_update_period = m_cfg.minUpdatePeriodInMs * MIN_UPDATE_PERIOD_FACTOR;
					as.startNumber = startTimeInMs / segDurationInMs;
				}
				rep.mimeType = meta->mimeType;
				rep.codecs = meta->codecName;
				rep.startWithSAP = true;
				if (live && meta->latencyIn180k) {
					as.availabilityTimeOffset = std::max(0.0, (segDurationInMs - clockToTimescale(meta->latencyIn180k, DASH_TIMESCALE)) / 1000.0);
					mpd.minBufferTime = clockToTimescale(meta->latencyIn180k, DASH_TIMESCALE);
				}
				switch (meta->type) {
				case AUDIO_PKT:
					rep.audioSamplingRate = meta->sampleRate;
					break;
				case VIDEO_PKT:
					rep.width = meta->resolution.width;
					rep.height = meta->resolution.height;
					break;
				default: break;
				}

				rep.initialization = getInitName(quality, repIdx);
				rep.media = getSegmentName(quality, repIdx, templateName);

				if (rep.width && !meta->startsWithRAP) /*video only*/
					rep.startWithSAP = false;

				std::string segFilename, nextSegFilename;
				if (useSegmentTimeline) {
					auto prevEnt = as.entries.size() ? &as.entries.back() : nullptr;
					auto const currDur = (int64_t)clockToTimescale(meta->durationIn180k, DASH_TIMESCALE);
					uint64_t segTime = 0;
					if (!prevEnt || prevEnt->duration != currDur) {
						segTime = prevEnt ? prevEnt->startTime + prevEnt->duration*(prevEnt->repeatCount+1) : startTimeInMs;
						MPD::Entry ent {};
						ent.startTime = segTime;
						ent.duration = currDur;
						as.entries.push_back(ent);
					} else {
						prevEnt->repeatCount++;
						segTime = prevEnt->startTime + prevEnt->duration*(prevEnt->repeatCount);
					}

					segFilename = getPrefixedSegmentName(quality, repIdx, segTime);
				} else {
					auto n = getCurSegNum();
					segFilename = getPrefixedSegmentName(quality, repIdx, n);
					if (m_cfg.presignalNextSegment)
						nextSegFilename = getPrefixedSegmentName(quality, repIdx, n + 1);
				}

				postSegment(quality, segFilename, nextSegFilename);

				if (m_cfg.timeShiftBufferDepthInMs)
					deleteOldSegments(quality);

				as.representations.push_back(rep);
			}

			for(auto as : adaptationSets)
				period.adaptationSets.push_back(as.second);

			mpd.periods.push_back(period);

			return serializeMpd(mpd);
		}

		void deleteOldSegments(Quality& quality) {
			int64_t totalDuration = 0;
			auto seg = quality.timeshiftSegments.begin();
			while (seg != quality.timeshiftSegments.end()) {
				totalDuration += clockToTimescale(seg->durationIn180k, DASH_TIMESCALE);
				if (totalDuration > m_cfg.timeShiftBufferDepthInMs) {
					m_host->log(Debug, format( "Delete segment \"%s\".", seg->filename).c_str());

					// send 'DELETE' command
					{
						auto out = outputSegments->allocData<DataRaw>(0);
						auto meta = make_shared<MetadataFile>(SEGMENT);
						meta->filesize = INT64_MAX; // "DELETE"
						meta->filename = seg->filename;
						out->setMetadata(meta);
						outputSegments->post(out);
					}

					seg = quality.timeshiftSegments.erase(seg);
				} else {
					++seg;
				}
			}
		}

		void postSegment(Quality& quality, std::string segFilename, std::string nextSegFilename) {
			auto const &meta = quality.getMeta();
			auto metaFn = make_shared<MetadataFile>(SEGMENT);
			metaFn->filename = segFilename;
			metaFn->mimeType = meta->mimeType;
			metaFn->codecName = meta->codecName;
			metaFn->durationIn180k = meta->durationIn180k;
			metaFn->filesize = meta->filesize;
			metaFn->latencyIn180k = meta->latencyIn180k;
			metaFn->startsWithRAP= meta->startsWithRAP;
			metaFn->sampleRate = meta->sampleRate;
			metaFn->resolution = meta->resolution;

			if (!segFilename.empty()) {
				auto out = getPresignalledData(meta->filesize, quality.lastData, true);
				if (!out)
					throw error("Unexpected null pointer detected while getting data.");
				out->setMetadata(metaFn);
				out->setMediaTime(totalDurationInMs, 1000);
				outputSegments->post(out);

				if (!nextSegFilename.empty()) {
					auto out = getPresignalledData(0, quality.lastData, false);
					if (out) {
						auto meta = make_shared<MetadataFile>(*metaFn);
						meta->filename = nextSegFilename;
						meta->EOS = false;
						out->setMetadata(meta);
						out->setMediaTime(totalDurationInMs, 1000);
						outputSegments->post(out);
					}
				}
			}

			auto s = Quality::PendingSegment{metaFn->durationIn180k, metaFn->filename};
			quality.timeshiftSegments.emplace(quality.timeshiftSegments.begin(), s);
		}

		void onEndOfStream() {
			if (m_cfg.timeShiftBufferDepthInMs) {
				if (!m_cfg.segmentsNotOwned)
					m_host->log(Info, "Manifest was not rewritten for on-demand and all files are being deleted.");
			} else {
				m_host->log(Info, "Manifest rewritten for on-demand. Media files untouched.");

				auto cfg = m_cfg;
				cfg.live = false;
				cfg.minUpdatePeriodInMs = 0;
				totalDurationInMs -= segDurationInMs;
				auto xml = createManifest(cfg);
				postManifest(xml);
			}
		}
};

IModule* createObject(KHost* host, void* va) {
	auto config = (DasherConfig*)va;
	enforce(host, "MPEG_DASH: host can't be NULL");
	enforce(config, "MPEG_DASH: config can't be NULL");
	return createModule<Dasher>(host, config).release();
}

auto const registered = Factory::registerModule("MPEG_DASH", &createObject);
}
