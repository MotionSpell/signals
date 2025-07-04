#include "lib_pipeline/pipeline.hpp"
#include "lib_utils/system_clock.hpp"
#include "lib_utils/os.hpp"
#include "lib_utils/time.hpp"
#include "config.hpp"
#include <algorithm> //std::max
#include <cassert>

// modules
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_media/decode/decoder.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/encode/libav_encode.hpp"
#include "lib_media/mux/mux_mp4_config.hpp"
#include "lib_media/stream/adaptive_streaming_common.hpp" // AdaptiveStreamingCommon::getCommonPrefixAudio
#include "lib_media/out/filesystem.hpp"
#include "lib_media/out/http_sink.hpp"
#include "lib_media/transform/audio_convert.hpp"
#include "lib_media/transform/logo_overlay.hpp"
#include "plugins/RegulatorMono/regulator_mono.hpp"
#include "plugins/Dasher/mpeg_dash.hpp"

using namespace Modules;
using namespace Pipelines;

extern const char *g_appName;

auto const DASH_SUBDIR = "dash/";
auto const MP4_MONITOR = false;
auto const MAX_GOP_DURATION_IN_MS = 2000;

struct UtcStartTime : IUtcStartTimeQuery {
	uint64_t query() const override {
		return startTime;
	}
	uint64_t startTime;
};

static UtcStartTime utcStartTime;

struct OffsetUtcClock : IUtcClock {
	Fraction getTime() {
		return g_UtcClock->getTime() - offset;
	}
	Fraction offset;
};

static OffsetUtcClock utcClock;

Resolution autoRotate(Resolution res, bool verticalize) {
	if (verticalize && res.height < res.width) {
		Resolution oRes(res.height, res.height);
		g_Log->log(Info, format("[autoRotate] Switched resolution from %sx%s to %sx%s", res.width, res.height, oRes.width, oRes.height).c_str());
		return oRes;
	} else {
		return res;
	}
};

Resolution autoFit(Resolution input, Resolution output) {
	if (input == Resolution())
		return output;
	if (output.width == -1) {
		assert((input.width * output.height % input.height) == 0); //TODO: add SAR at the DASH level to handle rounding errors
		Resolution oRes((input.width * output.height) / input.height, output.height);
		g_Log->log(Info, format("[autoFit] Switched resolution from -1x%s to %sx%s", input.height, oRes.width, oRes.height).c_str());
		return oRes;
	} else if (output.height == -1) {
		assert((input.height * output.width % input.width) == 0); //TODO: add SAR at the DASH level to handle rounding errors
		Resolution oRes(output.width, (input.height * output.width) / input.width);
		g_Log->log(Info, format("[autoFit] Switched resolution from %sx-1 to %sx%s", input.width, oRes.width, oRes.height).c_str());
		return oRes;
	} else {
		return output;
	}
};

IFilter* createEncoder(Pipeline* pipeline, Metadata metadata, bool ultraLowLatency, VideoCodecType videoCodecType, PictureFormat &dstFmt, int bitrate, uint64_t segmentDurationInMs) {
	auto const codecType = metadata->type;
	if (codecType == VIDEO_PKT) {
		g_Log->log(Info, "[Encoder] Found video stream");
		EncoderConfig p { EncoderConfig::Video };
		p.isLowLatency = ultraLowLatency;
		p.codecType = videoCodecType;
		p.bitrate = bitrate;

		auto const metaVideo = safe_cast<const MetadataPktVideo>(metadata);
		p.frameRate = metaVideo->framerate;
		const int GOPDurationDivisor = 1 + (int)(segmentDurationInMs / (MAX_GOP_DURATION_IN_MS + 1));
		p.GOPSize = (Fraction(segmentDurationInMs, 1000) * p.frameRate) / Fraction((GOPDurationDivisor), 1);
		if ((segmentDurationInMs * p.frameRate.num) % (1000 * GOPDurationDivisor * p.frameRate.den)) {
			g_Log->log(Warning, format("[%s] Couldn't align GOP size (%s/%s, divisor=%s) with segment duration (%sms). Segment duration may vary.", g_appName, p.GOPSize.num, p.GOPSize.den, GOPDurationDivisor, segmentDurationInMs).c_str());
		}
		if (GOPDurationDivisor > 1)
			g_Log->log(Info, format("[Encoder] Setting GOP duration to %sms (%s frames)", segmentDurationInMs / GOPDurationDivisor, (double)p.GOPSize).c_str());

		auto m = pipeline->add("Encoder", &p);
		dstFmt.format = p.pixelFormat;
		return m;
	} else if (codecType == AUDIO_PKT) {
		g_Log->log(Info, "[Encoder] Found audio stream");
		EncoderConfig p { EncoderConfig::Audio };
		return pipeline->add("Encoder", &p);
	} else {
		g_Log->log(Info, "[Encoder] Found unknown stream");
		return nullptr;
	}
}

/*video is forced, audio is as passthru as possible*/
IFilter* createConverter(Pipeline* pipeline, Metadata metadata, const PictureFormat &dstFmt) {
	auto const codecType = metadata->type;
	if (codecType == VIDEO_PKT) {
		g_Log->log(Info, "[Converter] Found video stream");
		return pipeline->add("VideoConvert", &dstFmt);
	} else if (codecType == AUDIO_PKT) {
		g_Log->log(Info, "[Converter] Found audio stream");
		auto const demuxFmt = toPcmFormat(safe_cast<const MetadataPktAudio>(metadata));
		auto format = PcmFormat(demuxFmt.sampleRate, demuxFmt.numChannels, demuxFmt.layout, demuxFmt.sampleFormat, (demuxFmt.numPlanes == 1) ? Interleaved : Planar);
		auto cfg = AudioConvertConfig{ {0}, format, 1024};
		return pipeline->add("AudioConvert", &cfg);
	} else {
		throw std::runtime_error("can only create converter for audio/video");
	}
}

void ensureDir(std::string path) {
	if(!dirExists(path))
		mkdir(path);
}

namespace {
struct Logger : LogSink {
	void send(Level level, const char* msg) override {
		g_Log->log(level, format("[%s] %s", g_appName, msg).c_str());
	}
};

Logger g_PrefixedLogger;

OutputPin insertLogo(Pipeline* pipeline, OutputPin main, std::string path) {
	DemuxConfig demuxCfg {};
	demuxCfg.url = path;
	auto demux = pipeline->add("LibavDemux", &demuxCfg);
	DecoderConfig decCfg;
	decCfg.type = VIDEO_PKT;
	auto decoder = pipeline->add("Decoder", &decCfg);
	pipeline->connect(demux, decoder);

	LogoOverlayConfig logoCfg {};
	logoCfg.x = 10;
	logoCfg.y = 10;
	auto overlay = pipeline->add("LogoOverlay", &logoCfg);
	pipeline->connect(main, GetInputPin(overlay, 0));
	pipeline->connect(decoder, GetInputPin(overlay, 1));

	return overlay;
};

}

std::unique_ptr<Pipeline> buildPipeline(const Config &cfg) {
	auto log = &g_PrefixedLogger;
	auto pipeline = make_unique<Pipeline>(log, cfg.ultraLowLatency, Pipelines::Threading::OnePerModule);

	DemuxConfig demuxCfg;
	demuxCfg.url = cfg.input;
	demuxCfg.loop = cfg.loop;
	auto demux = pipeline->add("LibavDemux", &demuxCfg);
	auto const live = cfg.isLive || cfg.ultraLowLatency;
	auto dasherCfg = Modules::DasherConfig { DASH_SUBDIR, format("%s.mpd", g_appName), live, (uint64_t)cfg.segmentDurationInMs, cfg.segmentDurationInMs * cfg.timeshiftInSegNum};
	dasherCfg.utcClock = &utcClock;
	auto dasher = pipeline->add("MPEG_DASH", &dasherCfg);

	IFilter* sink {};

	if(cfg.publishUrl.empty()) {
		auto sinkCfg = FileSystemSinkConfig { cfg.workingDir };
		sink = pipeline->add("FileSystemSink", &sinkCfg);
	} else {
		auto sinkCfg = HttpSinkConfig { cfg.publishUrl, "Elemental", {} };
		sink = pipeline->add("HttpSink", &sinkCfg);
	}

	pipeline->connect(GetOutputPin(dasher, 0), sink);
	pipeline->connect(GetOutputPin(dasher, 1), sink, true);

	if(cfg.publishUrl.empty()) {
		ensureDir(DASH_SUBDIR);
	}

	const bool transcode = cfg.v.size() > 0;
	if (!transcode) {
		log->log(Info, "No transcode. Make passthru.");
		if (cfg.autoRotate)
			throw std::runtime_error("cannot autorotate when transcoding is disabled");
	}

	bool isVertical = false;

	auto decode = [&](OutputPin source, Metadata metadata) -> OutputPin {
		DecoderConfig decCfg;
		decCfg.type = metadata->type;
		auto decoder = pipeline->add("Decoder", &decCfg);
		pipeline->connect(source, decoder);

		if (metadata->isVideo() && cfg.autoRotate) {
			auto const res = safe_cast<const MetadataPktVideo>(metadata)->resolution;
			if (res.height > res.width)
				isVertical = true;
		}

		return decoder;
	};

	auto regulate = [&](OutputPin source) -> OutputPin {
		RegulatorMonoConfig rmCfg;
		auto regulator = pipeline->add("RegulatorMono", &rmCfg);
		pipeline->connect(source, regulator);
		return GetOutputPin(regulator);
	};

	auto mux = [&](OutputPin compressed) -> OutputPin {
		Mp4MuxConfig mp4config;
		mp4config.segmentDurationInMs =  cfg.segmentDurationInMs;
		mp4config.segmentPolicy = FragmentedSegment;
		mp4config.fragmentPolicy = cfg.ultraLowLatency ? OneFragmentPerFrame : OneFragmentPerSegment;
		mp4config.compatFlags = FlushFragMemory | ExactInputDur | Browsers;
		mp4config.utcStartTime = &utcStartTime;

		auto muxer = pipeline->add("GPACMuxMP4", &mp4config);
		pipeline->connect(compressed, muxer);
		return muxer;
	};

	int numDashInputs = 0;

	auto processElementaryStream = [&](int streamIndex) {
		auto const metadata = demux->getOutputMetadata(streamIndex);
		if (!metadata) {
			log->log(Warning, format("Unknown metadata for stream %s. Ignoring.", streamIndex).c_str());
			return;
		}

		auto source = GetOutputPin(demux, streamIndex);

		if(cfg.isLive)
			source = regulate(source);

		OutputPin decoded(nullptr);

		auto const numRes = metadata->isVideo() ? std::max<int>(cfg.v.size(), 1) : 1;
		for (int r = 0; r < numRes; ++r) {
			auto compressed = source;
			if (transcode) {

				if(!decoded.mod) {
					decoded = decode(source, metadata);

					if(metadata->isVideo() && cfg.logoPath != "")
						decoded = insertLogo(pipeline.get(), decoded, cfg.logoPath);
				}

				auto inputRes = metadata->isVideo() ? safe_cast<const MetadataPktVideo>(metadata)->resolution : Resolution();
				auto const outputRes = autoRotate(autoFit(inputRes, cfg.v[r].res), isVertical);
				PictureFormat encoderInputPicFmt(outputRes, PixelFormat::UNKNOWN);
				auto encoder = createEncoder(pipeline.get(), metadata, cfg.ultraLowLatency, (VideoCodecType)cfg.v[r].type, encoderInputPicFmt, cfg.v[r].bitrate, cfg.segmentDurationInMs);
				if (!encoder)
					return;

				auto converter = createConverter(pipeline.get(), metadata, encoderInputPicFmt);

				if(cfg.debugMonitor) {
					if (metadata->isVideo() && r == 0) {
						auto webcamPreview = pipeline->add("SDLVideo", nullptr);
						pipeline->connect(converter, webcamPreview);
					}
				}

				pipeline->connect(decoded, converter);
				pipeline->connect(converter, encoder);
				compressed = GetOutputPin(encoder);
			}

			auto muxer = mux(compressed);

			pipeline->connect(muxer, GetInputPin(dasher, numDashInputs));
			++numDashInputs;

			if(MP4_MONITOR) {
				std::string prefix;
				if (metadata->isVideo()) {
					auto reso = safe_cast<const MetadataPktVideo>(demux->getOutputMetadata(streamIndex))->resolution;
					if (transcode)
						reso = autoFit(reso, cfg.v[r].res);
					prefix = Stream::AdaptiveStreamingCommon::getCommonPrefixVideo(numDashInputs, reso);
				} else {
					prefix = Stream::AdaptiveStreamingCommon::getCommonPrefixAudio(numDashInputs);
				}

				Mp4MuxConfig cfg;
				cfg.baseName = "monitor_" + prefix;
				auto muxer = pipeline->add("GPACMuxMP4", &cfg);
				pipeline->connect(compressed, muxer);
			}
		}
	};

	for (int i = 0; i < demux->getNumOutputs(); ++i)
		processElementaryStream(i);

	auto const offset = 0; //use this to simulate a clock issue from the DASH server
	utcStartTime.startTime = fractionToClock(getUTC() - offset);
	utcClock.offset = offset;

	return pipeline;
}
