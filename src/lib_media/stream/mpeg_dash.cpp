#include "mpeg_dash.hpp"
#include "../common/libav.hpp"


#define MIN_BUFFER_TIME_IN_MS_VOD  3000
#define MIN_BUFFER_TIME_IN_MS_LIVE 1000

#define AVAILABILITY_TIMEOFFSET_IN_S 0.0

namespace Modules {

namespace {
GF_MPD_AdaptationSet *createAS(uint64_t segDurationInMs, GF_MPD_Period *period, gpacpp::MPD *mpd) {
	auto as = mpd->addAdaptationSet(period);
	GF_SAFEALLOC(as->segment_template, GF_MPD_SegmentTemplate);
	as->segment_template->duration = segDurationInMs;
	as->segment_template->timescale = 1000;
	as->segment_template->start_number = 1;
	as->segment_template->availability_time_offset = AVAILABILITY_TIMEOFFSET_IN_S;

	//FIXME: arbitrary: should be set by the app, or computed
	as->segment_alignment = GF_TRUE;
	as->bitstream_switching = GF_TRUE;

	return as;
}
}

namespace Stream {

MPEG_DASH::MPEG_DASH(const std::string &mpdPath, Type type, uint64_t segDurationInMs)
	: AdaptiveStreamingCommon(type, segDurationInMs),
	  mpd(type == Live ? new gpacpp::MPD(GF_MPD_TYPE_DYNAMIC, MIN_BUFFER_TIME_IN_MS_LIVE)
	  : new gpacpp::MPD(GF_MPD_TYPE_STATIC, MIN_BUFFER_TIME_IN_MS_VOD)), mpdPath(mpdPath) {
}

MPEG_DASH::~MPEG_DASH() {
	endOfStream();
}

std::unique_ptr<Quality> MPEG_DASH::createQuality() const {
	return uptr<Quality>(safe_cast<Quality>(new DASHQuality));
}

void MPEG_DASH::ensureManifest() {
	if (!mpd->mpd->availabilityStartTime) {
		mpd->mpd->availabilityStartTime = startTimeInMs;
	}

	if (!gf_list_count(mpd->mpd->periods)) {
		mpd->mpd->publishTime = mpd->mpd->availabilityStartTime;

		auto period = mpd->addPeriod();
		period->ID = gf_strdup("p0");
		GF_MPD_AdaptationSet *audioAS = nullptr, *videoAS = nullptr;
		for (size_t i = 0; i < getNumInputs() - 1; ++i) {
			GF_MPD_AdaptationSet *as = nullptr;
			auto quality = safe_cast<DASHQuality>(qualities[i].get());
			switch (quality->meta->getStreamType()) {
			case AUDIO_PKT: audioAS ? as = audioAS : as = audioAS = createAS(segDurationInMs, period, mpd.get()); break;
			case VIDEO_PKT: videoAS ? as = videoAS : as = videoAS = createAS(segDurationInMs, period, mpd.get()); break;
			default: assert(0);
			}

			auto rep = mpd->addRepresentation(as, format("%s", i).c_str(), (u32)quality->avg_bitrate_in_bps);
			quality->rep = rep;
			GF_SAFEALLOC(rep->segment_template, GF_MPD_SegmentTemplate);
			rep->segment_template->start_number = 1;
			rep->mime_type = gf_strdup(quality->meta->getMimeType().c_str());
			rep->codecs = gf_strdup(quality->meta->getCodecName().c_str());
			rep->starts_with_sap = GF_TRUE;
			switch (quality->meta->getStreamType()) {
			case AUDIO_PKT:
				rep->samplerate = quality->meta->sampleRate;
				rep->segment_template->initialization = gf_strdup(format("audio.mp4").c_str());
				rep->segment_template->media = gf_strdup(format("audio.mp4_$Number$").c_str());
				break;
			case VIDEO_PKT:
				rep->width = quality->meta->resolution[0];
				rep->height = quality->meta->resolution[1];
				rep->segment_template->initialization = gf_strdup(format("video_$RepresentationID$_%sx%s.mp4", rep->width, rep->height).c_str());
				rep->segment_template->media = gf_strdup(format("video_%s_%sx%s.mp4_$Number$", i, rep->width, rep->height).c_str());
				break;
			default:
				assert(0);
			}
		}
	}
}

void MPEG_DASH::generateManifest() {
	ensureManifest();

	for (size_t i = 0; i < getNumInputs() - 1; ++i) {
		auto quality = safe_cast<DASHQuality>(qualities[i].get());
		if (quality->rep->width) { /*video only*/
			quality->rep->starts_with_sap = (quality->rep->starts_with_sap == GF_TRUE && quality->meta->getStartsWithRAP()) ? GF_TRUE : GF_FALSE;
		}
	}

	if (type == Live) {
		if (!mpd->write(mpdPath)) {
			log(Warning, "Can't write MPD at %s (1). Check you have sufficient rights.", mpdPath);
		}
	}
}

void MPEG_DASH::finalizeManifest() {
	mpd->mpd->type = GF_MPD_TYPE_STATIC;
	mpd->mpd->minimum_update_period = 0;
	mpd->mpd->media_presentation_duration = totalDurationInMs;
	generateManifest();

	if (!mpd->write(mpdPath)) {
		log(Warning, "Can't write MPD at %s (2). Check you have sufficient rights.", mpdPath);
	}
}

}
}
