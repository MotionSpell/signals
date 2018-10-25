#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "libav_demux.hpp"
#include "../transform/restamp.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/os.hpp"
#include "../common/ffpp.hpp"
#include "../common/libav.hpp"
#include <atomic>
#include <thread>
#include <vector>

extern "C" {
#include <libavdevice/avdevice.h> // avdevice_register_all
#include <libavformat/avformat.h> // av_find_input_format
}

#define PKT_QUEUE_SIZE 256

#define AV_PKT_FLAG_RESET_DECODER (1 << 30)

static const int avioCtxBufferSize = 1024 * 1024;

using namespace Modules;

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

static
bool startsWith(std::string s, std::string prefix) {
	return s.compare(0, prefix.size(), prefix) == 0;
}

class LibavDemux : public ActiveModule {
	public:
		//@param url may be a file, a remote URL, or a webcam (set "webcam" to list the available devices)
		LibavDemux(IModuleHost* host, DemuxConfig const& config)
			: m_host(host),
			  loop(config.loop), done(false), packetQueue(PKT_QUEUE_SIZE), m_read(config.func) {
			if (!(m_formatCtx = avformat_alloc_context()))
				throw error("Can't allocate format context");

			avformat_network_init();

			auto& url = config.url;

			avdevice_register_all();
			const std::string device = url.substr(0, url.find(":"));
			if (device == "webcam") {
				if (url == device || !webcamOpen(url.substr(url.find(":") + 1))) {
					webcamList();
					clean();
					throw error("Webcam init failed.");
				}

				m_streams.resize(m_formatCtx->nb_streams);
				for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
					m_streams[i].restamper = create<Transform::Restamp>(&NullHost, Transform::Restamp::ClockSystem); /*some webcams timestamps don't start at 0 (based on UTC)*/
				}
			} else {
				ffpp::Dict dict(typeid(*this).name(), "-buffer_size 1M -fifo_size 1M -probesize 10M -analyzeduration 10M -overrun_nonfatal 1 -protocol_whitelist file,udp,rtp,http,https,tcp,tls,rtmp -rtsp_flags prefer_tcp -correct_ts_overflow 1 " + config.avformatCustom);

				if (m_read) {
					m_avioCtx = avio_alloc_context((unsigned char*)av_malloc(avioCtxBufferSize), avioCtxBufferSize, 0, this, &LibavDemux::read, nullptr, nullptr);
					if (!m_avioCtx)
						throw std::runtime_error("AvIO allocation failed");
					m_formatCtx->pb = m_avioCtx;
					m_formatCtx->flags = AVFMT_FLAG_CUSTOM_IO;
				}

				AVInputFormat* avInputFormat = nullptr;

				if(!config.formatName.empty()) {
					avInputFormat = av_find_input_format(config.formatName.c_str());
					if(!avInputFormat) {
						clean();
						throw error(format("Error when opening input '%s': unsupported input format '%s'", url, config.formatName));
					}
				}

				int err = avformat_open_input(&m_formatCtx, url.c_str(), avInputFormat, &dict);
				if (err) {
					clean();
					char buffer[256];
					av_strerror(err, buffer, sizeof buffer);
					throw error(format("Error when opening input '%s': %s", url, buffer));
				}

				if(!avInputFormat) {
					m_host->log(Info, format("Using input format '%s'", m_formatCtx->iformat->name).c_str());
				}

				m_formatCtx->flags |= AVFMT_FLAG_KEEP_SIDE_DATA; //deprecated >= 3.5 https://github.com/FFmpeg/FFmpeg/commit/ca2b779423

				if (config.seekTimeInMs) {
					if (avformat_seek_file(m_formatCtx, -1, INT64_MIN, rescale(config.seekTimeInMs, 1000, AV_TIME_BASE), INT64_MAX, 0) < 0) {
						clean();
						throw error(format("Couldn't seek to time %sms", config.seekTimeInMs));
					}

					m_host->log(Info, format("Successful initial seek to %sms", config.seekTimeInMs).c_str());
				}

				//if you don't call you may miss the first frames
				if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
					clean();
					throw error("Couldn't get additional video stream info");
				}
				m_streams.resize(m_formatCtx->nb_streams);

				initRestamp();

				av_dict_free(&dict);
			}

			for (unsigned i = 0; i<m_formatCtx->nb_streams; i++) {
				auto const st = m_formatCtx->streams[i];
				auto const parser = av_stream_get_parser(st);
				if (parser) {
					st->codec->ticks_per_frame = parser->repeat_pict + 1;
				} else {
					m_host->log(Debug, format("No parser found for stream %s (%s). Couldn't use full metadata to get the timescale.", i, avcodec_get_name(st->codecpar->codec_id)).c_str());
				}
				st->codec->time_base = st->time_base; //allows to keep trace of the pkt timebase in the output metadata
				if (!st->codec->framerate.num) {
					st->codec->framerate = st->avg_frame_rate; //it is our reponsibility to provide the application with a reference framerate
				}

				std::shared_ptr<IMetadata> m;
				auto codecCtx = shptr(avcodec_alloc_context3(nullptr));
				avcodec_copy_context(codecCtx.get(), st->codec);
				switch (st->codecpar->codec_type) {
				case AVMEDIA_TYPE_AUDIO: m = make_shared<MetadataPktLibavAudio>(codecCtx, st->id); break;
				case AVMEDIA_TYPE_VIDEO: m = make_shared<MetadataPktLibavVideo>(codecCtx, st->id); break;
				case AVMEDIA_TYPE_SUBTITLE: m = make_shared<MetadataPktLibavSubtitle>(codecCtx, st->id); break;
				default: break;
				}
				m_streams[i].output = addOutput<OutputDataDefault<DataAVPacket>>(m);
				av_dump_format(m_formatCtx, i, url.c_str(), 0);
			}
		}

		~LibavDemux() {
			clean();
		}

		bool work() override {
			if(!workingThread.joinable())
				workingThread = std::thread(&LibavDemux::inputThread, this);

			AVPacket pkt;
			if (!packetQueue.read(pkt)) {
				if (done) {
					m_host->log(Info, "All data consumed: exit process().");
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				return true;
			}

			if (!rectifyTimestamps(pkt)) {
				av_packet_unref(&pkt);
				return true;
			}

			if (!dispatchable(&pkt)) {
				av_packet_unref(&pkt);
				return true;
			}

			dispatch(&pkt);
			return true;
		};

	private:
		int readFrame(AVPacket* pkt) {
			for(;;) {
				int status = av_read_frame(m_formatCtx, pkt);
				if (status != (int)AVERROR(EAGAIN))
					return status;

				m_host->log(Debug, format("Stream asks to try again later. Sleeping for a short period of time.").c_str());
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		void clean() {
			done = true;
			if (workingThread.joinable()) {
				workingThread.join();
			}

			if (m_formatCtx) {
				avformat_close_input(&m_formatCtx);
				avformat_free_context(m_formatCtx);
			}

			AVPacket p;
			while (packetQueue.read(p)) {
				av_packet_unref(&p);
			}

			if(m_avioCtx)
				av_free(m_avioCtx->buffer);
			av_free(m_avioCtx);
		}

		void webcamList() {
			m_host->log(Warning, "Webcam list:");
			ffpp::Dict dict(typeid(*this).name(), "-list_devices true");
			avformat_open_input(&m_formatCtx, "video=dummy:audio=dummy", av_find_input_format(webcamFormat()), &dict);
			m_host->log(Warning, "Webcam example: webcam:video=\"Integrated Webcam\":audio=\"Microphone (Realtek High Defini\"");
		}

		bool webcamOpen(const std::string &options) {
			auto avInputFormat = av_find_input_format(webcamFormat());
			if (avformat_open_input(&m_formatCtx, options.c_str(), avInputFormat, nullptr)) {
				avformat_free_context(m_formatCtx);
				m_formatCtx = nullptr;
				return false;
			}
			return true;
		}

		void initRestamp() {
			for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
				const std::string format(m_formatCtx->iformat->name);
				const std::string url = m_formatCtx->url;
				if (format == "rtsp" || url == "rtp" || format == "sdp" || startsWith(url, "rtp:") || startsWith(url, "udp:")) {
					highPriority = true;
					m_streams[i].restamper = create<Transform::Restamp>(&NullHost, Transform::Restamp::IgnoreFirstAndReset);
				} else {
					m_streams[i].restamper = create<Transform::Restamp>(&NullHost, Transform::Restamp::Reset);
				}

				if (format == "rtsp" || format == "rtp" || format == "mpegts" || format == "rtmp" || format == "flv") { //TODO: evaluate why this is not the default behaviour
					auto stream = m_formatCtx->streams[i];
					startPTSIn180k = std::max<int64_t>(startPTSIn180k, timescaleToClock(stream->start_time*stream->time_base.num, stream->time_base.den));
				} else {
					startPTSIn180k = 0;
				}
			}
		}

		void seekToStart() {
			if (av_seek_frame(m_formatCtx, -1, m_formatCtx->start_time, AVSEEK_FLAG_ANY) < 0)
				throw error(format("Couldn't seek to start time %s", m_formatCtx->start_time));

			for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
				m_streams[i].offsetIn180k += timescaleToClock(m_formatCtx->duration, AV_TIME_BASE);
			}
		}

		bool rectifyTimestamps(AVPacket &pkt) {
			auto const stream = m_formatCtx->streams[pkt.stream_index];
			auto & demuxStream = m_streams[pkt.stream_index];
			auto const maxDelayInSec = 5;
			auto const thresholdInBase = (maxDelayInSec * stream->time_base.den) / stream->time_base.num;

			if (pkt.dts != AV_NOPTS_VALUE) {
				pkt.dts += clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den);
				if (demuxStream.lastDTS && pkt.dts < demuxStream.lastDTS
				    && (1LL << stream->pts_wrap_bits) - demuxStream.lastDTS < thresholdInBase && pkt.dts + (1LL << stream->pts_wrap_bits) > demuxStream.lastDTS) {
					demuxStream.offsetIn180k += timescaleToClock((1LL << stream->pts_wrap_bits) * stream->time_base.num, stream->time_base.den);
					m_host->log(Warning, format("Stream %s: overflow detecting on DTS (%s, last=%s, timescale=%s/%s, offset=%s).",
					        pkt.stream_index, pkt.dts, demuxStream.lastDTS, stream->time_base.num, stream->time_base.den, clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den)).c_str());
				}
			}

			if (pkt.pts != AV_NOPTS_VALUE) {
				pkt.pts += clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den);
				if (pkt.pts < pkt.dts && (1LL << stream->pts_wrap_bits) + pkt.pts - pkt.dts < thresholdInBase) {
					auto const localOffsetIn180k = timescaleToClock((1LL << stream->pts_wrap_bits) * stream->time_base.num, stream->time_base.den);
					m_host->log(Warning, format("Stream %s: overflow detecting on PTS (%s, new=%s, timescale=%s/%s, offset=%s).",
					        pkt.stream_index, pkt.pts, pkt.pts + localOffsetIn180k, stream->time_base.num, stream->time_base.den, clockToTimescale(demuxStream.offsetIn180k * stream->time_base.num, stream->time_base.den)).c_str());
					pkt.pts += localOffsetIn180k;
				}
			}

			if (pkt.pts != AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE && pkt.pts < pkt.dts) {
				m_host->log(Error, format("Stream %s: pts < dts (%s < %s)", pkt.stream_index, pkt.pts, pkt.dts).c_str());
				return false;
			}

			return true;
		}

		void inputThread() {
			if(highPriority && !setHighThreadPriority())
				m_host->log(Warning, format("Couldn't change reception thread priority to realtime.").c_str());

			bool nextPacketResetFlag = false;
			while (!done) {
				AVPacket pkt;
				av_init_packet(&pkt);

				int status = readFrame(&pkt);

				if (status < 0) {
					av_packet_unref(&pkt);

					if (status == (int)AVERROR_EOF || (m_formatCtx->pb && m_formatCtx->pb->eof_reached)) {
						m_host->log(Info, format("End of stream detected - %s", loop ? "looping" : "leaving").c_str());
						if (loop) {
							seekToStart();
							nextPacketResetFlag = true;
							continue;
						}
					} else if (m_formatCtx->pb && m_formatCtx->pb->error) {
						m_host->log(Error, format("Stream contains an irrecoverable error (%s) - leaving", status).c_str());
					}
					done = true;
					return;
				}

				if (pkt.stream_index >= (int)m_streams.size()) {
					m_host->log(Warning, format("Detected stream index %s that was not initially detected (adding streams dynamically is not supported yet). Discarding packet.", pkt.stream_index).c_str());
					av_packet_unref(&pkt);
					continue;
				}

				if (nextPacketResetFlag) {
					pkt.flags |= AV_PKT_FLAG_RESET_DECODER;
					nextPacketResetFlag = false;
				}

				while (!packetQueue.write(pkt)) {
					if (done) {
						av_packet_unref(&pkt);
						return;
					}
					m_host->log(m_formatCtx->pb && !m_formatCtx->pb->seekable ? Warning : Debug,
					    "Dispatch queue is full - regulating.");
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			}
		}

		void setTimestamp(std::shared_ptr<DataAVPacket> data) {
			auto pkt = data->getPacket();
			if (!pkt->duration) {
				pkt->duration = pkt->dts - m_streams[pkt->stream_index].lastDTS;
			}
			m_streams[pkt->stream_index].lastDTS = pkt->dts;
			auto const base = m_formatCtx->streams[pkt->stream_index]->time_base;
			auto const time = timescaleToClock(pkt->pts * base.num, base.den);
			data->setMediaTime(time - startPTSIn180k);
			int64_t offset;
			if (startPTSIn180k) {
				offset = -startPTSIn180k; //a global offset is applied to all streams (since it is a PTS we may have negative DTSs)
			} else {
				data->setMediaTime(m_streams[pkt->stream_index].restamper->restamp(data->getMediaTime())); //restamp by pid only when no start time
				offset = data->getMediaTime() - time;
			}
			if (offset != 0) {
				data->restamp(offset * base.num, base.den); //propagate to AVPacket
			}
		}

		bool dispatchable(AVPacket * const pkt) {
			if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
				m_host->log(Error, format("Corrupted packet received (DTS=%s).", pkt->dts).c_str());
			}
			if (pkt->dts == AV_NOPTS_VALUE) {
				pkt->dts = m_streams[pkt->stream_index].lastDTS;
				m_host->log(Debug, format("No DTS: setting last value %s.", pkt->dts).c_str());
			}
			if (!m_streams[pkt->stream_index].lastDTS) {
				auto stream = m_formatCtx->streams[pkt->stream_index];
				auto minPts = clockToTimescale(startPTSIn180k*stream->time_base.num, stream->time_base.den);
				if(pkt->pts < minPts) {
					return false;
				}
			}
			return true;
		}

		void dispatch(AVPacket *pkt) {
			auto output = m_streams[pkt->stream_index].output;
			auto out = output->getBuffer(0);
			auto outPkt = out->getPacket();
			av_packet_move_ref(outPkt, pkt);
			if(pkt->flags & AV_PKT_FLAG_RESET_DECODER)
				out->flags |= DATA_FLAGS_DISCONTINUITY;
			setTimestamp(out);
			output->emit(out);
			sparseStreamsHeartbeat(outPkt);
		}

		void sparseStreamsHeartbeat(AVPacket const * const pkt) {
			int64_t curDTS = 0;

			// signal clock from audio to sparse streams
			// (should be the PCR but libavformat doesn't give access to it)
			if (m_formatCtx->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
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
					if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
						auto sparse = m_streams[i].output->getBuffer(0);
						sparse->setMediaTime(curTimeIn180k);
						m_streams[i].output->emit(sparse);
					}
				}
			}
		}

		struct Stream {
			OutputDataDefault<DataAVPacket>* output = nullptr;
			uint64_t offsetIn180k = 0;
			int64_t lastDTS = 0;
			std::unique_ptr<Transform::Restamp> restamper;
		};

		IModuleHost* const m_host;

		std::vector<Stream> m_streams;

		const bool loop;
		bool highPriority = false;
		std::thread workingThread;
		std::atomic_bool done;
		QueueLockFree<AVPacket> packetQueue;
		AVFormatContext* m_formatCtx;
		AVIOContext* m_avioCtx = nullptr;
		const DemuxConfig::ReadFunc m_read;
		int64_t curTimeIn180k = 0, startPTSIn180k = std::numeric_limits<int64_t>::min();

		static int read(void* user, uint8_t* data, int size) {
			auto pThis = (LibavDemux*)user;
			return pThis->m_read(data, size);
		}
};

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto config = va_arg(va, DemuxConfig*);
	enforce(host, "LibavDemux: host can't be NULL");
	enforce(config, "LibavDemux: config can't be NULL");
	return Modules::create<LibavDemux>(host, *config).release();
}

auto const registered = Factory::registerModule("LibavDemux", &createObject);

}
