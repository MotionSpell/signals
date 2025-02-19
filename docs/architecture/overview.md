# %Signals Framework Overview {#mainpage}
\ingroup architecture

## Introduction
Signals is a modern C++ framework for building modular multimedia applications with a focus on:
- Minimal boilerplate code
- Flexible pipeline architecture
- Plugin-based extensibility
- Real-time media processing

## Core Components 

### Library Structure
1. **@ref core_api "lib_utils"** (Base Layer)
   - Lightweight C++ helpers
   - String manipulation
   - Container utilities
   - Logging system
   - Profiling tools
   - Clock management

2. **lib_signals** (Messaging Layer)
   - Signal/slot mechanism
   - Type-safe messaging
   - C++11 based implementation
   - Generic data transport

3. **@ref modules "lib_modules"** (Module System)
   - Core module interfaces
   - Data/metadata system
   - Input/Output management
   - Resource allocation
   - Clock synchronization

4. **@ref pipeline "lib_pipeline"** (Pipeline Layer)
   - Module orchestration
   - Connection management
   - Dynamic pipeline building
   - Error handling

5. **@ref media_api "lib_media"** (Media Layer)
   - Audio/Video type definitions
   - Format conversion
   - Codec integration
   - Stream management

6. **@ref plugins "plugins"** (Extension Layer)
   - FFmpeg integration
   - GPAC integration
   - Custom module implementations
   - Media transformations

## Key Concepts

### Modules 
See @ref modules for details.
- **@ref Modules::IModule "IModule"**: Base interface for all modules
- **@ref Modules::Data "Data Flow"**: Reference counted data containers
- **@ref Modules::Connection "Connections"**: Type-safe module interconnections
- **@ref Modules::Metadata "Metadata"**: Extensible attribute system

### Pipeline Management
See @ref pipeline for details.
- @ref Pipeline::Manager "Dynamic module loading"
- @ref Pipeline::ResourceManager "Automatic resource management"
- @ref Pipeline::ErrorHandler "Error propagation"
- @ref Pipeline::Config "Runtime reconfiguration"

### Plugin Architecture
See @ref plugins for details.
- @ref Plugin::Loader "Dynamic loading"
- @ref Plugin::Version "Version management"
- @ref Plugin::Resource "Resource isolation"
- @ref Plugin::Platform "Platform independence"

## Workflow Examples

### Basic Pipeline

```cpp
Pipeline p;
auto demux = p.add("LibavDemux", &cfg);
auto decoder = p.add("LibavDecode");
p.connect(decoder, demux);
```
For a complete example, see @ref basic_pipeline_example "Basic Pipeline Example"
