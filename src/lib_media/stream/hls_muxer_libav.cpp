#include "hls_muxer_libav.hpp"

#include "../common/attributes.hpp"
#include "../common/libav.hpp"
#include "../common/metadata_file.hpp"
#include "../mux/libav_mux.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp" // safe_cast

#include <cassert>

using namespace Modules;

namespace {

bool fileExists(std::string path) {
  auto f = fopen(path.c_str(), "r");
  if(f) {
    fclose(f);
    return true;
  }
  return false;
}

uint64_t fileSize(std::string path) {
  auto file = fopen(path.c_str(), "rt");
  if(!file)
    throw std::runtime_error(format("Can't open segment for reading: '%s'", path));
  fseek(file, 0, SEEK_END);
  auto const fsize = ftell(file);
  fclose(file);
  return fsize;
}

class LibavMuxHLSTS : public ModuleDynI {
  public:
  LibavMuxHLSTS(KHost *host, HlsMuxConfigLibav *cfg)
      : m_host(host)
      , m_utcStartTime(cfg->utcStartTime)
      , segDuration(timescaleToClock(cfg->segDurationInMs, 1000))
      , hlsDir(cfg->baseDir)
      , segBasename(cfg->baseName) {
    {
      MuxConfig muxCfg = {format("%s%s.m3u8", hlsDir, cfg->baseName), "hls", cfg->options};
      delegate = loadModule("LibavMux", m_host, &muxCfg);
    }
    addInput();
    outputSegment = addOutput();
    outputManifest = addOutput();
  }

  void flush() override {
    if(DTS != lastSegDTS) {
      auto meta = make_shared<MetadataFile>(SEGMENT);
      meta->durationIn180k = DTS - lastSegDTS;
      meta->filename = format("%s%s%s.ts", hlsDir, segBasename, segIdx);
      meta->startsWithRAP = true;
      schedule({(int64_t)m_utcStartTime->query() + lastSegDTS, meta, segIdx});
    }

    while(post()) {}
  }

  void process() override {
    ensureDelegateInputs();

    int inputIdx = 0;
    Data data;
    while(!inputs[inputIdx]->tryPop(data)) {
      inputIdx++;
    }
    delegate->getInput(inputIdx)->push(data);

    if(isDeclaration(data))
      return; // contains metadata but not data

    if(data->getMetadata()->type == VIDEO_PKT) {
      auto flags = data->get<CueFlags>();
      DTS = data->get<PresentationTime>().time;

      if(lastSegDTS == INT64_MIN) {
        lastSegDTS = DTS;
        if(!flags.keyframe)
          throw error("First segment doesn't start with a RAP. Aborting.");
      }

      if(DTS >= lastSegDTS + segDuration && flags.keyframe /*always start with a keyframe*/) {
        auto meta = make_shared<MetadataFile>(SEGMENT);
        meta->durationIn180k = DTS - lastSegDTS;
        meta->filename = format("%s%s%s.ts", hlsDir, segBasename, segIdx);
        meta->startsWithRAP = true;

        switch(data->getMetadata()->type) {
        case AUDIO_PKT:
          meta->sampleRate = safe_cast<const MetadataPktAudio>(data->getMetadata())->sampleRate;
          break;
        case VIDEO_PKT: {
          auto const res = safe_cast<const MetadataPktVideo>(data->getMetadata())->resolution;
          meta->resolution = res;
          break;
        }
        default:
          assert(0);
        }

        schedule({(int64_t)m_utcStartTime->query() + lastSegDTS, meta, segIdx});

        /*next segment*/
        lastSegDTS = DTS;
        segIdx++;
      }

      if(DTS - lastSegDTS > 2 * segDuration)
        m_host->log(Error,
              format("DTS(%s) - lastSegDTS(%s) > 2 * segDuration(%s): please check your encoding (GOP) parameters.",
                    DTS, lastSegDTS, segDuration)
                    .c_str());
    }

    postIfPossible();
  }

  IInput *getInput(int i) override {
    delegate->getInput(i);
    return ModuleDynI::getInput(i);
  }

  private:
  void ensureDelegateInputs() {
    auto const inputs = getNumInputs();
    auto const delegateInputs = delegate->getNumInputs();
    for(auto i = delegateInputs; i < inputs; ++i) {
      delegate->getInput(i);
    }
  }

  struct PostableSegment {
    int64_t pts;
    std::shared_ptr<MetadataFile> meta;
    int64_t segIdx;
  };

  void schedule(PostableSegment s) { segmentsToPost.push_back(s); }

  bool post() {
    if(segmentsToPost.empty())
      return false;

    auto s = segmentsToPost.front();
    if(!fileExists(s.meta->filename)) {
      m_host->log(Warning, format("Cannot post filename \"%s\": file does not exist.", s.meta->filename).c_str());
      return false;
    }

    s.meta->filesize = fileSize(s.meta->filename);

    auto data = outputSegment->allocData<DataRaw>(0);
    data->set(PresentationTime{s.pts});
    data->setMetadata(s.meta);
    outputSegment->post(data);
    segmentsToPost.erase(segmentsToPost.begin());

    /*update playlist*/
    {
      auto out = outputManifest->allocData<DataRaw>(0);

      auto metadata = make_shared<MetadataFile>(PLAYLIST);
      metadata->filename = hlsDir + segBasename + ".m3u8";

      out->setMetadata(metadata);
      outputManifest->post(out);
    }

    return true;
  }

  void postIfPossible() {
    if(segmentsToPost.empty())
      return;

    /*segment is complete when next segment exists*/
    auto s = segmentsToPost.front();
    if(!fileExists(format("%s%s%s.ts", hlsDir, segBasename, s.segIdx + 1)))
      return;

    post();
  }

  KHost *const m_host;
  IUtcStartTimeQuery const *const m_utcStartTime;
  std::shared_ptr<IModule> delegate;
  OutputDefault *outputSegment, *outputManifest;
  std::vector<PostableSegment> segmentsToPost;
  int64_t DTS, lastSegDTS = INT64_MIN, segDuration, segIdx = 0;
  std::string hlsDir, segBasename;
};

IModule *createObject(KHost *host, void *va) {
  auto config = (HlsMuxConfigLibav *)va;
  enforce(host, "LibavMuxHLSTS: host can't be NULL");
  enforce(config, "LibavMuxHLSTS: config can't be NULL");
  return createModule<LibavMuxHLSTS>(host, config).release();
}

auto const registered = Factory::registerModule("LibavMuxHLSTS", &createObject);
}
