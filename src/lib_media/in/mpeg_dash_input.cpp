#include "lib_utils/tools.hpp"
#include "lib_utils/time.hpp" // getUTC
#include "lib_utils/log_sink.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/queue.hpp"
#include "../common/metadata.hpp" // MetadataPkt
#include "../common/mpeg_dash_parser.hpp"
#include "mpeg_dash_input.hpp"
#include <algorithm> // max
#include <chrono>
#include <cstring> // memcpy
#include <map>
#include <thread>

using namespace std;
using namespace Modules::In;

string expandVars(string input, map<string,string> const& values);

namespace Modules {
namespace In {

/*binary semaphore blocking post() with a thread executor*/
struct BinaryBlockingExecutor {
		BinaryBlockingExecutor(exception_ptr eptr)
			: eptr(eptr), th(&BinaryBlockingExecutor::threadProc, this) {
		}

		~BinaryBlockingExecutor() {
			q.push(nullptr);
			qEmpty.notify_all();
			th.join();
		}

		void post(function<void()> fct) {
			{
				unique_lock<mutex> lock(m);
				while (count == 0) {
					if (eptr)
						rethrow_exception(eptr);

					qEmpty.wait_for(lock, chrono::milliseconds(100));
				}
				count--;
			}

			q.push(fct);
		}

	private:
		void threadProc() {
#ifdef SIGNALS_SET_THREAD_NAME
            pthread_setname_np("signals::MPEG_DASH_InputStream");
#endif
			try {
				while (auto f = q.pop()) {
					{
						unique_lock<mutex> lock(m);
						count++;
						qEmpty.notify_one();
					}

					f();
				}
			} catch(...) {
				eptr = current_exception();
			}
		}

		exception_ptr eptr;
		thread th;
		mutex m;
		Queue<function<void()>> q;
		int count = 1;
		condition_variable qEmpty;
};

struct MPEG_DASH_Input::Stream {
	Stream(OutputDefault* out, Representation const * rep, Fraction segmentDuration, unique_ptr<IFilePuller> source, exception_ptr eptr)
		: out(out), rep(rep), segmentDuration(segmentDuration), source(std::move(source)), executor(new BinaryBlockingExecutor(eptr)) {
	}

	OutputDefault* out;
	Representation const * rep;
	bool initializationChunkSent = false;
	int64_t currNumber = 0;
	Fraction segmentDuration;
	unique_ptr<IFilePuller> source;
	unique_ptr<BinaryBlockingExecutor> executor;
};

static string dirName(string path) {
	auto i = path.rfind('/');
	if(i != path.npos)
		path = path.substr(0, i);
	return path;
}

shared_ptr<IMetadata> MPEG_DASH_Input::createMetadata(Representation const& rep) {
	if(rep.mimeType == "audio/mp4" || rep.set(mpd.get()).contentType == "audio") {
		auto meta = make_shared<MetadataPkt>(AUDIO_PKT);
		meta->codec = rep.codecs;
		return meta;
	} else if(rep.mimeType == "video/mp4" || rep.set(mpd.get()).contentType == "video") {
		auto meta = make_shared<MetadataPkt>(VIDEO_PKT);
		meta->codec = rep.codecs;
		return meta;
	} else {
		return nullptr;
	}
}

MPEG_DASH_Input::MPEG_DASH_Input(KHost* host, IFilePullerFactory *filePullerFactory, string const& url)
	:  m_host(host) {
	m_host->activate(true);

	//GET MPD FROM HTTP
	auto mpdAsText = download(filePullerFactory->create().get(), url.c_str());
	if(mpdAsText.empty())
		throw runtime_error("can't get mpd");
	m_mpdDirname = dirName(url);

	//PARSE MPD
	mpd = parseMpd({(const char*)mpdAsText.data(), mpdAsText.size()});

	//DECLARE OUTPUT PORTS
	for(auto& set : mpd->sets) {
		if (!set.representations.empty()) {
			auto &rep = set.representations.front();
			auto meta = createMetadata(rep);
			if(!meta) {
				m_host->log(Warning, format("Ignoring Representation with unrecognized mime type: '%s'", rep.mimeType).c_str());
				continue;
			}

			auto out = addOutput();
			out->setMetadata(meta);
			auto stream = make_unique<Stream>(out, &rep, Fraction(rep.duration(mpd.get()), rep.timescale(mpd.get())), filePullerFactory->create(), eptr);
			m_streams.push_back(move(stream));
		}
	}

	for(auto& stream : m_streams) {
		stream->currNumber = stream->rep->startNumber(mpd.get());
		if(mpd->dynamic) {
			auto now = mpd->publishTime;
			if(!mpd->publishTime)
				now = (int64_t)getUTC();

			if (stream->segmentDuration.num == 0)
				throw runtime_error("No duration for stream");

			stream->currNumber += int64_t(stream->segmentDuration.inverse() * (now - mpd->availabilityStartTime));
			// HACK: add one segment latency.
            // HACK modified by jack: also add at least a second, to cater for the times
            // in the MPD to have a 1-second resolution.
            int leeway = 2;
            leeway += int(stream->segmentDuration.inverse());
			stream->currNumber = std::max<int64_t>(stream->currNumber-leeway, stream->rep->startNumber(mpd.get()));
		}
	}
}

MPEG_DASH_Input::~MPEG_DASH_Input() {
}

void MPEG_DASH_Input::processStream(Stream* stream) {
	//evaluate once at start as it may be modified from another thread
	auto rep = stream->rep;

	if (!rep) {
		// this adaptation set is disabled: move to the next step
		if (stream->initializationChunkSent)
			stream->currNumber++;

		return;
	}

	if (mpd->periodDuration) {
		if (stream->segmentDuration * (stream->currNumber - rep->startNumber(mpd.get())) >= mpd->periodDuration) {
			m_host->log(Info, "End of period");
			m_host->activate(false);
			return;
		}
	}

	string url;

	{
		map<string, string> vars;

		vars["RepresentationID"] = rep->id;

		if (stream->initializationChunkSent) {
			vars["Number"] = format("%s", stream->currNumber);
			stream->currNumber++;
			url = m_mpdDirname + "/" + expandVars(rep->media(mpd.get()), vars);
		} else {
			url = m_mpdDirname + "/" + expandVars(rep->initialization(mpd.get()), vars);
		}
	}

	m_host->log(Debug, format("wget: '%s'", url).c_str());

	bool empty = true;

	auto onBuffer = [&](SpanC chunk) {
		empty = false;

		auto data = make_shared<DataRaw>(chunk.len);
		memcpy(data->buffer->data().ptr, chunk.ptr, chunk.len);
		stream->out->post(data);
	};

	stream->source->wget(url.c_str(), onBuffer);
	if (empty) {
		if (mpd->dynamic) {
			stream->currNumber = std::max<int64_t>(stream->currNumber - 1, rep->startNumber(mpd.get())); // too early, retry
			return;
		}
		m_host->log(Error, format("can't download file: '%s'", url).c_str());
		m_host->activate(false);
	}

	stream->initializationChunkSent = true;
}

void MPEG_DASH_Input::process() {
	bool allDisabled = true;
	for (auto &s : m_streams)
		if (s->rep)
			allDisabled = false;

	if (allDisabled) {
		// all streams disabled, stop session
		m_host->activate(false);
		return;
	}

	for(auto& stream : m_streams)
		stream->executor->post(std::bind(&MPEG_DASH_Input::processStream, this, stream.get()));
}

int MPEG_DASH_Input::getNumAdaptationSets() const {
	return getNumOutputs();
}

int MPEG_DASH_Input::getNumRepresentationsInAdaptationSet(int adaptationSetIdx) const {
	if (adaptationSetIdx < 0 || adaptationSetIdx >= (int)m_streams.size())
		throw error("getNumRepresentationsInAdaptationSet(): wrong index");

	return m_streams[adaptationSetIdx]->rep->set(mpd.get()).representations.size();
}

std::string MPEG_DASH_Input::getSRD(int adaptationSetIdx) const {
	if (adaptationSetIdx < 0 || adaptationSetIdx >= (int)m_streams.size())
		throw error("getSRD(): wrong AdaptationSet index");

	return m_streams[adaptationSetIdx]->rep->set(mpd.get()).srd;
}

void MPEG_DASH_Input::enableStream(int asIdx, int repIdx) {
	if (asIdx < 0 || asIdx >=(int)m_streams.size())
		throw error("enableStream(): wrong adaptation set index");

	if (repIdx < 0 || repIdx >= (int)m_streams[asIdx]->rep->set(mpd.get()).representations.size())
		throw error("enableStream(): wrong representation index");

	m_streams[asIdx]->executor->post([this, asIdx, repIdx]() {
		auto &newRep = m_streams[asIdx]->rep->set(mpd.get()).representations[repIdx];
		m_streams[asIdx]->currNumber += newRep.startNumber(mpd.get()) - m_streams[asIdx]->rep->startNumber(mpd.get());
		m_streams[asIdx]->rep = &newRep;
	});
}

void MPEG_DASH_Input::disableStream(int asIdx) {
	if (asIdx < 0 || asIdx >= (int)m_streams.size())
		throw error("disableStream(): wrong adaptation set index");

	m_streams[asIdx]->executor->post([this, asIdx]() {
		m_streams[asIdx]->rep = nullptr;
	});
}

}
}

