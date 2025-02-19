# Basic Pipeline {#basic_pipeline_example}
\page basic_pipeline_example Basic Pipeline
\ingroup examples

## Overview
This example demonstrates how to create a basic media processing pipeline using the Signals framework. The pipeline will demux a media file, decode the streams, and print the output.

## Key Concepts
- **Pipeline**: Manages the flow of data between modules
- **Modules**: Individual processing units (e.g., demuxer, decoder)
- **Connections**: Links between module outputs and inputs

## Example: Basic Pipeline

### Step 1: Include Necessary Headers
```cpp
#include "lib_pipeline/pipeline.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/decode/libav_decode.hpp"
#include "lib_media/out/print.hpp"
```

### Step 2: Define the Pipeline
```cpp
int main() {
    // Create a pipeline
    Pipelines::Pipeline pipeline;

    // Configure the demuxer
    DemuxConfig demuxConfig;
    demuxConfig.url = "data/sample.mp4";

    // Add modules to the pipeline
    auto demux = pipeline.add("LibavDemux", &demuxConfig);
    auto decoder = pipeline.add("LibavDecode");
    auto printer = pipeline.addModule<Out::Print>();

    // Connect the modules
    for (int i = 0; i < demux->getNumOutputs(); ++i) {
        pipeline.connect(decoder, i, demux, i);
        pipeline.connect(printer, i, decoder, i);
    }

    // Start the pipeline
    pipeline.start();
    pipeline.waitForEndOfStream();

    return 0;
}
```

### Step 3: Run the Pipeline
- Compile and run the application
- The pipeline will demux the media file, decode the streams, and print the output

## Explanation

### Pipeline Creation
- The 

Pipeline

 class manages the flow of data between modules.
- Modules are added to the pipeline using the `add` method.

### Module Configuration
- The 

DemuxConfig

 struct is used to configure the demuxer.
- Modules like `LibavDemux`, `LibavDecode`, and `Out::Print` are added to the pipeline.

### Connecting Modules
- The 

connect

 method links module outputs to inputs.
- In this example, the demuxer outputs are connected to the decoder inputs, and the decoder outputs are connected to the printer inputs.

### Starting the Pipeline
- The `start` method begins the data flow through the pipeline.
- The `waitForEndOfStream` method waits for the pipeline to finish processing.

## Conclusion
This basic pipeline example demonstrates how to set up a simple media processing workflow using the Signals framework. By following these steps, you can create and connect modules to build custom pipelines for various media processing tasks.
