#include "mpeg_dash.hpp"
#include "adaptive_streaming_common.hpp"
#include "../common/gpacpp.hpp"
#include "../common/libav.hpp"
#include "../common/metadata_file.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/time.hpp"

#define DASH_TIMESCALE 1000 // /!\ there are some ms already hardcoded from the GPAC calls
#define MIN_UPDATE_PERIOD_FACTOR 1 //should be 0, but dash.js doesn't support MPDs with no refresh time
#define MIN_BUFFER_TIME_IN_MS_VOD  3000
#define MIN_BUFFER_TIME_IN_MS_LIVE 2000
#define AVAILABILITY_TIMEOFFSET_IN_S 0.0
#define PERIOD_NAME "1"

static auto const g_profiles = "urn:mpeg:dash:profile:isoff-live:2011, http://dashif.org/guidelines/dash264";

namespace Modules {

namespace {
GF_MPD_AdaptationSet *createAS(uint64_t segDurationInMs, GF_MPD_Period *period, gpacpp::MPD *mpd) {
	auto as = mpd->addAdaptationSet(period);
	GF_SAFEALLOC(as->segment_template, GF_MPD_SegmentTemplate);
	as->segment_template->duration = segDurationInMs;
	as->segment_template->timescale = DASH_TIMESCALE;
	as->segment_template->availability_time_offset = AVAILABILITY_TIMEOFFSET_IN_S;

	//FIXME: arbitrary: should be set by the app, or computed
	as->segment_alignment = GF_TRUE;
	as->bitstream_switching = GF_TRUE;

	return as;
}

std::unique_ptr<gpacpp::MPD> createMPD(bool live, uint32_t minBufferTimeInMs, const std::string &id) {
	return live ?
	    make_unique<gpacpp::MPD>(GF_MPD_TYPE_DYNAMIC, id, g_profiles, minBufferTimeInMs ? minBufferTimeInMs : MIN_BUFFER_TIME_IN_MS_LIVE) :
	    make_unique<gpacpp::MPD>(GF_MPD_TYPE_STATIC, id, g_profiles, minBufferTimeInMs ? minBufferTimeInMs : MIN_BUFFER_TIME_IN_MS_VOD );
}
}


namespace Stream {

AdaptiveStreamingCommon::Type getType(DasherConfig* cfg) {
	if(!cfg->live)
		return AdaptiveStreamingCommon::Static;
	else if(cfg->blocking)
		return AdaptiveStreamingCommon::Live;
	else
		return AdaptiveStreamingCommon::LiveNonBlocking;
}

AdaptiveStreamingCommon::AdaptiveStreamingCommonFlags getFlags(DasherConfig* cfg) {
	uint32_t r = 0;

	if(cfg->segmentsNotOwned)
		r |= AdaptiveStreamingCommon::SegmentsNotOwned;
	if(cfg->presignalNextSegment)
		r |= AdaptiveStreamingCommon::PresignalNextSegment;
	if(cfg->forceRealDurations)
		r |= AdaptiveStreamingCommon::ForceRealDurations;

	return AdaptiveStreamingCommon::AdaptiveStreamingCommonFlags(r);
}

class MPEG_DASH : public AdaptiveStreamingCommon, public gpacpp::Init {
	public:
		MPEG_DASH(IModuleHost* host, DasherConfig* cfg)
			: AdaptiveStreamingCommon(host, getType(cfg), cfg->segDurationInMs, cfg->mpdDir, getFlags(cfg)),
			  m_host(host),
			  mpd(createMPD(cfg->live, cfg->minBufferTimeInMs, cfg->id)), mpdFn(cfg->mpdName), baseURLs(cfg->baseURLs),
			  minUpdatePeriodInMs(cfg->minUpdatePeriodInMs ? cfg->minUpdatePeriodInMs : (segDurationInMs ? cfg->segDurationInMs : 1000)),
			  timeShiftBufferDepthInMs(cfg->timeShiftBufferDepthInMs), initialOffsetInMs(cfg->initialOffsetInMs), useSegmentTimeline(cfg->segDurationInMs == 0) {
			if (useSegmentTimeline && ((flags & PresignalNextSegment) || (flags & SegmentsNotOwned)))
				throw error("Next segment pre-signalling or segments not owned cannot be used with segment timeline.");
		}

		virtual ~MPEG_DASH() {
			endOfStream();
		}

	private:
		struct DASHQuality : public Quality {
			GF_MPD_Representation *rep = nullptr;
			struct SegmentToDelete {
				SegmentToDelete(std::shared_ptr<const MetadataFile> file) : file(file) {}
				std::shared_ptr<const MetadataFile> file;
				int retry = 5;
			};
			std::vector<SegmentToDelete> timeshiftSegments;
		};

		IModuleHost* const m_host;
		std::unique_ptr<gpacpp::MPD> mpd;
		const std::string mpdFn;
		const std::vector<std::string> baseURLs;
		const uint64_t minUpdatePeriodInMs, timeShiftBufferDepthInMs;
		const int64_t initialOffsetInMs;
		const bool useSegmentTimeline = false;

		std::unique_ptr<Quality> createQuality() const {
			return make_unique<DASHQuality>();
		}

		void ensureManifest() {
			if (!mpd->mpd->availabilityStartTime) {
				mpd->mpd->availabilityStartTime = startTimeInMs + initialOffsetInMs;
				mpd->mpd->time_shift_buffer_depth = (u32)timeShiftBufferDepthInMs;
			}
			mpd->mpd->publishTime = int64_t(getUTC() * 1000);

			if ((type == LiveNonBlocking) && (mpd->mpd->media_presentation_duration == 0)) {
				auto mpdOld = std::move(mpd);
				mpd = createMPD(type, mpdOld->mpd->min_buffer_time, mpdOld->mpd->ID);
				mpd->mpd->availabilityStartTime = mpdOld->mpd->availabilityStartTime;
				mpd->mpd->time_shift_buffer_depth = mpdOld->mpd->time_shift_buffer_depth;
			}

			if (!baseURLs.empty()) {
				auto mpdBaseURL = gf_list_new();
				if (!mpdBaseURL)
					throw error("Can't allocate mpdBaseURL with gf_list_new()");
				for (auto const &baseURL : baseURLs) {
					GF_MPD_BaseURL *url;
					GF_SAFEALLOC(url, GF_MPD_BaseURL);
					url->URL = gf_strdup(baseURL.c_str());
					gf_list_add(mpdBaseURL, url);
				}
				mpd->mpd->base_URLs = mpdBaseURL;
			}

			if (!gf_list_count(mpd->mpd->periods)) {
				auto period = mpd->addPeriod();
				period->ID = gf_strdup(PERIOD_NAME);
				GF_MPD_AdaptationSet *audioAS = nullptr, *videoAS = nullptr;
				for(auto i : getInputs()) {
					GF_MPD_AdaptationSet *as = nullptr;
					auto quality = safe_cast<DASHQuality>(qualities[i].get());
					auto const &meta = quality->getMeta();
					if (!meta) {
						continue;
					}
					switch (meta->type) {
					case AUDIO_PKT: as = audioAS ? audioAS : audioAS = createAS(segDurationInMs, period, mpd.get()); break;
					case VIDEO_PKT: as = videoAS ? videoAS : videoAS = createAS(segDurationInMs, period, mpd.get()); break;
					case SUBTITLE_PKT: as = createAS(segDurationInMs, period, mpd.get()); break;
					default: assert(0);
					}

					auto const repId = format("%s", i);
					auto rep = mpd->addRepresentation(as, repId.c_str(), (u32)quality->avg_bitrate_in_bps);
					quality->rep = rep;
					GF_SAFEALLOC(rep->segment_template, GF_MPD_SegmentTemplate);
					std::string templateName;
					if (useSegmentTimeline) {
						GF_SAFEALLOC(rep->segment_template->segment_timeline, GF_MPD_SegmentTimeline);
						rep->segment_template->segment_timeline->entries = gf_list_new();
						templateName = "$Time$";
						if (mpd->mpd->type == GF_MPD_TYPE_DYNAMIC) {
							mpd->mpd->minimum_update_period = (u32)minUpdatePeriodInMs;
						}
					} else {
						templateName = "$Number$";
						mpd->mpd->minimum_update_period = (u32)minUpdatePeriodInMs * MIN_UPDATE_PERIOD_FACTOR;
						rep->segment_template->start_number = (u32)(startTimeInMs / segDurationInMs);
					}
					rep->mime_type = gf_strdup(meta->mimeType.c_str());
					rep->codecs = gf_strdup(meta->codecName.c_str());
					rep->starts_with_sap = GF_TRUE;
					if (mpd->mpd->type == GF_MPD_TYPE_DYNAMIC && meta->latencyIn180k) {
						rep->segment_template->availability_time_offset = std::max<double>(0.0,  (double)(segDurationInMs - clockToTimescale(meta->latencyIn180k, 1000)) / 1000);
						mpd->mpd->min_buffer_time = (u32)clockToTimescale(meta->latencyIn180k, 1000);
					}
					switch (meta->type) {
					case AUDIO_PKT:
						rep->samplerate = meta->sampleRate;
						break;
					case VIDEO_PKT:
						rep->width = meta->resolution.width;
						rep->height = meta->resolution.height;
						break;
					default: break;
					}

					switch (meta->type) {
					case AUDIO_PKT: case VIDEO_PKT: case SUBTITLE_PKT:
						rep->segment_template->initialization = gf_strdup(getInitName(quality, i).c_str());
						rep->segment_template->media = gf_strdup(getSegmentName(quality, i, templateName).c_str());
						break;
					default: assert(0);
					}
				}
			}
		}

		void writeManifest() {
			if (!mpd->write(mpdFn)) {
				m_host->log(Warning, format("Can't write MPD at %s (1). Check you have sufficient rights.", mpdFn).c_str());
				return;
			}

			auto const mpdPath = format("%s%s", manifestDir, mpdFn);
			if (!moveFile(mpdFn, mpdPath)) {
				m_host->log(Error, format("Can't move MPD at %s (2). Check you have sufficient rights.", mpdPath).c_str());
				return;
			}

			auto out = outputManifest->getBuffer(0);
			auto metadata = make_shared<MetadataFile>(PLAYLIST);
			metadata->filename = mpdPath;
			metadata->durationIn180k = timescaleToClock(segDurationInMs, 1000);
			out->setMetadata(metadata);
			out->setMediaTime(totalDurationInMs, 1000);
			outputManifest->emit(out);
		}

		std::string getPrefixedSegmentName(DASHQuality const * const quality, size_t index, u64 segmentNum) const {
			return manifestDir + getSegmentName(quality, index, std::to_string(segmentNum));
		}

		void generateManifest() {
			ensureManifest();

			for(auto i : getInputs()) {
				auto quality = safe_cast<DASHQuality>(qualities[i].get());
				auto const &meta = quality->getMeta();
				if (!meta) {
					continue;
				}
				if (quality->rep->width) { /*video only*/
					quality->rep->starts_with_sap = (quality->rep->starts_with_sap == GF_TRUE && meta->startsWithRAP) ? GF_TRUE : GF_FALSE;
				}

				std::string fn, fnNext;
				if (useSegmentTimeline) {
					auto entries = quality->rep->segment_template->segment_timeline->entries;
					auto const prevEntIdx = gf_list_count(entries);
					GF_MPD_SegmentTimelineEntry *prevEnt = prevEntIdx == 0 ? nullptr : (GF_MPD_SegmentTimelineEntry*)gf_list_get(entries, prevEntIdx-1);
					auto const currDur = clockToTimescale(meta->durationIn180k, 1000);
					uint64_t segTime = 0;
					if (!prevEnt || prevEnt->duration != currDur) {
						auto ent = (GF_MPD_SegmentTimelineEntry*)gf_malloc(sizeof(GF_MPD_SegmentTimelineEntry));
						segTime = ent->start_time = prevEnt ? prevEnt->start_time + prevEnt->duration*(prevEnt->repeat_count+1) : startTimeInMs;
						ent->duration = (u32)currDur;
						ent->repeat_count = 0;
						gf_list_add(entries, ent);
					} else {
						prevEnt->repeat_count++;
						segTime = prevEnt->start_time + prevEnt->duration*(prevEnt->repeat_count);
					}

					fn = getPrefixedSegmentName(quality, i, segTime);
				} else {
					auto n = getCurSegNum();
					fn = getPrefixedSegmentName(quality, i, n);
					if (flags & PresignalNextSegment) {
						fnNext = getPrefixedSegmentName(quality, i, n + 1);
					}
				}
				auto metaFn = make_shared<MetadataFile>(SEGMENT);
				metaFn->filename = fn;
				metaFn->mimeType = meta->mimeType;
				metaFn->codecName= meta->codecName;
				metaFn->durationIn180k= meta->durationIn180k;
				metaFn->filesize= meta->filesize;
				metaFn->latencyIn180k= meta->latencyIn180k;
				metaFn->startsWithRAP= meta->startsWithRAP;

				switch (meta->type) {
				case AUDIO_PKT: metaFn->sampleRate = meta->sampleRate; break;
				case VIDEO_PKT: metaFn->resolution = meta->resolution; break;
				case SUBTITLE_PKT: break;
				default: assert(0);
				}

				if (!fn.empty()) {
					auto out = getPresignalledData(meta->filesize, quality->lastData, true);
					if (!out)
						throw error("Unexpected null pointer detected which getting data.");
					out->setMetadata(metaFn);
					out->setMediaTime(totalDurationInMs, 1000);
					outputSegments->emit(out);

					if (!fnNext.empty()) {
						auto out = getPresignalledData(0, quality->lastData, false);
						if (out) {
							auto meta = make_shared<MetadataFile>(*metaFn);
							meta->filename = fnNext;
							meta->EOS = false;
							out->setMetadata(meta);
							out->setMediaTime(totalDurationInMs, 1000);
							outputSegments->emit(out);
						}
					}
				}

				if (timeShiftBufferDepthInMs) {
					uint64_t timeShiftSegmentsInMs = 0;
					auto seg = quality->timeshiftSegments.begin();
					while (seg != quality->timeshiftSegments.end()) {
						timeShiftSegmentsInMs += clockToTimescale((*seg).file->durationIn180k, 1000);
						if (timeShiftSegmentsInMs > timeShiftBufferDepthInMs) {
							m_host->log(Debug, format( "Delete segment \"%s\".", (*seg).file->filename).c_str());
							if (gf_delete_file(seg->file->filename.c_str()) == GF_OK || seg->retry == 0) {
								seg = quality->timeshiftSegments.erase(seg);
							} else {
								m_host->log(Warning, format("Couldn't delete old segment \"%s\" (retry=%s).", (*seg).file->filename, (*seg).retry).c_str());
								seg->retry--;
							}
						} else {
							++seg;
						}
					}

					if (useSegmentTimeline) {
						timeShiftSegmentsInMs = 0;
						auto entries = quality->rep->segment_template->segment_timeline->entries;
						auto idx = gf_list_count(entries);
						while (idx--) {
							auto ent = (GF_MPD_SegmentTimelineEntry*)gf_list_get(quality->rep->segment_template->segment_timeline->entries, idx);
							auto const dur = ent->duration * (ent->repeat_count + 1);
							if (timeShiftSegmentsInMs > timeShiftBufferDepthInMs) {
								gf_list_rem(entries, idx);
								gf_free(ent);
							}
							timeShiftSegmentsInMs += dur;
						}
					}

					quality->timeshiftSegments.emplace(quality->timeshiftSegments.begin(), DASHQuality::SegmentToDelete(metaFn));
				}
			}

			if (type != Static) {
				writeManifest();
			}
		}

		void finalizeManifest() {
			if (mpd->mpd->time_shift_buffer_depth) {
				if (!(flags & SegmentsNotOwned)) {
					m_host->log(Info, "Manifest was not rewritten for on-demand and all file are being deleted.");
					auto const mpdPath = format("%s%s", manifestDir, mpdFn);
					if (gf_delete_file(mpdPath.c_str()) != GF_OK) {
						m_host->log(Error, format("Couldn't delete MPD: \"%s\".", mpdPath).c_str());
					}

					for(auto i : getInputs()) {
						auto quality = safe_cast<DASHQuality>(qualities[i].get());
						std::string fn = manifestDir + getInitName(quality, i);
						if (gf_delete_file(fn.c_str()) != GF_OK) {
							m_host->log(Error, format("Couldn't delete initialization segment \"%s\".", fn).c_str());
						}

						for (auto const &seg : quality->timeshiftSegments) {
							if (gf_delete_file(seg.file->filename.c_str()) != GF_OK) {
								m_host->log(Error, format("Couldn't delete media segment \"%s\".", seg.file->filename).c_str());
							}
						}
					}
				}
			} else {
				m_host->log(Info, "Manifest rewritten for on-demand. Media files untouched.");
				mpd->mpd->type = GF_MPD_TYPE_STATIC;
				mpd->mpd->minimum_update_period = 0;
				mpd->mpd->media_presentation_duration = totalDurationInMs;
				totalDurationInMs -= segDurationInMs;
				generateManifest();
				writeManifest();
			}
		}

};
}
}

namespace {

using namespace Modules;

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto config = va_arg(va, DasherConfig*);
	enforce(host, "MPEG_DASH: host can't be NULL");
	enforce(config, "MPEG_DASH: config can't be NULL");
	return Modules::create<Stream::MPEG_DASH>(host, config).release();
}

auto const registered = Factory::registerModule("MPEG_DASH", &createObject);
}
