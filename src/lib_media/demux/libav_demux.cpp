#include "libav_demux.hpp"
#include "../transform/restamp.hpp"
#include "lib_utils/tools.hpp"
#include "lib_ffpp/ffpp.hpp"
#include <cassert>
#include <fstream>

#define PKT_QUEUE_SIZE 256

namespace Modules {

namespace {

constexpr
const char* webcamFormat() {
#ifdef _WIN32
	return "dshow";
#elif __linux__
	return "v4l2";
#elif __APPLE__
	return "avfoundation";
#else
#error "unknown platform"
#endif
}

}

namespace Demux {

void LibavDemux::webcamList() {
	log(Warning, "Webcam list:");
	ffpp::Dict dict(typeid(*this).name(), "-list_devices true");
	avformat_open_input(&m_formatCtx, "video=dummy:audio=dummy", av_find_input_format(webcamFormat()), &dict);
	log(Warning, "Webcam example: webcam:video=\"Integrated Webcam\":audio=\"Microphone (Realtek High Defini\"");
}

bool LibavDemux::webcamOpen(const std::string &options) {
	auto avInputFormat = av_find_input_format(webcamFormat());
	if (avformat_open_input(&m_formatCtx, options.c_str(), avInputFormat, nullptr))
		return false;
	return true;
}

void LibavDemux::initRestamp() {
	restampers.resize(m_formatCtx->nb_streams);
	for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
		const std::string format(m_formatCtx->iformat->name);
		const std::string  fn = m_formatCtx->filename;
		if (format == "rtsp" || format == "rtp" || format == "sdp" || !fn.compare(0, 4, "rtp:") || !fn.compare(0, 4, "udp:")) {
			restampers[i] = create<Transform::Restamp>(Transform::Restamp::IgnoreFirstAndReset);
		}
		else {
			restampers[i] = create<Transform::Restamp>(Transform::Restamp::Reset);
		}

		if (format == "rtsp" || format == "rtp" || format == "mpegts") {
			startPTSIn180k = std::max<int64_t>(startPTSIn180k, timescaleToClock(m_formatCtx->streams[i]->start_time*m_formatCtx->streams[i]->time_base.num, m_formatCtx->streams[i]->time_base.den));
		}
	}
}

LibavDemux::LibavDemux(const std::string &url, const bool loop, const std::string &avformatCustom, const uint64_t seekTimeInMs, const std::string &formatName)
: loop(loop), done(false), dispatchPkts(PKT_QUEUE_SIZE) {
	if (!(m_formatCtx = avformat_alloc_context()))
		throw error("Can't allocate format context");

	const std::string device = url.substr(0, url.find(":"));
	if (device == "webcam") {
		if (url == device || !webcamOpen(url.substr(url.find(":") + 1))) {
			webcamList();
			if (m_formatCtx) avformat_close_input(&m_formatCtx);
			throw error("Webcam init failed.");
		}

		restampers.resize(m_formatCtx->nb_streams);
		for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
			restampers[i] = create<Transform::Restamp>(Transform::Restamp::ClockSystem); /*some webcams timestamps don't start at 0 (based on UTC)*/
		}
	} else {
		ffpp::Dict dict(typeid(*this).name(),"-buffer_size 1M -fifo_size 1M -probesize 10M -analyzeduration 10M -overrun_nonfatal 1 -protocol_whitelist file,udp,rtp,http,https,tcp,tls,rtmp -rtsp_flags prefer_tcp " + avformatCustom);
		if (avformat_open_input(&m_formatCtx, url.c_str(), av_find_input_format(formatName.c_str()), &dict)) {
			if (m_formatCtx) avformat_close_input(&m_formatCtx);
			throw error(format("Error when opening input '%s'", url));
		}

		if (seekTimeInMs) {
			if (avformat_seek_file(m_formatCtx, -1, INT64_MIN, convertToTimescale(seekTimeInMs, 1000, AV_TIME_BASE), INT64_MAX, 0) < 0) {
				avformat_close_input(&m_formatCtx);
				throw error(format("Couldn't seek to time %sms", seekTimeInMs));
			} else {
				log(Info, "Successful initial seek to %sms", seekTimeInMs);
			}
		}

		//if you don't call you may miss the first frames
		m_formatCtx->max_analyze_duration = 0;
		if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
			avformat_close_input(&m_formatCtx);
			throw error("Couldn't get additional video stream info");
		}

		initRestamp();

		av_dict_free(&dict);
	}

	lastDTS.resize(m_formatCtx->nb_streams);
	lastPTS.resize(m_formatCtx->nb_streams);
	for (unsigned i = 0; i<m_formatCtx->nb_streams; i++) {
		auto const st = m_formatCtx->streams[i];
		auto const parser = av_stream_get_parser(st);
		if (parser) {
			st->codec->ticks_per_frame = parser->repeat_pict + 1;
		} else {
			log(Debug, format("No parser found for stream %s (%s). Couldn't use full metadata to get the timescale.", i, st->codec->codec_name));
		}
		st->codec->time_base = st->time_base; //allows to keep trace of the pkt timebase in the output metadata
		if (!st->codec->framerate.num) {
			st->codec->framerate = st->avg_frame_rate; //it is our reponsibility to provide the application with a reference framerate
		}

		std::shared_ptr<IMetadata> m;
		auto codecCtx = shptr(avcodec_alloc_context3(nullptr));
		avcodec_copy_context(codecCtx.get(), st->codec);
		switch (st->codec->codec_type) {
		case AVMEDIA_TYPE_AUDIO: m = shptr(new MetadataPktLibavAudio(codecCtx, st->id)); break;
		case AVMEDIA_TYPE_VIDEO: m = shptr(new MetadataPktLibavVideo(codecCtx, st->id)); break;
		case AVMEDIA_TYPE_SUBTITLE: m = shptr(new MetadataPktLibavSubtitle(codecCtx, st->id)); break;
		default: break;
		}
		outputs.push_back(addOutput<OutputDataDefault<DataAVPacket>>(m));
		av_dump_format(m_formatCtx, i, url.c_str(), 0);
	}
}

LibavDemux::~LibavDemux() {
	done = true;
	if (workingThread.joinable()) {
		workingThread.join();
	}

	avformat_close_input(&m_formatCtx);

	AVPacket p;
	while (dispatchPkts.read(p)) {
		av_free_packet(&p);
	}
}

void LibavDemux::seekToStart() {
	if (av_seek_frame(m_formatCtx, -1, m_formatCtx->start_time, AVSEEK_FLAG_ANY) < 0)
		throw error(format("Couldn't seek to start time %s", m_formatCtx->start_time));

	loopOffsetIn180k += timescaleToClock(m_formatCtx->duration, AV_TIME_BASE) ;
}

void LibavDemux::threadProc() {
	AVPacket pkt;
	bool nextPacketResetFlag = false;
	while (!done) {
		av_init_packet(&pkt);
		int status = av_read_frame(m_formatCtx, &pkt);
		if (status < 0) {
			av_free_packet(&pkt);
			if (status == (int)AVERROR_EOF || (m_formatCtx->pb && m_formatCtx->pb->eof_reached)) {
				log(Info, "End of stream detected - %s", loop ? "looping" : "leaving");
				if (loop) {
					seekToStart();
					nextPacketResetFlag = true;
					continue;
				}
			} else if (m_formatCtx->pb && m_formatCtx->pb->error) {
				log(Error, "Stream contains an irrecoverable error (%s) - leaving", status);
			}
			done = true;
			return;
		}

		auto const base = m_formatCtx->streams[pkt.stream_index]->time_base;
		pkt.dts += clockToTimescale(loopOffsetIn180k * base.num, base.den);
		pkt.pts += clockToTimescale(loopOffsetIn180k * base.num, base.den);
		if (nextPacketResetFlag) {
			pkt.flags |= AV_PKT_FLAG_RESET_DECODER;
			nextPacketResetFlag = false;
		}

		while (!dispatchPkts.write(pkt)) {
			if (done) {
				av_free_packet(&pkt);
				return;
			}
			log(m_formatCtx->pb && !m_formatCtx->pb->seekable ? Warning : Debug, "Dispatch queue is full - regulating.");
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}

void LibavDemux::setMediaTime(std::shared_ptr<DataAVPacket> data) {
	auto pkt = data->getPacket();
	lastDTS[pkt->stream_index] = pkt->dts;
	lastPTS[pkt->stream_index] = pkt->pts;
	auto const base = m_formatCtx->streams[pkt->stream_index]->time_base;
	auto const time = timescaleToClock(pkt->dts * base.num, base.den);
	data->setMediaTime(time);

	restampers[pkt->stream_index]->process(data); /*restamp by pid only when no start time*/
	int64_t offset = data->getMediaTime() - time;
	if (offset != 0) {
		data->restamp(offset * base.num, base.den); /*propagate to AVPacket*/
	}
}

bool LibavDemux::dispatchable(AVPacket * const pkt) {
	if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
		log(Error, "Corrupted packet received (DTS=%s).", pkt->dts);
	}
	if (pkt->dts == AV_NOPTS_VALUE) {
		pkt->dts = lastDTS[pkt->stream_index];
		log(Debug, "No DTS: setting last value %s.", pkt->dts);
	}
	if (pkt->pts == AV_NOPTS_VALUE) {
		pkt->pts = lastPTS[pkt->stream_index];
		log(Debug, "No PTS: setting last value %s.", pkt->pts);
	}
	if (!lastDTS[pkt->stream_index] && pkt->pts < clockToTimescale(startPTSIn180k*m_formatCtx->streams[pkt->stream_index]->time_base.num, m_formatCtx->streams[pkt->stream_index]->time_base.den)) {
		av_free_packet(pkt);
		return false;
	}
	return true;
}

void LibavDemux::dispatch(AVPacket *pkt) {
	auto out = outputs[pkt->stream_index]->getBuffer(0);
	AVPacket *outPkt = out->getPacket();
	av_packet_move_ref(outPkt, pkt);
	setMediaTime(out);
	outputs[outPkt->stream_index]->emit(out);
	sparseStreamsHeartbeat(outPkt);
}

void LibavDemux::sparseStreamsHeartbeat(AVPacket const * const pkt) {
	int64_t curDTS = 0;
	if (m_formatCtx->streams[pkt->stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO) { //signal clock from audio to sparse streams (should be the PCR but libavformat doesn't give access to it)
		auto const base = m_formatCtx->streams[pkt->stream_index]->time_base;
		auto const time = timescaleToClock(pkt->dts * base.num, base.den);
		if (time > curDTS) {
			curDTS = time;
		}
	}
	if (curDTS > curTimeIn180k) {
		curTimeIn180k = curDTS;
		for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
			if ((int)i == pkt->stream_index) {
				continue;
			}
			auto const st = m_formatCtx->streams[i];
			if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
				auto outParse = outputs[i]->getBuffer(0);
				outParse->setMediaTime(curTimeIn180k);
				outputs[i]->emit(outParse);
			}
		}
	}
}

void LibavDemux::process(Data data) {
	if (startPTSIn180k) {
		startPTSIn180k += g_DefaultClock->now();
	}
	workingThread = std::thread(&LibavDemux::threadProc, this);

	AVPacket pkt;
	while (1) {
		if (getNumInputs() && getInput(0)->tryPop(data)) {
			done = true;
			log(Info, "Exit from an external event.");
			return;
		}

		if (!dispatchPkts.read(pkt)) {
			if (done) {
				log(Info, "All data consumed: exit process().");
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}
		if (dispatchable(&pkt)) {
			dispatch(&pkt);
		}
	}

	while (dispatchPkts.read(pkt)) {
		dispatch(&pkt);
	}
}

}
}
