#include "mpeg_dash_input.hpp"

#include "../common/metadata.hpp" // MetadataPkt
#include "../common/mpeg_dash_parser.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/queue.hpp"
#include "lib_utils/time.hpp" // getUTC
#include "lib_utils/tools.hpp"

#include <algorithm> // max
#include <atomic>
#include <chrono>
#include <cstring> // memcpy
#include <map>
#include <thread>

using namespace std;
using namespace Modules::In;

string expandVars(string input, map<string, string> const &values);

namespace Modules { namespace In {

/*binary threaded executor: will execute the latest queued request only*/
struct BinaryExecutor {
  BinaryExecutor(exception_ptr eptr)
      : eptr(eptr)
      , th(&BinaryExecutor::threadProc, this)
      , fct(nullptr) {
    done = false;
  }

  ~BinaryExecutor() {
    fct = nullptr; // cancel unflushed task (if any) and signal end of queue
    done = true; // unlock queue
    th.join(); // return as soon as current processing is finished
  }

  void post(function<void()> next, bool priority = false) {
    if(eptr)
      rethrow_exception(eptr);

    unique_lock<mutex> lock(m);
    if(fct && !priority)
      return;

    fct = next;
  }

  private:
  void threadProc() {
    try {
      while(!done) {
        function<void()> next = nullptr;

        {
          unique_lock<mutex> lock(m);
          if(fct) {
            next = fct;
            fct = nullptr;
          }
        }

        if(next)
          next();
        else
          this_thread::sleep_for(chrono::milliseconds(100));
      }
    } catch(...) {
      eptr = current_exception();
    }
  }

  exception_ptr eptr;
  thread th;
  mutex m; // protects @fct
  function<void()> fct;
  std::atomic_bool done;
};

struct MPEG_DASH_Input::Stream {
  Stream(OutputDefault *out,
        Representation const *rep,
        Fraction segmentDuration,
        unique_ptr<IFilePuller> source,
        exception_ptr eptr)
      : out(out)
      , rep(rep)
      , segmentDuration(segmentDuration)
      , source(std::move(source))
      , executor(new BinaryExecutor(eptr)) {}

  OutputDefault *out;
  Representation const *rep;
  bool initializationChunkSent = false;
  int64_t currNumber = 0;
  Fraction segmentDuration;
  unique_ptr<IFilePuller> source;
  unique_ptr<BinaryExecutor> executor;
};

static string dirName(string path) {
  auto i = path.rfind('/');
  if(i != path.npos)
    path = path.substr(0, i);
  return path;
}

shared_ptr<IMetadata> MPEG_DASH_Input::createMetadata(Representation const &rep) {
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

MPEG_DASH_Input::MPEG_DASH_Input(KHost *host, IFilePullerFactory *filePullerFactory, string const &url)
    : m_host(host) {
  m_host->activate(true);

  // GET MPD FROM HTTP
  auto mpdAsText = download(filePullerFactory->create().get(), url.c_str());
  if(mpdAsText.empty())
    throw runtime_error("can't get mpd");
  m_mpdDirname = dirName(url);

  // PARSE MPD
  mpd = parseMpd({(const char *)mpdAsText.data(), mpdAsText.size()});

  // DECLARE OUTPUT PORTS
  for(auto &set : mpd->sets) {
    if(!set.representations.empty()) {
      auto &rep = set.representations.front();
      auto meta = createMetadata(rep);
      if(!meta) {
        m_host->log(Warning, format("Ignoring Representation with unrecognized mime type: '%s'", rep.mimeType).c_str());
        continue;
      }

      auto out = addOutput();
      out->setMetadata(meta);
      auto stream = make_unique<Stream>(
            out, &rep, Fraction(rep.duration(mpd.get()), rep.timescale(mpd.get())), filePullerFactory->create(), eptr);
      m_streams.push_back(std::move(stream));
    }
  }

  for(auto &stream : m_streams) {
    stream->currNumber = stream->rep->startNumber(mpd.get());
    if(mpd->dynamic) {
      auto now = mpd->publishTime;
      if(!mpd->publishTime)
        now = (int64_t)getUTC();

      if(stream->segmentDuration.num == 0)
        throw runtime_error("No duration for stream");

      stream->currNumber += int64_t(stream->segmentDuration.inverse() * (now - mpd->availabilityStartTime)) / 1000 + 1;
      // HACK: add one segment latency
      stream->currNumber = std::max<int64_t>(stream->currNumber - 2, stream->rep->startNumber(mpd.get()));
    }
  }
}

MPEG_DASH_Input::~MPEG_DASH_Input() {}

void MPEG_DASH_Input::processStream(Stream *stream) {
  // evaluate once at start as it may be modified from another thread
  auto rep = stream->rep;

  if(!rep) {
    // this adaptation set is disabled: move to the next step
    if(stream->initializationChunkSent)
      stream->currNumber++;

    return;
  }

  if(mpd->periodDuration) {
    if(stream->segmentDuration * (stream->currNumber - rep->startNumber(mpd.get())) >= mpd->periodDuration) {
      m_host->log(Info, "End of period");
      m_host->activate(false);
      return;
    }
  }

  string url;

  {
    map<string, string> vars;

    vars["RepresentationID"] = rep->id;

    if(stream->initializationChunkSent) {
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
  try {
    stream->source->wget(url.c_str(), onBuffer);
  } catch(std::runtime_error const &) {
    m_host->activate(false);
    throw;
  }

  if(empty) {
    if(mpd->dynamic) {
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
  for(auto &s : m_streams)
    if(s->rep)
      allDisabled = false;

  if(allDisabled) {
    // all streams disabled, stop session
    m_host->activate(false);
    return;
  }

  for(auto &stream : m_streams)
    stream->executor->post(std::bind(&MPEG_DASH_Input::processStream, this, stream.get()));
}

int MPEG_DASH_Input::getNumAdaptationSets() const { return getNumOutputs(); }

int MPEG_DASH_Input::getNumRepresentationsInAdaptationSet(int adaptationSetIdx) const {
  if(adaptationSetIdx < 0 || adaptationSetIdx >= (int)m_streams.size())
    throw error("getNumRepresentationsInAdaptationSet(): wrong index");

  return m_streams[adaptationSetIdx]->rep->set(mpd.get()).representations.size();
}

std::string MPEG_DASH_Input::getSRD(int adaptationSetIdx) const {
  if(adaptationSetIdx < 0 || adaptationSetIdx >= (int)m_streams.size())
    throw error("getSRD(): wrong AdaptationSet index");

  return m_streams[adaptationSetIdx]->rep->set(mpd.get()).srd;
}

void MPEG_DASH_Input::enableStream(int asIdx, int repIdx) {
  if(asIdx < 0 || asIdx >= (int)m_streams.size())
    throw error("enableStream(): wrong adaptation set index");

  if(repIdx < 0 || repIdx >= (int)m_streams[asIdx]->rep->set(mpd.get()).representations.size())
    throw error("enableStream(): wrong representation index");

  m_streams[asIdx]->executor->post(
        [this, asIdx, repIdx]() {
          auto curRep = m_streams[asIdx]->rep;
          auto &newRep = curRep->set(mpd.get()).representations[repIdx];
          m_streams[asIdx]->currNumber += newRep.startNumber(mpd.get()) - curRep->startNumber(mpd.get());
          m_streams[asIdx]->rep = &newRep;
        },
        true);
}

void MPEG_DASH_Input::disableStream(int asIdx) {
  if(asIdx < 0 || asIdx >= (int)m_streams.size())
    throw error("disableStream(): wrong adaptation set index");

  m_streams[asIdx]->executor->post([this, asIdx]() { m_streams[asIdx]->rep = nullptr; }, true);
}

}}
