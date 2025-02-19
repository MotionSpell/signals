# Custom Module {#custom_module_example}
\page custom_module_example Custom Module
\ingroup examples

## Overview
This example demonstrates how to create a custom module within the Signals framework. Custom modules allow you to extend the functionality of the framework by implementing specific processing logic.

## Key Concepts
- **ModuleS**: Base class for single input modules
- **Data Handling**: Managing input and output data
- **Metadata**: Handling metadata associated with data

## Example: Custom Module

### Step 1: Include Necessary Headers
```cpp
#include "lib_modules/core/module.hpp"
#include "lib_modules/core/data.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/log.hpp"
```

### Step 2: Define the Custom Module
```cpp
class CustomModule : public Modules::ModuleS {
public:
    CustomModule(Modules::KHost* host) {
        // Initialize output
        output = addOutput();
    }

    void processOne(Modules::Data data) override {
        // Process input data
        auto inputData = Modules::safe_cast<Modules::DataPcm>(data);
        
        // Perform custom processing
        auto processedData = processData(inputData);
        
        // Set metadata
        processedData->setMetadata(data->getMetadata());
        
        // Send output data
        output->push(processedData);
    }

private:
    std::shared_ptr<Modules::DataPcm> processData(std::shared_ptr<Modules::DataPcm> inputData) {
        // Custom processing logic
        // For example, apply a gain to the audio samples
        for (auto& sample : inputData->getPlane(0)) {
            sample *= 2; // Apply gain
        }
        return inputData;
    }
};
```

### Step 3: Register the Custom Module
```cpp
// Register the custom module with the factory
Modules::IModule* createCustomModule(Modules::KHost* host, void* params) {
    return new CustomModule(host);
}

auto const registered = Modules::Factory::registerModule("CustomModule", &createCustomModule);
```

### Step 4: Use the Custom Module in a Pipeline
```cpp
int main() {
    // Create a pipeline
    Pipelines::Pipeline pipeline;

    // Add modules to the pipeline
    auto source = pipeline.add("FileInput", &inputConfig);
    auto customModule = pipeline.add("CustomModule");
    auto sink = pipeline.add("FileOutput", &outputConfig);

    // Connect the modules
    pipeline.connect(customModule, source);
    pipeline.connect(sink, customModule);

    // Start the pipeline
    pipeline.start();
    pipeline.waitForEndOfStream();

    return 0;
}
```

## Explanation

### Module Definition
- The `CustomModule` class inherits from 

Modules::ModuleS

, which is a base class for single input modules.
- The 

processOne

 method is overridden to implement custom processing logic.

### Data Handling
- Input data is cast to the appropriate type using 

Modules::safe_cast

.
- Custom processing is performed on the input data.
- Metadata is preserved and set on the output data.

### Module Registration
- The custom module is registered with the factory using 

Modules::Factory::registerModule

.
- This allows the module to be dynamically loaded and used in a pipeline.

### Pipeline Integration
- The custom module is added to a pipeline using the `pipeline.add` method.
- Modules are connected using the `pipeline.connect` method.
- The pipeline is started and waits for the end of the stream.

## Conclusion
This custom module example demonstrates how to extend the Signals framework by implementing specific processing logic. By following these steps, you can create and integrate custom modules into your media processing pipelines.
