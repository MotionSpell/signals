/*
Feeds downstream modules with a "clean" signal.

A "clean" signal has the following properties:
 - its timings are continuous (no gaps, overlaps, or discontinuities - but may
   not start at zero), and
 - the different media are synchronized.

The module needs to be sample accurate.
It operates on raw data. Raw data requires a lot of memory, however:
 - We store a short duration (@analyzeWindowInFrames) as the framework works by
   default with pre-allocated pools, and
 - RAM is cheap ;)

The module works this way:
 - At each tick it pulls some data (like a mux would).
 - We rely on clock times. Media times are considered non-reliable and only
   used to achieve sync.
 - Each time the master queue is empty we fill it up to @analyzeWindowInFrames.
 - The different media types are processed differently
   (video = lead, audio = pulled, subtitles = sparse).

The module explicitly assumes that:
 - All media data are available.
 - The input media frame rate is equal to the output framerate. This is not
   checked in any way. The module doesn't make any record nor any check on the
   input media timelines.
 - The input media pace is equal to the injected clock pace. If these clocks
   drift then you may start to see output artifacts (and log messages).
   Therefore the module shall not be used to transframerate.
*/
#include "rectifier.hpp"

#include "lib_media/common/attributes.hpp" // PresentationTime
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/subtitle.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/scheduler.hpp"
#include "lib_utils/tools.hpp" // enforce, safe_cast

#include <algorithm> // remove_if
#include <cassert>
#include <mutex>

using namespace Modules;

namespace {

struct Stream {
  struct Rec {
    int64_t creationTime = -1;
    Data data;
  };

  OutputDefault *output;
  std::vector<Rec> data;
  Rec blank{}; // when we have no data from the input, send this instead.
  PcmFormat fmt{};
};

// a time range, in clock units or audio sample units
struct Interval {
  int64_t start = -1, stop = -1;
};

struct Rectifier : ModuleDynI {
  Rectifier(KHost *host, std::shared_ptr<IClock> clock_, std::shared_ptr<IScheduler> scheduler_, Fraction frameRate)
      : m_host(host)
      , framePeriod(frameRate.inverse())
      , analyzeWindowInFrames(frameRate * masterInputLifetime / 2)
      , clock(clock_)
      , scheduler(scheduler_) {}

  ~Rectifier() {
    std::unique_lock<std::mutex> lock(mutex);
    if(pendingTaskId)
      scheduler->cancel(pendingTaskId);
  }

  void flush() override {
    std::unique_lock<std::mutex> lock(mutex);

    // stop scheduler
    if(pendingTaskId)
      scheduler->cancel(pendingTaskId);

    // flush input queues
    discardOutdatedData(std::numeric_limits<int64_t>::max());
    started = false;
  }

  void process() override {
    if(!started) {
      reschedule(clock->now());
      started = true;
    }
  }

  int getNumOutputs() const override {
    {
      auto pThis = const_cast<Rectifier *>(this);
      std::unique_lock<std::mutex> lock(mutex);
      pThis->mimicOutputs();
    }
    return ModuleDynI::getNumOutputs();
  }

  IOutput *getOutput(int i) override {
    {
      std::unique_lock<std::mutex> lock(mutex);
      mimicOutputs();
    }
    return ModuleDynI::getOutput(i);
  }

  private:
  KHost *const m_host;
  Fraction const framePeriod;
  const Fraction gInputLifetime = Fraction(5000, 1000); // clock time after which data is discarded
  const Fraction masterInputLifetime = Fraction(500,
        1000); // clock time after which master data is discarded - the master stream is the only raw video stream
  const int analyzeWindowInFrames; // target buffer level for the master input
  std::shared_ptr<IClock> const clock;
  std::shared_ptr<IScheduler> const scheduler;
  int64_t numTicks = 0;
  Fraction refClockTime = std::numeric_limits<int64_t>::min();
  bool started = false;

  mutable std::mutex mutex; // protects from stream (outputs, inputs, streams) and task declarations
  std::vector<Stream> streams;
  IScheduler::Id pendingTaskId{};

  // mutex must be owned
  void mimicOutputs() {
    while(streams.size() < getInputs().size()) {
      auto output = addOutput();
      streams.push_back(Stream{output, {}});
    }
  }

  // mutex must be owned
  void reschedule(Fraction when) {
    pendingTaskId = scheduler->scheduleAt(std::bind(&Rectifier::onPeriod, this, std::placeholders::_1), when);
  }

  void onPeriod(Fraction timeNow) {
    std::unique_lock<std::mutex> lock(mutex);
    pendingTaskId = {};
    emitOnePeriod(timeNow);
    reschedule(timeNow + framePeriod);
  }

  void declareScheduler(IInput const *const input, IOutput const *const output) {
    auto const oMeta = output->getMetadata();
    if(!oMeta) {
      m_host->log(Debug, "Output isn't connected or doesn't expose a metadata: impossible to check");
    } else if(input->getMetadata()->type != oMeta->type)
      throw error("Metadata I/O inconsistency");
  }

  // Audio media times may be noisy whereas actual data don't need rectification.
  // The issue is that we can't know on which side of the inaccuracy we start.
  Data rectifyAudioMediaTimes(Data data, const int i) {
    if(streams[i].data.empty())
      return data;

    if(auto const pcm = std::dynamic_pointer_cast<const DataPcm>(data)) {
      auto const accuracy = divUp(1 * IClock::Rate, 90000LL); // 1 sample at 90kHz
      auto const dataPrev = safe_cast<const DataPcm>(streams[i].data.back().data);

      // Operate on samples.
      auto const dataPrevSampleEnd = dataPrev->get<PresentationTime>().time * streams[i].fmt.sampleRate / IClock::Rate +
            dataPrev->getSampleCount();
      auto const dataSampleStart = data->get<PresentationTime>().time * streams[i].fmt.sampleRate / IClock::Rate;

      // Align start time with previous end time when within accuracy. Can't drift by design.
      if(dataPrevSampleEnd != dataSampleStart && std::abs(dataPrevSampleEnd - dataSampleStart) <= accuracy) {
        auto dataClone = data->clone();
        dataClone->set(PresentationTime{timescaleToClock(dataPrevSampleEnd, streams[i].fmt.sampleRate)});
        return dataClone;
      }
    }

    return data;
  }

  // mutex must be owned
  void fillInputQueues() {
    auto const now = fractionToClock(clock->now());

    for(auto i : getInputs()) {
      auto &currInput = inputs[i];

      Data data;
      while(currInput->tryPop(data)) {
        data = rectifyAudioMediaTimes(data, i);
        streams[i].data.push_back({now, data});

        if(currInput->updateMetadata(data))
          declareScheduler(currInput.get(), streams[i].output);
      }
    }
  }

  void discardOutdatedData(const int64_t removalClockTime) {
    for(auto i : getInputs())
      discardStreamOutdatedData(i, removalClockTime);
  }

  void discardStreamOutdatedData(const size_t inputIdx, const int64_t removalClockTime) {
    auto isOutdated = [&](Stream::Rec const &rec) { return rec.creationTime < removalClockTime; };

    auto &data = streams[inputIdx].data;
    data.erase(std::remove_if(data.begin(), data.end(), isOutdated), data.end());
  }

  Stream::Rec chooseNextMasterFrame(Stream &stream, const Fraction now, const int64_t duration) {
    if(refClockTime == std::numeric_limits<int64_t>::min())
      refClockTime = now; // init

    if(stream.data.empty()) {
      refClockTime = now; // reset refClockTime: triggers rebuffering

      // Missing frames are likely due to an issue upstream, compensate media and clock time drift.
      // This way, media from other streams can still be consumed without filling queues waiting for expiration.
      if(stream.blank.data) {
        stream.blank.creationTime = stream.blank.creationTime + duration;
        auto nextBlankData = stream.blank.data->clone();
        nextBlankData->set(PresentationTime{stream.blank.data->get<PresentationTime>().time + duration});
        stream.blank.data = nextBlankData;
        m_host->log(Warning,
              format("Empty master input (pts=%s). Resetting reference clock time to %ss.",
                    stream.blank.data->get<PresentationTime>().time, (double)refClockTime)
                    .c_str());
      }

      return stream.blank;
    }

    auto last = stream.blank;
    auto next = stream.blank = stream.data.front();

    if(now <= refClockTime + framePeriod * analyzeWindowInFrames)
      // still (re)buffering from an empty master input queue
      return stream.blank;

    // Introduce some latency.
    // If the frame is available, but since very little time, use it, but don't remove it.
    // Thus it will be used again next time.
    // This protects us from frame phase changes (e.g on SDI cable replacement).
    if(fractionToClock(now) - next.creationTime < fractionToClock(framePeriod))
      return next;

    stream.data.erase(stream.data.begin()); // don't reuse
    return next;
  }

  int getMasterStreamId() const {
    bool inputsHaveMeta = true;
    for(auto i : getInputs()) {
      auto meta = inputs[i]->getMetadata() ? inputs[i]->getMetadata() : outputs[i]->getMetadata();
      if(!meta)
        inputsHaveMeta = false;
      else if(meta->type == VIDEO_RAW)
        return i;
    }

    return inputsHaveMeta ? -2 : -1;
  }

  // Post one "media period" on all outputs.
  // In this function we distinguish between "in" media times, and "out" media times:
  // - "in" media times are the media times of the input samples.
  //   They might contain gaps, offsets, etc.
  //   Thus, they should only be used for synchronisation between input streams.
  // - "out" media times are the media times of the output samples.
  //   They must be perfectly continuous, increasing, and must not depend in any way
  //   of the input framing.
  //
  // Current remarkable behaviours:
  // - If the master stream is not decidable at startup then an exception is raised.
  // - The scheduler starts at time zero, leading to discard master data with negative timestamps.
  // - The master stream and the slave streams are sent only when metadata is associated (otherwise silently not sent).
  //
  // Mutex must be owned.
  void emitOnePeriod(Fraction now) {
    // needed if not connected and no data received yet
    mimicOutputs();

    // input data management
    fillInputQueues();
    discardOutdatedData(fractionToClock(now - gInputLifetime));

    // output media times corresponding to the "media period"
    auto const outMasterTime = Interval{fractionToClock(Fraction((numTicks + 0) * framePeriod.num, framePeriod.den)),
          fractionToClock(Fraction((numTicks + 1) * framePeriod.num, framePeriod.den))};

    auto const masterStreamId = getMasterStreamId();
    if(masterStreamId == -1) {
      m_host->log(Error, "No master stream: waiting to receive one video stream metadata to start the session");
      return;
    } else if(masterStreamId == -2)
      throw error("No master stream: requires to have one connected video stream");

    // master data
    auto const inMasterTime = emitOnePeriod_Master(masterStreamId, now, outMasterTime);
    if(inMasterTime.start == inMasterTime.stop)
      return;

    discardStreamOutdatedData(masterStreamId, fractionToClock(now - masterInputLifetime));

    // slave data
    for(auto i : getInputs()) {
      if(i == masterStreamId)
        continue;

      auto &input = inputs[i];

      if(!input->getMetadata()) // Romain: huh
        continue;

      if(!outputs[i]->getMetadata())
        outputs[i]->setMetadata(input->getMetadata());

      switch(input->getMetadata()->type) {
      case AUDIO_RAW:
        emitOnePeriod_RawAudio(i, inMasterTime, outMasterTime, now);
        break;
      case SUBTITLE_RAW:
        emitOnePeriod_RawSubtitle(i, inMasterTime, outMasterTime);
        break;
      case VIDEO_RAW:
        throw error("only one video stream is supported");
        break;
      default:
        throw error("unhandled media type");
      }
    }

    ++numTicks;
  }

  // returns input media times corresponding to the "media period"
  Interval emitOnePeriod_Master(const int i, Fraction now, const Interval outMasterTime) {
    auto &master = streams[i];
    auto masterFrame = chooseNextMasterFrame(master, now, outMasterTime.stop - outMasterTime.start);
    if(!masterFrame.data) {
      assert(numTicks == 0);
      m_host->log(Warning, format("No available reference data for clock time %s", fractionToClock(now)).c_str());
      return {};
    } else if(numTicks == 0)
      m_host->log(Info, format("First available reference clock time: %s", fractionToClock(now)).c_str());

    auto data = masterFrame.data->clone();
    data->set(PresentationTime{outMasterTime.start});
    master.output->post(data);

    return {masterFrame.data->get<PresentationTime>().time,
          masterFrame.data->get<PresentationTime>().time + (outMasterTime.stop - outMasterTime.start)};
  }

  void emitOnePeriod_RawAudio(const int i, const Interval inMasterTime, const Interval outMasterTime, Fraction now) {
    auto &stream = streams[i];

    if(!stream.data.empty())
      stream.fmt = safe_cast<const DataPcm>(stream.data.front().data)->format;

    if(stream.fmt.sampleRate == 0)
      throw error("Unknown audio format");

    auto const BPS = stream.fmt.getBytesPerSample() / getNumChannelsFromLayout(stream.fmt.layout);

    // Convert a timestamp to an absolute sample count.
    auto toSamples = [&](int64_t time) -> int64_t { return time * stream.fmt.sampleRate / IClock::Rate; };

    auto getSampleInterval = [&](int64_t pts, uint64_t planeSize) -> Interval {
      auto const start = pts * stream.fmt.sampleRate / IClock::Rate;
      return {start, start + int64_t(planeSize / BPS)};
    };

    // Convert all times to absolute sample counts. This way we handle early all precision issues.
    // Doing computations in sample counts (instead of clock rate) allows us to ensure sample accuracy.
    const Interval inMasterSamples = {toSamples(inMasterTime.start), toSamples(inMasterTime.stop)};
    const Interval outMasterSamples = {toSamples(outMasterTime.start),
          toSamples(outMasterTime.start) + (inMasterSamples.stop - inMasterSamples.start)};

    // Create an zeroed output sample.
    // We want it to start at 'outMasterTime.start' and to cover the full 'outMasterTime' interval.
    auto pcm = stream.output->allocData<DataPcm>(outMasterSamples.stop - outMasterSamples.start, stream.fmt);
    pcm->set(PresentationTime{outMasterTime.start});
    for(int i = 0; i < pcm->format.numPlanes; ++i)
      memset(pcm->getPlane(i), 0, pcm->getPlaneSize());

    // Remove obsolete samples wrt media time.
    auto isObsolete = [&](Stream::Rec const &rec) {
      auto const inputData = safe_cast<const DataPcm>(rec.data);
      auto const inSamples = getSampleInterval(rec.data->get<PresentationTime>().time, inputData->getPlaneSize());
      return inSamples.stop < inMasterSamples.start;
    };
    stream.data.erase(std::remove_if(stream.data.begin(), stream.data.end(), isObsolete), stream.data.end());

    int writtenSamples = 0;
    int64_t obsolescenceCreationTime = -1;

    // Fill the period "outMasterSamples" with portions of input audio samples
    // that intersect with the "media period".
    for(auto &data : stream.data) {
      auto const inputData = safe_cast<const DataPcm>(data.data);
      auto const inSamples = getSampleInterval(data.data->get<PresentationTime>().time, inputData->getPlaneSize());
      assert(inSamples.stop - inSamples.start == inputData->getSampleCount());

      // Intersect period of this data with the "media period".
      auto const left = std::max(inSamples.start, inMasterSamples.start);
      auto const right = std::min(inSamples.stop, inMasterSamples.stop);

      // Intersection is empty.
      if(left >= right)
        continue;

      // Store clock time of the oldest used data.
      if(obsolescenceCreationTime == -1)
        obsolescenceCreationTime = data.creationTime;

      for(int i = 0; i < stream.fmt.numPlanes; ++i) {
        auto src = inputData->getPlane(i) + (left - inSamples.start) * BPS;
        assert((right - inSamples.start) * BPS <= (int64_t)inputData->getPlaneSize());
        auto dst = pcm->getPlane(i) + (left - inMasterSamples.start) * BPS;
        assert((right - inMasterSamples.start) * BPS <= (int64_t)pcm->getPlaneSize());
        memcpy(dst, src, (right - left) * BPS);
      }

      writtenSamples += (right - left);
    }

    if(writtenSamples != (inMasterSamples.stop - inMasterSamples.start)) {
      m_host->log(Warning,
            format("Incomplete audio period (%s samples instead of %s - queue size %s (v=%s)). Expect glitches.",
                  writtenSamples, inMasterSamples.stop - inMasterSamples.start, stream.data.size(),
                  streams[0].data.size())
                  .c_str());

      if(0) { // debug traces
        printf("\t[%lf - %lf] now=%lf\n", inMasterTime.start / (double)IClock::Rate,
              inMasterTime.stop / (double)IClock::Rate, (double)now);

        for(auto i : getInputs()) {
          printf("\t[%d] queue size=%d", i, (int)streams[i].data.size());
          if(!streams[i].data.empty())
            printf(" - [media]=[%lf-%lf] (clock)=(%lf-%lf)",
                  streams[i].data.front().data->get<PresentationTime>().time / (double)IClock::Rate,
                  streams[i].data.back().data->get<PresentationTime>().time / (double)IClock::Rate,
                  streams[i].data.front().creationTime / (double)IClock::Rate,
                  streams[i].data.back().creationTime / (double)IClock::Rate);
          printf("\n");
        }

        if(1)
          for(auto &data : stream.data) {
            auto const inputData = safe_cast<const DataPcm>(data.data);
            auto const inSamples =
                  getSampleInterval(data.data->get<PresentationTime>().time, inputData->getPlaneSize());
            printf("\t\t %lld - %lld  (%lld) [%lf]\n", (long long int)inSamples.start, (long long int)inSamples.stop,
                  (long long int)(inSamples.stop - inSamples.start), data.creationTime / (double)IClock::Rate);
          }

        fflush(stdout);
      }
    }

    // Remove obsolete samples wrt clock time.
    discardStreamOutdatedData(i, obsolescenceCreationTime);

    stream.output->post(pcm);
  }

  // Sparse stream data is dispatched immediately:
  // - it may last longer than one rectifier period;
  // - it may arrive way in advance and be discarded unduely.
  void emitOnePeriod_RawSubtitle(const int i, const Interval inMasterTime, const Interval outMasterTime) {
    auto &stream = streams[i];

    auto data = stream.data.begin();
    while(data != stream.data.end()) {
      auto const sub = safe_cast<const DataSubtitle>(data->data);
      if(sub->page.hideTimestamp >= inMasterTime.start) {
        auto dataOut = safe_cast<DataSubtitle>(sub->clone());

        auto const delta = inMasterTime.start - outMasterTime.start;
        dataOut->set(PresentationTime{outMasterTime.start});
        dataOut->page.showTimestamp += delta;
        dataOut->page.hideTimestamp += delta;
        stream.output->post(dataOut);

        data = stream.data.erase(data);
      } else {
        // Don't assume data arrives in order: continue processing.
        ++data;
      }
    }

    // Send heartbeat.
    auto heartbeat = stream.output->allocData<DataSubtitle>(0);
    heartbeat->set(PresentationTime{outMasterTime.start});
    stream.output->post(heartbeat);
  }
};

IModule *createObject(KHost *host, void *va) {
  auto config = (RectifierConfig *)va;
  enforce(host, "Rectifier: host can't be NULL");
  enforce(config, "Rectifier: config can't be NULL");
  return createModuleWithSize<Rectifier>(100, host, config->clock, config->scheduler, config->frameRate).release();
}

auto const registered = Factory::registerModule("Rectifier", &createObject);
}
