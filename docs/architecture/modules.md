# Modules System

## Overview
The Signals module system provides the core building blocks for creating modular media processing pipelines.

## Module Types

### Base Module Classes
- `Module`: Base class for all modules
- `ModuleS`: Single input specialized module
- `ModuleDynI`: Dynamic input module (e.g. muxers)

### Key Components
1. Inputs/Outputs
   - Input pins for receiving data
   - Output pins for sending data
   - Reference-counted data containers
   - Type-safe connections

2. Data Flow
   - Process() method for data handling
   - Flush() for end-of-stream
   - Thread-safe execution
   - Data attributes and metadata

## Example Implementations

### Basic Module
```cpp
struct PrintModule : public ModuleS {
    PrintModule(KHost* host) {
        output = addOutput();
    }
    
    void processOne(Data data) override {
        // Process data
        output->post(processedData);
    }
};
```

### Dynamic Module
```cpp
struct DynamicModule : public ModuleDynI {
    void process() override {
        // Handle multiple inputs
        for(auto& input : inputs) {
            Data data;
            if(input->tryPop(data)) {
                // Process data
            }
        }
    }
};
```

## Module Development Guidelines
1. Data Handling
   - Use reference counting
   - Handle metadata properly
   - Implement flush() when needed

2. Threading
   - Modules are thread-safe when running
   - No manual thread management needed
   - Use host facilities for scheduling

3. Error Handling
   - Use exceptions for errors
   - Propagate errors through pipeline
   - Clean resource handling

