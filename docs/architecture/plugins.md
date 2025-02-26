
# Plugins {#plugins}
\page plugins Plugins
\ingroup architecture

## Overview
The Signals plugin system provides a flexible way to extend functionality through dynamically loaded modules.


## Plugin Architecture

### Directory Structure
```
src/plugins/
├── decode/            # Decoder plugins
│   └── libav/         # FFmpeg decoder implementation
├── demux/             # Demultiplexer plugins
│   ├── gpac/          # GPAC MP4 demuxer
│   └── libav/         # FFmpeg demuxer
├── encode/            # Encoder plugins
│   └── libav/         # FFmpeg encoder
├── input/             # Input plugins
│   ├── libav/         # FFmpeg input
│   └── file/          # File system input
├── mux/               # Multiplexer plugins
│   ├── gpac/          # GPAC MP4 muxer
│   └── libav/         # FFmpeg muxer
├── transform/         # Transform plugins
│   ├── audio/         # Audio processing
│   │   ├── convert/   # Format conversion
│   │   └── resample/  # Audio resampling
│   └── video/         # Video processing
│       ├── convert/   # Pixel format conversion
│       ├── scale/     # Video scaling
│       └── overlay/   # Logo overlay
└── output/            # Output plugins
    ├── display/       # Display output
    ├── file/          # File output
    └── sink/          # Data sink
```

### Plugin Base Classes
```cpp
// Base plugin interface
class IModule {
    virtual void process() = 0;
    virtual int getNumInputs() const = 0;
    virtual int getNumOutputs() const = 0;
};

// Single input module
class ModuleS : public IModule {
    virtual void processOne(Data data) = 0;
};

// Dynamic input module
class ModuleDynI : public IModule {
    virtual void onNewInput(int inputIndex) = 0;
};
```

### Plugin Categories

#### 1. Media Container Plugins
- **Demuxers**: Extract elementary streams
- **Muxers**: Combine streams into containers
```cpp
// Demuxer configuration
struct DemuxConfig {
    std::string url;
    std::string format;
};

// Usage example
auto demux = loadModule("LibavDemux", &host, &cfg);
```

#### 2. Codec Plugins
- **Decoders**: Raw media extraction
- **Encoders**: Media compression
```cpp
// Decoder configuration
struct DecodeConfig {
    std::string codecName;
    int threads;
};

// Usage example
auto decoder = loadModule("LibavDecode", &host, &cfg);
```

#### 3. Transform Plugins
- **Audio**: Format conversion, resampling
- **Video**: Scaling, format conversion
```cpp
// Audio converter configuration
struct AudioConvertConfig {
    PcmFormat inputFormat;
    PcmFormat outputFormat;
};

// Usage example
auto converter = loadModule("AudioConvert", &host, &cfg);
```

### Plugin Development Guide

#### 1. Plugin Registration
```cpp
// Required macro for plugin registration
REGISTER_MODULE("PluginName", PluginClass)
```

#### 2. Configuration Structure
```cpp
struct PluginConfig {
    // Plugin specific parameters
    std::string param1;
    int param2;
};
```

#### 3. Resource Management
```cpp
class CustomPlugin : public ModuleS {
    ~CustomPlugin() {
        // Cleanup resources
    }
    
    void flush() override {
        // Handle end of stream
    }
};
```

#### 4. Error Handling
```cpp
void processOne(Data data) override {
    try {
        // Process data
    } catch (const std::exception& e) {
        throw std::runtime_error("Plugin error: " + 
                               std::string(e.what()));
    }
}
