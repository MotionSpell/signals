#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_media/decode/decoder.hpp"
#include "lib_media/demux/gpac_demux_mp4_simple.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/transform/audio_convert.hpp"
#include "lib_media/transform/restamp.hpp"
#include "lib_media/utils/recorder.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log.hpp"
#include "lib_utils/tools.hpp" // safe_cast
#include "tests/tests.hpp"

#include <cmath>

using namespace Tests;
using namespace Modules;

typedef std::shared_ptr<IModule> (*CreateDemuxFunc)(const char *path);

std::vector<int64_t> runDemux(std::string basename, CreateDemuxFunc createDemux) {
  std::vector<int64_t> actualTimes;
  auto onFrame = [&](Data data) {
    if(isDeclaration(data))
      return;
    actualTimes.push_back(data->get<PresentationTime>().time);
  };

  auto demux = createDemux((basename + ".mp4").c_str());
  ConnectOutput(demux->getOutput(0), onFrame);
  for(int i = 0; i < 100; ++i)
    demux->process();
  return actualTimes;
}

std::shared_ptr<IModule> createLibavDemux(const char *path) {
  DemuxConfig cfg;
  cfg.url = path;
  return loadModule("LibavDemux", &NullHost, &cfg);
}

std::shared_ptr<IModule> createGpacDemux(const char *path) {
  Mp4DemuxConfig cfg{path};
  return loadModule("GPACDemuxMP4Simple", &NullHost, &cfg);
}

unittest("timestamps start at random values (LibavDemux)") {
  const std::vector<int64_t> timesOut = {0};
  ASSERT_EQUALS(timesOut, runDemux("data/start_at_random_value", &createLibavDemux));
}

unittest("timestamps start at random values (GPACDemuxMP4Simple)") {
  const auto interval = IClock::Rate;
  const std::vector<int64_t> timesIn = {interval, 2 * interval, 3 * interval};

  ASSERT_EQUALS(timesIn, runDemux("data/start_at_random_value", createGpacDemux));
}

unittest("timestamps start at a negative value (LibavDemux)") {
  const std::vector<int64_t> timesOut = {0};

  ASSERT_EQUALS(timesOut, runDemux("data/start_at_negative_value", &createLibavDemux));
}

unittest("timestamps start at a negative value (GPACDemuxMP4Simple)") {
  const auto interval = IClock::Rate;
  const std::vector<int64_t> timesIn = {-interval, 0, interval};

  ASSERT_EQUALS(timesIn, runDemux("data/start_at_negative_value", &createGpacDemux));
}

unittest("timestamps start at zero with B-Frames (GPACDemuxMP4Simple)") {
  const auto interval = IClock::Rate;
  const std::vector<int64_t> timesOut = {0, 2 * interval, interval};

  ASSERT_EQUALS(timesOut, runDemux("data/start_at_zero_with_b_frames", &createGpacDemux));
}

unittest("timestamps start at a negative value with B-Frames (GPACDemuxMP4Simple)") {
  const auto interval = IClock::Rate;
  const std::vector<int64_t> timesOut = {-interval, interval, 0};

  ASSERT_EQUALS(timesOut, runDemux("data/start_at_negative_value_with_b_frames", &createGpacDemux));
}

#include "lib_media/encode/libav_encode.hpp"

unittest("transcoder with reframers: test a/v sync recovery") {
  const int64_t maxDurIn180k = 2 * IClock::Rate;
  const size_t bufferSize = (maxDurIn180k * 1000) / (20 * IClock::Rate);

  struct Gapper : public ModuleS {
    Gapper() { output = addOutput(); }
    void processOne(Data data) override {
      if(!isDeclaration(data) && (i++ % 5) && (data->get<PresentationTime>().time < maxDurIn180k)) {
        output->post(data);
      }
    }
    uint64_t i = 0;
    OutputDefault *output;
  };

  auto createEncoder = [&](Metadata metadataDemux, PictureFormat &dstFmt) -> std::shared_ptr<IModule> {
    auto const codecType = metadataDemux->type;
    if(codecType == VIDEO_PKT) {
      EncoderConfig p{EncoderConfig::Video};
      p.bufferSize = bufferSize;
      auto m = loadModule("Encoder", &NullHost, &p);
      dstFmt.format = p.pixelFormat;
      return m;
    } else if(codecType == AUDIO_PKT) {
      EncoderConfig p{EncoderConfig::Audio};
      p.bufferSize = bufferSize;
      return loadModule("Encoder", &NullHost, &p);
    } else
      throw std::runtime_error("[Converter] Found unknown stream");
  };
  auto createConverter = [&](Metadata metadataDemux, const PictureFormat &dstFmt) -> std::shared_ptr<IModule> {
    auto const codecType = metadataDemux->type;
    if(codecType == VIDEO_PKT) {
      return loadModule("VideoConvert", &NullHost, &dstFmt);
    } else if(codecType == AUDIO_PKT) {
      auto const demuxFmt = toPcmFormat(safe_cast<const MetadataPktAudio>(metadataDemux));
      auto const format = PcmFormat(demuxFmt.sampleRate, demuxFmt.numChannels, demuxFmt.layout, demuxFmt.sampleFormat,
            (demuxFmt.numPlanes == 1) ? Interleaved : Planar);
      auto cfg = AudioConvertConfig{{0}, format, 1024};
      return loadModule("AudioConvert", &NullHost, &cfg);
    } else
      throw std::runtime_error("[Converter] Found unknown stream");
  };

  DemuxConfig cfg;
  cfg.url = "data/beepbop.mp4";
  auto demux = loadModule("LibavDemux", &NullHost, &cfg);
  std::vector<std::shared_ptr<IModule>> modules;
  std::vector<std::unique_ptr<Utils::Recorder>> recorders;
  for(int i = 0; i < demux->getNumOutputs(); ++i) {
    auto const metadataDemux = safe_cast<const MetadataPkt>(demux->getOutput(i)->getMetadata());
    if(!metadataDemux->isVideo())
      continue;

    auto gapper = createModule<Gapper>();
    ConnectOutputToInput(demux->getOutput(i), gapper->getInput(0));
    DecoderConfig decCfg;
    decCfg.type = metadataDemux->type;
    auto decoder = loadModule("Decoder", &NullHost, &decCfg);
    ConnectOutputToInput(gapper->getOutput(0), decoder->getInput(0));

    auto inputRes = safe_cast<const MetadataPktVideo>(demux->getOutput(i)->getMetadata())->resolution;
    PictureFormat encoderInputPicFmt(inputRes, PixelFormat::UNKNOWN);
    auto encoder = createEncoder(metadataDemux, encoderInputPicFmt);
    auto converter = createConverter(metadataDemux, encoderInputPicFmt);
    ConnectOutputToInput(decoder->getOutput(0), converter->getInput(0));
    ConnectOutputToInput(converter->getOutput(0), encoder->getInput(0));

    auto recorder = createModule<Utils::Recorder>(&NullHost);
    ConnectOutputToInput(encoder->getOutput(0), recorder->getInput(0));
    recorders.push_back(std::move(recorder));
    modules.push_back(std::move(gapper));
    modules.push_back(std::move(decoder));
    modules.push_back(std::move(converter));
    modules.push_back(std::move(encoder));
  }

  for(int i = 0; i < 1000; ++i)
    demux->process();

  for(auto &m : modules) {
    m->flush();
  }

  for(size_t g = 0; g < recorders.size(); ++g) {
    recorders[g]->processOne(nullptr);
    int64_t lastMediaTime = 0;
    while(auto data = recorders[g]->pop()) {
      if(isDeclaration(data))
        continue;
      g_Log->log(Debug, format("recv[%s] %s", g, data->get<PresentationTime>().time).c_str());
      lastMediaTime = data->get<PresentationTime>().time;
    }
    ASSERT(llabs(maxDurIn180k - lastMediaTime) < maxDurIn180k / 30);
  }
}

unittest("restamp: passthru with offsets") {
  auto const time = 10001LL;
  int64_t expected = 0;
  auto onFrame = [&](Data data) { ASSERT_EQUALS(expected, data->get<PresentationTime>().time); };
  auto data = make_shared<DataRaw>(0);

  data->set(PresentationTime{time});
  auto restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Reset);
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);

  data->set(PresentationTime{time});
  restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Reset, 0);
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);

  data->set(PresentationTime{time});
  restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Reset, time);
  expected = time;
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);
}

unittest("restamp: reset with offsets") {
  int64_t time = 10001;
  int64_t offset = -100;
  int64_t expected = time;
  auto onFrame = [&](Data data) { ASSERT_EQUALS(expected, data->get<PresentationTime>().time); };
  auto data = make_shared<DataRaw>(0);

  data->set(PresentationTime{time});
  auto restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Passthru);
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);

  data->set(PresentationTime{time});
  restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Passthru, 0);
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);

  data->set(PresentationTime{time});
  restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Passthru, offset);
  expected = time + offset;
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);

  data->set(PresentationTime{time});
  restamp = createModule<Transform::Restamp>(&NullHost, Transform::Restamp::Passthru, time);
  expected = time + time;
  ConnectOutput(restamp->getOutput(0), onFrame);
  restamp->processOne(data);
}
