# Media Processing {#media_processing_example}
\page media_processing_example Media Processing
\ingroup examples

## Overview
This example demonstrates how to create a media processing pipeline using the Signals framework. The pipeline will demux a media file, decode the streams, apply transformations, and encode the output.

## Key Concepts
- **Pipeline**: Manages the flow of data between modules
- **Modules**: Individual processing units (e.g., demuxer, decoder, encoder)
- **Transformations**: Apply changes to media data (e.g., scaling, format conversion)
- **Connections**: Links between module outputs and inputs

## Example: Media Processing Pipeline

### Step 1: Include Necessary Headers
```cpp
#include "lib_pipeline/pipeline.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/decode/libav_decode.hpp"
#include "lib_media/encode/libav_encode.hpp"
#include "lib_media/transform/audio_convert.hpp"
#include "lib_media/transform/video_scale.hpp"
#include "lib_media/out/file.hpp"
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
    auto audioConverter = pipeline.add("AudioConvert");
    auto videoScaler = pipeline.add("VideoScale");
    auto encoder = pipeline.add("LibavEncode");
    auto fileOutput = pipeline.add("FileOutput");

    // Connect the modules
    for (int i = 0; i < demux->getNumOutputs(); ++i) {
        pipeline.connect(decoder, i, demux, i);
        if (i == 0) { // Assuming first output is audio
            pipeline.connect(audioConverter, 0, decoder, i);
            pipeline.connect(encoder, 0, audioConverter, 0);
        } else { // Assuming second output is video
            pipeline.connect(videoScaler, 0, decoder, i);
            pipeline.connect(encoder, 1, videoScaler, 0);
        }
    }
    pipeline.connect(fileOutput, encoder);

    // Start the pipeline
    pipeline.start();
    pipeline.waitForEndOfStream();

    return 0;
}
```

### Step 3: Run the Pipeline
- Compile and run the application
- The pipeline will demux the media file, decode the streams, apply transformations, encode the output, and save it to a file

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
- Modules like `LibavDemux`, `LibavDecode`, `AudioConvert`, `VideoScale`, 

LibavEncode

, and `FileOutput` are added to the pipeline.

### Connecting Modules
- The `connect` method links module outputs to inputs.
- In this example, the demuxer outputs are connected to the decoder inputs, the decoder outputs are connected to the audio converter and video scaler, and the transformed data is encoded and saved to a file.

### Starting the Pipeline
- The `start` method begins the data flow through the pipeline.
- The `waitForEndOfStream` method waits for the pipeline to finish processing.

## Conclusion
This media processing example demonstrates how to set up a complex media processing workflow using the Signals framework. By following these steps, you can create and connect modules to build custom pipelines for various media processing tasks.
