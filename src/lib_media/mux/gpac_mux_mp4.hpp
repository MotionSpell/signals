#pragma once

#include "lib_media/common/metadata.hpp"
#include "lib_modules/utils/helper_dyn.hpp"

#include "mux_mp4_config.hpp"

typedef struct __tag_isom GF_ISOFile;
namespace gpacpp {
class IsoSample;
}
namespace Modules { namespace Mux {

class GPACMuxMP4 : public ModuleS {
  public:
  GPACMuxMP4(KHost *host, Mp4MuxConfig const &config);
  virtual ~GPACMuxMP4();
  void processOne(Data data) override;
  void flush() override;

  protected:
  virtual void declareStreamVideo(const MetadataPktVideo *metadata);
  virtual void declareStreamAudio(const MetadataPktAudio *metadata);
  virtual void declareStreamSubtitle(const MetadataPktSubtitle *metadata);
  virtual void startSegmentPostAction() {}
  uint32_t trackId = 0;
  std::string codec4CC;
  std::string lang;
  GF_ISOFile *isoInit, *isoCur;
  KHost *const m_host;

  private:
  IUtcStartTimeQuery const *const m_utcStartTime;

  void updateFormat(Data data);
  void declareStream(const IMetadata *metadata);
  void handleInitialTimeOffset();
  void sendSegmentToOutput(bool EOS);
  void fillSample(Data data, gpacpp::IsoSample *sample, bool isRap);
  void startChunk(gpacpp::IsoSample *const sample);
  void addData(gpacpp::IsoSample const *const sample, int64_t lastDataDurationInTs);
  void closeChunk(bool nextSampleIsRAP);
  void processSample(Data data, int64_t lastDataDurationInTs);
  void updateSegmentName();

  uint32_t MP4_4CC;
  CompatibilityFlag compatFlags;
  Data lastData = nullptr; // used with ExactInputDur flag
  int64_t m_DTS = 0, initDTSIn180k = 0, firstDataAbsTimeInMs = 0;
  uint64_t defaultSampleIncInTs = 0;
  uint32_t timeScale = 0;
  bool isAnnexB = false;

  // fragments
  void setupFragments();
  void startFragment(uint64_t DTS, uint64_t PTS);
  void closeFragment();
  const FragmentPolicy fragmentPolicy;
  int64_t curFragmentDurInTs = 0;
  // SmoothStreaming compat only, for fragments:
  uint64_t nextFragmentNum /*used with IndependentSegment*/ = 1;

  // segments
  void startSegment();
  void closeSegment(bool isLastSeg);
  const SegmentPolicy segmentPolicy;
  Fraction segmentDuration{};
  uint64_t curSegmentDurInTs = 0, curSegmentDeltaInTs = 0, segmentNum = 0, lastSegmentSize = 0;
  bool segmentStartsWithRAP = true;
  std::string segmentName;
  std::string initName;
  std::string baseName;

  OutputDefault *output;
  Resolution resolution;
  int sampleRate;
};

}}
