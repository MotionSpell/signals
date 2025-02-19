# Pipelines  {#pipeline}
\page pipeline Pipeline
\ingroup architecture

## Overview
The Pipeline system is the core orchestration layer in Signals, managing module lifecycles, connections, and data flow.

## Key Components

### Pipeline Class
- Module management
- Connection topology
- Thread safety
- Error handling
- Runtime reconfiguration

### Core Features
1. **Module Management**
```cpp
// Add modules
auto demux = pipeline.add("LibavDemux", &cfg);
auto decoder = pipeline.addModule<Decoder>();

// Remove modules
pipeline.removeModule(module);
```

2. **Connections**
```cpp
// Connect modules
pipeline.connect(source, sink);
pipeline.connect(decoder, GetInputPin(muxer, 0));

// Dynamic modifications
pipeline.disconnect(source, 0, sink, 0);
```

3. **Pipeline Control**
```cpp
// Lifecycle
pipeline.start();
pipeline.waitForEndOfStream();
pipeline.exitSync();
```

## Threading Models
- One thread per module
- Synchronized data flow
- Lock-free queues
- Thread-safe operations

## Error Handling
```cpp
pipeline.registerErrorCallback([](const char* error) {
    // Handle error
});
```

## Common Patterns

### Basic Pipeline
```cpp
Pipeline p;
auto source = p.add("FileInput", &inputConfig);
auto processor = p.add("Processor");
auto sink = p.add("FileOutput", &outputConfig);

p.connect(source, processor);
p.connect(processor, sink);
p.start();
```

### Dynamic Modification
- Runtime module addition/removal
- Dynamic connection changes
- Hot-swapping components

### Advanced Features
- Graph visualization
- Performance monitoring
- Resource management
- Plugin support


