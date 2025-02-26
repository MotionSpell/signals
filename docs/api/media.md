# Media API Documentation {#media_api}
\page media_api Media API Documentation
\ingroup api

## Overview
The Media API provides essential components for audio and video processing within the Signals framework. It includes modules for demuxing, decoding, encoding, muxing, and various transformations.

## Key Components

### Common Types
- **PcmFormat**: Defines audio format
- **PictureFormat**: Defines video format
- **Metadata**: Stores media metadata
- **Attributes**: Extensible attribute system

### Demuxing
- **LibavDemux**: FFmpeg-based demuxer
- **GpacDemux**: GPAC-based demuxer
```cpp
// Example: Using LibavDemux
DemuxConfig cfg;
cfg.url = "data/sample.mp4";
auto demux = loadModule("LibavDemux", &NullHost, &cfg);
```

### Decoding
- **LibavDecode**: FFmpeg-based decoder
```cpp
// Example: Using LibavDecode
auto decode = loadModule("LibavDecode", &NullHost, nullptr);
```

### Encoding
- **LibavEncode**: FFmpeg-based encoder
```cpp
// Example: Using LibavEncode
EncoderConfig cfg;
cfg.codecName = "h264";
auto encode = loadModule("LibavEncode", &NullHost, &cfg);
```

### Muxing
- **LibavMux**: FFmpeg-based muxer
- **GpacMux**: GPAC-based muxer
```cpp
// Example: Using LibavMux
MuxConfig cfg;
cfg.format = "mp4";
cfg.path = "output.mp4";
auto mux = loadModule("LibavMux", &NullHost, &cfg);
```

### Transformations
- **AudioConvert**: Audio format conversion
- **VideoScale**: Video scaling
- **LogoOverlay**: Video overlay
```cpp
// Example: Using AudioConvert
AudioConvertConfig cfg;
cfg.inputFormat = PcmFormat(44100, 2, AudioLayout::Stereo, AudioSampleFormat::S16, AudioStruct::Interleaved);
cfg.outputFormat = PcmFormat(48000, 2, AudioLayout::Stereo, AudioSampleFormat::S16, AudioStruct::Planar);
auto converter = loadModule("AudioConvert", &NullHost, &cfg);
```

## Best Practices

### Data Handling
- Use reference-counted data containers
- Ensure type-safe casting
- Handle metadata properly

### Performance
- Minimize memory copies
- Use efficient algorithms
- Profile critical paths

### Error Handling
- Clear error messages
- Proper resource cleanup
- Exception safety

## Example Workflow


```cpp
Pipeline p;
auto demux = p.add("LibavDemux", &cfg);
auto decode = p.add("LibavDecode");
auto encode = p.add("LibavEncode", &encodeCfg);
auto mux = p.add("LibavMux", &muxCfg);

p.connect(demux, decode);
p.connect(decode, encode);
p.connect(encode, mux);

p.start();
```
