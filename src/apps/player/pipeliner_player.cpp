#include "pipeliner_player.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/log.hpp" // g_Log
#include "lib_utils/format.hpp"
#include "lib_utils/scheduler.hpp"
#include "lib_utils/system_clock.hpp"
#include <fstream>

// modules
#include "lib_media/decode/decoder.hpp"
#include "lib_media/demux/dash_demux.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/in/mpeg_dash_input.hpp"
#include "lib_media/in/video_generator.hpp"
#include "lib_media/in/file.hpp"
#include "lib_media/out/null.hpp"
#include "lib_media/transform/rectifier.hpp"
//FIXME: #include "plugins/GpacFilters/gpac_filters.hpp"
#include "plugins/HlsDemuxer/hls_demux.hpp"
#include "plugins/HttpInput/http_input.hpp"
#include "plugins/SocketInput/socket_input.hpp"
#include "plugins/RegulatorMono/regulator_mono.hpp"
#include "plugins/RegulatorMulti/regulator_multi.hpp"
#include "plugins/TsDemuxer/ts_demuxer.hpp"

using namespace Modules;
using namespace Pipelines;

namespace {

const bool regulateMono = true;
const bool regulateMulti = true;
const bool rectify = true;

struct Restamper : Module {
		Restamper(KHost*, int count, int shift) : shift(shift) {
			for(int i=0; i < count; ++i) {
				addInput();
				addOutput();
			}
		}

		void process() override {
			int idx;
			auto dataIn = popAny(idx);

			auto const dataTime = dataIn->get<DecodingTime>().time;
			if(startTime == INT64_MIN)
				startTime = dataTime;

			auto restampedTime = dataTime - startTime + shift;

			auto dataOut = dataIn->clone();
			dataOut->set(PresentationTime{restampedTime});
			try {
				dataOut->set(DecodingTime{dataIn->get<DecodingTime>().time - startTime + shift});
			} catch(...) {}

			outputs[idx]->post(dataOut);
		}

	private:
		int64_t startTime = INT64_MIN, shift;

		Data popAny(int& inputIdx) {
			Data data;
			inputIdx = 0;
			while (!inputs[inputIdx]->tryPop(data))
				inputIdx++;
			return data;
		}
};

struct LocalFileSystem : In::IFilePuller {
	void wget(const char* szUrl, std::function<void(SpanC)> callback) override {
		printf("LocalFileSystem: wget('%s')\n", szUrl);
		std::ifstream fp(szUrl, std::ios::binary);
		if(!fp.is_open())
			return;

		fp.seekg(0, std::ios::end);
		auto const size = fp.tellg();
		fp.seekg(0, std::ios::beg);

		std::vector<uint8_t> buf(size);
		fp.read((char*)buf.data(), buf.size());

		callback({buf.data(), buf.size()});
	}
	void askToExit() override {}
};

bool startsWith(std::string s, std::string prefix) {
	return s.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string s, std::string suffix) {
	return s.substr(s.size() - suffix.size(), suffix.size()) == suffix;
}

bool hasAudio = false, hasVideo = false;

IFilter* createRenderer(Pipeline& pipeline, Config cfg, int codecType) {
	if(!cfg.noRenderer) {
		if (codecType == VIDEO_RAW) {
			g_Log->log(Info, "Found video stream");
			return pipeline.add("SDLVideo", nullptr);
		} else if (codecType == AUDIO_RAW) {
			g_Log->log(Info, "Found audio stream");
			return pipeline.add("SDLAudio", nullptr);
		}
		g_Log->log(Info, "Found unknown stream");
	}

	return pipeline.addModule<Out::Null>();
}

IFilter* createDemuxer(Pipeline& pipeline, std::string url) {
	if(startsWith(url, "mpegts://")) {
		url = url.substr(9);
		IFilter *in = nullptr;
		SocketInputConfig mcast;
		mcast.isTcp = false;
		mcast.isMulticast = true;
		if (startsWith(url, "http")) {
			HttpInputConfig cfg;
			cfg.url = url;
			in = pipeline.add("HttpInput", &cfg);
		} else if (sscanf(url.c_str(), "%d.%d.%d.%d:%d",
		    &mcast.ipAddr[0],
		    &mcast.ipAddr[1],
		    &mcast.ipAddr[2],
		    &mcast.ipAddr[3],
		    &mcast.port) == 5) {
			in = pipeline.add("SocketInput", &mcast);
		} else {
			FileInputConfig fileInputConfig;
			fileInputConfig.filename = url;
			in = pipeline.add("FileInput", &fileInputConfig);
		}
		TsDemuxerConfig cfg {};
		auto demux = pipeline.add("TsDemuxer", &cfg);
		pipeline.connect(in, demux);
		return demux;
	}
	if(startsWith(url, "videogen://")) {
		return pipeline.addModule<In::VideoGenerator>(url.c_str());
	}
	if(endsWith(url, ".m3u8")) {
		HlsDemuxConfig hlsCfg;
		if(!startsWith(url, "http://") && !startsWith(url, "https://") ) {
			static LocalFileSystem fs;
			hlsCfg.filePuller = &fs;
		}
		hlsCfg.url = url;
		auto recv = pipeline.add("HlsDemuxer", &hlsCfg);
		TsDemuxerConfig tsCfg {};
		auto demux = pipeline.add("TsDemuxer", &tsCfg);
		pipeline.connect(recv, demux);
		return demux;
	}
	if(startsWith(url, "http://") || startsWith(url, "https://")) {
		DashDemuxConfig cfg;
		cfg.url = url;
		return pipeline.add("DashDemuxer", &cfg);
	} else {
		DemuxConfig cfg;
		cfg.url = url;
		return pipeline.add("LibavDemux", &cfg);
	}
}
} // anonymous namespace

void declarePipeline(Config cfg, Pipeline &pipeline, const char *url) {
	auto demuxer = createDemuxer(pipeline, url);
	if(demuxer->getNumOutputs() == 0)
		throw std::runtime_error("No streams found");

	auto const maxMediaTimeDelayInMs = 3000;
	auto restamper = pipeline.addNamedModule<Restamper>("restamper", demuxer->getNumOutputs(), timescaleToClock(maxMediaTimeDelayInMs-100, 1000));

	IFilter* regulatorMulti = nullptr;
	if (regulateMulti) {
		RegulatorMultiConfig rmCfg;
		rmCfg.maxMediaTimeDelayInMs = maxMediaTimeDelayInMs;
		regulatorMulti = pipeline.add("RegulatorMulti", &rmCfg);
	}

	IFilter *rectifier = nullptr;
	if (rectify) {
		RectifierConfig rCfg;
		rCfg.clock = g_SystemClock;
		rCfg.scheduler = std::make_shared<Scheduler>(rCfg.clock);
		rCfg.frameRate = Fraction(25, 1); //always play at this rate
		g_Log->log(Debug, format("Rectify at frequency %s/%s (%s)", rCfg.frameRate.num, rCfg.frameRate.den, (double)rCfg.frameRate).c_str());
		rectifier = pipeline.add("Rectifier", &rCfg);
	}

	for (int k = 0; k < demuxer->getNumOutputs(); ++k) {
		auto source = GetOutputPin(demuxer, k);

		auto metadata = demuxer->getOutputMetadata(k);
		if(!metadata) {
			g_Log->log(Debug, format("Ignoring stream #%s (no metadata)", k).c_str());
			continue;
		}
		if (metadata->isSubtitle()/*only render audio and video*/) {
			g_Log->log(Debug, format("Ignoring stream #%s", k).c_str());
			continue;
		}
		if (metadata->type == VIDEO_PKT) {
			if (hasVideo) {
				g_Log->log(Debug, format("Ignoring stream #%s because we only process the first video", k).c_str());
				continue;
			}
			hasVideo = true;
		}
		if (metadata->type == AUDIO_PKT) {
			if (hasAudio) {
				g_Log->log(Debug, format("Ignoring stream #%s because we only process the first audio", k).c_str());
				continue;
			}
			hasAudio = true;
		}

		if (regulateMulti) {
			pipeline.connect(source, GetInputPin(regulatorMulti, k));
			source = GetOutputPin(regulatorMulti, k);
		}

		pipeline.connect(source, GetInputPin(restamper, k));
		source = GetOutputPin(restamper, k);

		if (regulateMono) {
			RegulatorMonoConfig rmCfg;
			rmCfg.resyncAllowed = false;
			auto regulator = pipeline.add("RegulatorMono", &rmCfg);
			pipeline.connect(source, regulator);
			source = GetOutputPin(regulator);
		}

		if(metadata->type != VIDEO_RAW) {
			DecoderConfig decCfg;
			decCfg.type = metadata->type;
			auto decode = pipeline.add("Decoder", &decCfg);
			pipeline.connect(source, decode);
			source = GetOutputPin(decode);
		}

		metadata = source.mod->getOutputMetadata(source.index);

		if (rectify) {
			int pin = rectifier->getNumInputs() - 1;
			pipeline.connect(source, GetInputPin(rectifier, pin));
			source = GetOutputPin(rectifier, pin);
		}

		auto render = createRenderer(pipeline, cfg, metadata->type);
		pipeline.connect(source, GetInputPin(render));
	}
}
