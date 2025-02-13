# Core API Documentation

## Overview
The Core API provides fundamental building blocks for the Signals framework:
- Data management
- Module system
- Threading model
- Resource handling

## Key Components

### Data System
```cpp
namespace Modules {
    class DataBase {
        // Base class for all data containers
        // Reference counted data management
    };
    
    class Data : public DataBase {
        // Concrete data implementation
        // Type-safe data container
    };
}
```

### Module System
```cpp
class IModule {
    // Base interface for all modules
    virtual void process() = 0;
    virtual IOutput* getOutput(int i) = 0;
    virtual IInput* getInput(int i) = 0;
};

class ModuleS : public IModule {
    // Single input specialized module
    void processOne(Data data);
};
```

### Threading Model
- One thread per module
- Lock-free queues
- Thread-safe operations
- Event-driven processing

### Resource Management
- RAII patterns
- Smart pointers
- Reference counting
- Safe type casting

## Best Practices

### Data Handling
```cpp
// Safe data casting
auto pcmData = safe_cast<DataPcm>(data);

// Reference counting
auto data = make_shared<DataPcm>();
```

### Module Development
```cpp
// Basic module implementation
struct CustomModule : public ModuleS {
    void processOne(Data data) override {
        // Process data
        output->push(processedData);
    }
};
```

### Error Handling
```cpp
try {
    // Module operations
} catch (const std::runtime_error& e) {
    // Error propagation
}
```

## Performance Considerations
1. Zero-copy data passing
2. Lock-free queue implementation
3. Memory pool allocation
4. Thread synchronization patterns