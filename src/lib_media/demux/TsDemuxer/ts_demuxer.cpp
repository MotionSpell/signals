// MPEG-TS push demuxer.
//
// The design goal here is to discard the biggest amount of input
// data (which is often erroneous) while still getting the job done.
//
#include "ts_demuxer.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_utils/log_sink.hpp" // Error
#include "lib_utils/format.hpp"
#include <vector>
using namespace std;

using namespace Modules;

namespace {

auto const TS_PACKET_LEN = 188;
auto const PID_PAT = 0;

#include "stream.hpp"
#include "pes_stream.hpp"
#include "psi_stream.hpp"

struct TsDemuxer : ModuleS, PsiStream::Listener, PesStream::Restamper {
		TsDemuxer(IModuleHost* host, TsDemuxerConfig const& config)
			: m_host(host) {

			addInput(this);

			m_streams.push_back(make_unique<PsiStream>(PID_PAT, m_host, this));

			for(auto& pid : config.pids)
				if(pid.type != TsDemuxerConfig::NONE)
					m_streams.push_back(make_unique<PesStream>(pid.pid, pid.type, this, m_host, addOutput<OutputDefault>()));
		}

		void process(Data data) override {
			auto buf = data->data();

			while(buf.len > 0) {
				if(buf.len < TS_PACKET_LEN) {
					m_host->log(Error, "Truncated TS packet");
					return;
				}

				try {
					processTsPacket({buf.ptr, TS_PACKET_LEN});
				} catch(exception const& e) {
					m_host->log(Error, e.what());
				}
				buf += TS_PACKET_LEN;
			}
		}

		void flush() {
			for(auto& s : m_streams)
				s->flush();
		}

		// PsiStream::Listener implementation
		void onPat(span<int> pmtPids) override {
			m_host->log(Debug, format("Found PAT (%s programs)", pmtPids.len).c_str());
			for(auto pid : pmtPids)
				m_streams.push_back(make_unique<PsiStream>(pid, m_host, this));
		}

		void onPmt(span<PsiStream::EsInfo> esInfo) override {
			m_host->log(Debug, format("Found PMT (%s streams)", esInfo.len).c_str());
			for(auto es : esInfo) {
				if(auto stream = findMatchingStream(es)) {
					stream->pid = es.pid;
					if(stream->setType(es.mpegStreamType))
						m_host->log(Debug, format("[%s] MPEG stream type %s", es.pid, es.mpegStreamType).c_str());
					else
						m_host->log(Warning, format("[%s] unknown MPEG stream type: %s", es.pid, es.mpegStreamType).c_str());
				}
			}
		}

		// PesStream::Restamper implementation
		void restamp(int64_t& pts) override {

			// unroll PTS: ensure it's not too far from the last unrolled PTS
			{
				auto const PTS_PERIOD = 1LL << 33;
				while(pts < (m_lastUnrolledPts-PTS_PERIOD/2))
					pts += PTS_PERIOD;

				m_lastUnrolledPts = pts;
			}

			// make the timestamp start from zero
			{
				if(m_ptsOrigin == INT64_MAX)
					m_ptsOrigin = pts;

				pts -= m_ptsOrigin;
			}
		}

	private:
		void processTsPacket(SpanC pkt) {
			BitReader r = {pkt};
			const int syncByte = r.u(8);
			const int transportErrorIndicator = r.u(1);
			const int payloadUnitStartIndicator = r.u(1);
			/*const int priority =*/ r.u(1);
			const int packetId = r.u(13);
			const int scrambling = r.u(2);
			const int adaptationFieldControl = r.u(2);
			const int continuityCounter = r.u(4);

			if(syncByte != 0x47) {
				m_host->log(Error, "TS sync byte not found");
				return;
			}

			// skip adaptation field if any
			if(adaptationFieldControl & 0b10) {
				auto length = r.u(8);
				skip(r, length, "adaptation_field length in TS header");
			}

			auto stream = findStreamForPid(packetId);
			if(!stream)
				return; // we're not interested in this PID

			if(transportErrorIndicator) {
				m_host->log(Error, "Discarding TS packet with TEI=1");
				return;
			}

			if(scrambling) {
				m_host->log(Error, "Discarding scrambled TS packet");
				return;
			}

			if(continuityCounter == stream->cc)
				return; // discard duplicated packet

			stream->cc = continuityCounter;

			if(payloadUnitStartIndicator)
				stream->flush();

			if(adaptationFieldControl & 0b01) {
				stream->push(r.payload(), payloadUnitStartIndicator);
			}
		}

		PesStream* findMatchingStream(PsiStream::EsInfo es) {
			for(auto& s : m_streams) {
				if(auto stream = dynamic_cast<PesStream*>(s.get()))
					if(matches(stream, es))
						return stream;
			}

			return nullptr;
		}

		static bool matches(PesStream* stream, PsiStream::EsInfo es) {
			if(stream->pid == TsDemuxerConfig::ANY) {
				auto meta = createMetadata(es.mpegStreamType);
				if(!meta)
					return false;
				switch(stream->type) {
				case TsDemuxerConfig::AUDIO: return meta->type == AUDIO_PKT;
				case TsDemuxerConfig::VIDEO: return meta->type == VIDEO_PKT;
				default: // invalid configuration
					assert(0);
					return false;
				}
			} else {
				return stream->pid == es.pid;
			}
		}

		Stream* findStreamForPid(int packetId) {
			for(auto& s : m_streams) {
				if(s->pid == packetId)
					return s.get();
			}
			return nullptr;
		}

		IModuleHost* const m_host;
		vector<unique_ptr<Stream>> m_streams;
		int64_t m_ptsOrigin = INT64_MAX;
		int64_t m_lastUnrolledPts = 0;
};

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto config = va_arg(va, TsDemuxerConfig*);
	enforce(host, "TsDemuxer: host can't be NULL");
	enforce(config, "TsDemuxer: config can't be NULL");
	return Modules::create<TsDemuxer>(host, *config).release();
}
}

auto const registered = Factory::registerModule("TsDemuxer", &createObject);

