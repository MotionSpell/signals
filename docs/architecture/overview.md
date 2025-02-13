# Signals Framework Architecture

## Overview
Signals is a modern C++ framework for building modular multimedia applications with a focus on:
- Minimal boilerplate code
- Flexible pipeline architecture
- Plugin-based extensibility
- Real-time media processing

## Core Components 

### Library Structure
1. **lib_utils** (Base Layer)
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

3. **lib_modules** (Module System)
   - Core module interfaces
   - Data/metadata system
   - Input/Output management
   - Resource allocation
   - Clock synchronization

4. **lib_pipeline** (Pipeline Layer)
   - Module orchestration
   - Connection management
   - Dynamic pipeline building
   - Error handling

5. **lib_media** (Media Layer)
   - Audio/Video type definitions
   - Format conversion
   - Codec integration
   - Stream management

6. **plugins** (Extension Layer)
   - FFmpeg integration
   - GPAC integration
   - Custom module implementations
   - Media transformations

## Key Concepts

### Module System
- **IModule**: Base interface for all modules
- **Data Flow**: Reference counted data containers
- **Connections**: Type-safe module interconnections
- **Metadata**: Extensible attribute system

### Pipeline Management
- Dynamic module loading
- Automatic resource management
- Error propagation
- Runtime reconfiguration

### Plugin Architecture
- Dynamic loading
- Version management
- Resource isolation
- Platform independence

## Workflow Examples

### Basic Pipeline
```cpp
Pipeline p;
auto demux = p.add("LibavDemux", &cfg);
auto decoder = p.add("LibavDecode");
p.connect(decoder, demux);