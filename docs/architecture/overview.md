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
   - Clock and scheduling

2. **@ref signals "lib_signals"** (Messaging Layer)
   - Signal/slot mechanism
   - Type-safe messaging
   - Generic data transport

3. **@ref modules "lib_modules"** (Module System)
   - Core module interfaces
   - Data/Metadata system
   - Input/Output management
   - Resource allocation
   - Module factory and loader

4. **@ref pipeline "lib_pipeline"** (Pipeline Layer)
   - Orchestration of modules
   - Threading management
   - Graph dynamic capabilities
   - Error and statistics handling

5. **@ref media_api "lib_media"** (Media Layer)
   - Module specialization for media
   - Codec/Format conversion
   - GPAC and FFmpeg wrappers
   - Utilities: XML, HTTP, dates, ...

6. **@ref plugins "plugins"** (Media Layer too)
   - lib_media modules built independently

## Key Concepts

### Modules 
See @ref modules for details.
- **@ref Modules::IModule "IModule"**: Base interface for all modules
- **@ref Modules::Data "Data Flow"**: Reference counted data containers
- **@ref Modules::Connection "Connections"**: Type-safe module interconnections
- **@ref Modules::Metadata "Metadata"**: Extensible attribute system

### Pipeline Management
See @ref pipeline for details.
- @ref Pipeline::Pipeline "Dynamic module loading"

## Workflow Examples

### Basic Pipeline

```cpp
Pipeline p;
auto demux = p.add("LibavDemux", &cfg);
auto decoder = p.add("LibavDecode");
p.connect(decoder, demux);

p.start();
p.waitForEndOfStream();
```
For a complete example, see @ref basic_pipeline_example "Basic Pipeline Example" and unit tests
