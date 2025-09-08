#pragma once

#include "../common/metadata_file.hpp"
#include "lib_media/common/resolution.hpp"
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log.hpp"
#include "lib_utils/tools.hpp" // safe_cast

#include <memory>
#include <string>
#include <thread>

namespace Modules { namespace Stream {

struct Quality {
  virtual ~Quality() {}

  std::shared_ptr<const MetadataFile> getMeta() const {
    return lastData ? safe_cast<const MetadataFile>(lastData->getMetadata()) : nullptr;
  };

  Data lastData;
  uint64_t avg_bitrate_in_bps = 0;
  std::string prefix; // typically a subdir, ending with a folder separator '/'
};

class AdaptiveStreamingCommon : public ModuleDynI {
  public:
  /*created each quality private data*/
  virtual std::unique_ptr<Quality> createQuality() const = 0;
  /*called each time segments are ready*/
  virtual void generateManifest() = 0;
  /*last manifest to be written: usually the VoD one*/
  virtual void finalizeManifest() = 0;

  enum Type {
    Static,
    Live,
    LiveNonBlocking,
  };
  enum AdaptiveStreamingCommonFlags {
    None = 0,
    SegmentsNotOwned = 1 << 0, // don't touch files
    PresignalNextSegment = 1 << 1, // speculative, allows prefetching on player side
    ForceRealDurations = 1 << 2
  };

  AdaptiveStreamingCommon(KHost *host,
        Type type,
        uint64_t segDurationInMs,
        const std::string &manifestDir,
        AdaptiveStreamingCommonFlags flags);
  virtual ~AdaptiveStreamingCommon() {}

  void process() override final;
  void flush() override final;

  static std::string getCommonPrefixAudio(size_t index) { return format("a_%s", index); }
  static std::string getCommonPrefixVideo(size_t index, Resolution res) {
    return format("v_%s_%sx%s", index, res.width, res.height);
  }
  static std::string getCommonPrefixSubtitle(size_t index) { return format("s_%s", index); }

  protected:
  KHost *const m_host;
  bool moveFile(const std::string &src, const std::string &dst) const;
  void processInitSegment(Quality const *const quality, size_t index);
  std::string getInitName(Quality const *const quality, size_t index) const;
  std::string getSegmentName(Quality const *const quality, size_t index, const std::string &segmentNumSymbol) const;
  uint64_t getCurSegNum() const;
  std::shared_ptr<DataBase> getPresignalledData(uint64_t size, Data &data, bool EOS);
  void endOfStream();

  const Type type;
  uint64_t startTimeInMs = -1, totalDurationInMs = 0;
  const uint64_t segDurationInMs;
  const std::string manifestDir;
  const AdaptiveStreamingCommonFlags flags;
  std::vector<std::unique_ptr<Quality>> qualities;
  OutputDefault *outputSegments, *outputManifest;

  private:
  void ensurePrefix(size_t index);
  std::string getPrefix(Quality const *const quality, size_t index) const;
  void threadProc();
  std::thread workingThread;
};

}}
