#include "lib_media/common/pcm.hpp"
#include "lib_media/decode/decoder.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/out/null.hpp"
#include "lib_media/transform/audio_convert.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/tools.hpp"
#include "tests/tests.hpp"

using namespace Tests;
using namespace Modules;

namespace {

secondclasstest("packet type erasure + multi-output: libav Demux -> libav Decoder (Video Only) -> Render::SDL2") {
  DemuxConfig cfg;
  cfg.url = "data/beepbop.mp4";
  auto demux = loadModule("LibavDemux", &NullHost, &cfg);
  auto null = createModule<Out::Null>(&NullHost);

  int videoIndex = -1;
  for(int i = 0; i < demux->getNumOutputs(); ++i) {
    if(demux->getOutput(i)->getMetadata()->isVideo()) {
      videoIndex = i;
    } else {
      ConnectOutputToInput(demux->getOutput(i), null->getInput(0));
    }
  }
  ASSERT(videoIndex != -1);
  auto metadata = demux->getOutput(videoIndex)->getMetadata();
  DecoderConfig decCfg;
  decCfg.type = metadata->type;
  auto decode = loadModule("Decoder", &NullHost, &decCfg);
  auto render = Modules::loadModule("SDLVideo", &NullHost, nullptr);

  ConnectOutputToInput(demux->getOutput(videoIndex), decode->getInput(0));
  ConnectOutputToInput(decode->getOutput(0), render->getInput(0));

  demux->process();
}

secondclasstest("packet type erasure + multi-output: libav Demux -> libav Decoder (Audio Only) -> Render::SDL2") {
  DemuxConfig cfg;
  cfg.url = "data/beepbop.mp4";
  auto demux = loadModule("LibavDemux", &NullHost, &cfg);
  auto null = createModule<Out::Null>(&NullHost);

  int audioIndex = -1;
  for(int i = 0; i < demux->getNumOutputs(); ++i) {
    if(demux->getOutput(i)->getMetadata()->type == AUDIO_PKT) {
      audioIndex = i;
    } else {
      ConnectOutputToInput(demux->getOutput(i), null->getInput(0));
    }
  }
  ASSERT(audioIndex != -1);
  auto metadata = demux->getOutput(audioIndex)->getMetadata();
  DecoderConfig decCfg;
  decCfg.type = metadata->type;
  auto decode = loadModule("Decoder", &NullHost, &decCfg);
  auto srcFormat = PcmFormat(44100, 1, AudioLayout::Mono, AudioSampleFormat::F32, AudioStruct::Planar);
  auto dstFormat = PcmFormat(44100, 2, AudioLayout::Stereo, AudioSampleFormat::S16, AudioStruct::Interleaved);
  auto converterCfg = AudioConvertConfig{srcFormat, dstFormat, -1};
  auto converter = Modules::loadModule("AudioConvert", &NullHost, &converterCfg);
  auto render = Modules::loadModule("SDLAudio", &NullHost, nullptr);

  ConnectOutputToInput(demux->getOutput(audioIndex), decode->getInput(0));
  ConnectOutputToInput(decode->getOutput(0), converter->getInput(0));
  ConnectOutputToInput(converter->getOutput(0), render->getInput(0));

  demux->process();
}

}
