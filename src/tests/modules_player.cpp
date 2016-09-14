#include "tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_utils/tools.hpp"
#include "lib_media/decode/libav_decode.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/out/null.hpp"
#include "lib_media/render/sdl_audio.hpp"
#include "lib_media/render/sdl_video.hpp"
#include "lib_media/transform/audio_convert.hpp"
#include "lib_utils/tools.hpp"


using namespace Tests;
using namespace Modules;

namespace {

unittest("packet type erasure + multi-output: libav Demux -> libav Decoder (Video Only) -> Render::SDL2") {
	auto demux = uptr(create<Demux::LibavDemux>("data/beepbop.mp4"));
	auto null = uptr(create<Out::Null>());

	size_t videoIndex = std::numeric_limits<size_t>::max();
	for (size_t i = 0; i < demux->getNumOutputs(); ++i) {
		auto metadata = getMetadataFromOutput<MetadataPktLibav>(demux->getOutput(i));
		if (metadata->isVideo()) {
			videoIndex = i;
		} else {
			ConnectOutputToInput(demux->getOutput(i), null);
		}
	}
	ASSERT(videoIndex != std::numeric_limits<size_t>::max());
	auto metadata = getMetadataFromOutput<MetadataPktLibav>(demux->getOutput(videoIndex));
	auto decode = uptr(create<Decode::LibavDecode>(*metadata));
	auto render = uptr(create<Render::SDLVideo>());

	ConnectOutputToInput(demux->getOutput(videoIndex), decode);
	ConnectOutputToInput(decode->getOutput(0), render);

	demux->process(nullptr);
}

unittest("packet type erasure + multi-output: libav Demux -> libav Decoder (Audio Only) -> Render::SDL2") {
	auto demux = uptr(create<Demux::LibavDemux>("data/beepbop.mp4"));
	auto null = uptr(create<Out::Null>());

	size_t audioIndex = std::numeric_limits<size_t>::max();
	for (size_t i = 0; i < demux->getNumOutputs(); ++i) {
		auto metadata = getMetadataFromOutput(demux->getOutput(i));
		if (metadata->getStreamType() == AUDIO_PKT) {
			audioIndex = i;
		} else {
			ConnectOutputToInput(demux->getOutput(i), null);
		}
	}
	ASSERT(audioIndex != std::numeric_limits<size_t>::max());
	auto metadata = getMetadataFromOutput<MetadataPktLibav>(demux->getOutput(audioIndex));
	auto decode = uptr(create<Decode::LibavDecode>(*metadata));
	auto srcFormat = PcmFormat(44100, 1, AudioLayout::Mono, AudioSampleFormat::F32, AudioStruct::Planar);
	auto dstFormat = PcmFormat(44100, 2, AudioLayout::Stereo, AudioSampleFormat::S16, AudioStruct::Interleaved);
	auto converter = uptr(create<Transform::AudioConvert>(srcFormat, dstFormat));
	auto render = uptr(create<Render::SDLAudio>());

	ConnectOutputToInput(demux->getOutput(audioIndex), decode);
	ConnectOutputToInput(decode->getOutput(0), converter);
	ConnectOutputToInput(converter->getOutput(0), render);

	demux->process(nullptr);
}

}
