# Include source files
set(LIB_MEDIA_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/common/expand_vars.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/http_puller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/http_sender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/iso8601.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/mpeg_dash_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/sax_xml_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/xml.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/demux/dash_demux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/in/file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/in/mpeg_dash_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/in/sound_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/in/video_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/out/file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/out/http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/out/http_sink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/out/null.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/out/print.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stream/apple_hls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stream/ms_hss.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stream/adaptive_streaming_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/audio_gap_filler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/restamp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/rectifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/recorder.cpp
)


# Define the media library
add_library(media STATIC ${LIB_MEDIA_SRCS})
add_library(signals::media ALIAS media)

signals_install_library(media STATIC)

target_include_directories(media PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include/signals>
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DESTINATION include/signals/
    FILES_MATCHING
    PATTERN "*.hpp"
)

target_link_libraries(media 
    PUBLIC 
    modules 
    pipeline 
    appcommon 
    utils
    $<BUILD_INTERFACE:gpac::gpac>
    $<BUILD_INTERFACE:CURL::libcurl>
    )

# Define source files for the AVCC2AnnexBConverter
set(EXE_AVCC2ANNEXBCONVERTER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/avcc2annexb.cpp
)

# Combine all source files into a single list
list(APPEND EXE_AVCC2ANNEXBCONVERTER_SRCS
    ${LIB_MODULES_SRCS}
)

# Add the target (AVCC2AnnexBConverter.smd)
add_library(AVCC2AnnexBConverter SHARED ${EXE_AVCC2ANNEXBCONVERTER_SRCS})

# Include directories for the module
target_include_directories(AVCC2AnnexBConverter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/
)

target_link_libraries(AVCC2AnnexBConverter 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils
    )

signals_install_plugin(AVCC2AnnexBConverter ".smd")

# Define source files for the LibavMuxHLSTS
set(EXE_LIBAVMUXHLSTS_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/stream/hls_muxer_libav.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
)

# Combine all source files into a single list 
list(APPEND EXE_LIBAVMUXHLSTS_SRCS
    ${LIB_MODULES_SRCS}
    )

# Add the target (LibavMuxHLSTS.smd)
add_library(LibavMuxHLSTS SHARED ${EXE_LIBAVMUXHLSTS_SRCS})

# Include directories for the module
target_include_directories(LibavMuxHLSTS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
)

# Link libraries to the target 
target_link_libraries(LibavMuxHLSTS 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(LibavMuxHLSTS ".smd")

# Define source files for the VideoConvert target
set(EXE_VIDEOCONVERTER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/video_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
)

list(APPEND EXE_VIDEOCONVERTER_SRCS
    ${LIB_MODULES_SRCS}
)

# Add the target (VideoConvert.smd)
add_library(VideoConvert SHARED ${EXE_VIDEOCONVERTER_SRCS})

# Include directories for the module
target_include_directories(VideoConvert PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
)

# Link the necessary libraries
target_link_libraries(VideoConvert PRIVATE
    media modules pipeline appcommon utils
    ${FFMPEG_LIBRARIES}
)

signals_install_plugin(VideoConvert ".smd")

# AudioConvert target
set(EXE_AUDIOCONVERTER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/audio_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
)
list(APPEND EXE_AUDIOCONVERTER_SRCS ${LIB_MODULES_SRCS})
add_library(AudioConvert SHARED ${EXE_AUDIOCONVERTER_SRCS})
target_include_directories(AudioConvert 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
    )
target_link_libraries(AudioConvert 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(AudioConvert ".smd")

# JPEGTurboDecode target
set(EXE_JPEGTURBODECODE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/decode/jpegturbo_decode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
)
list(APPEND EXE_JPEGTURBODECODE_SRCS ${LIB_MODULES_SRCS})
add_library(JPEGTurboDecode SHARED ${EXE_JPEGTURBODECODE_SRCS})
target_include_directories(JPEGTurboDecode 
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(JPEGTurboDecode 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    libjpeg-turbo::turbojpeg
    )
    
signals_install_plugin(JPEGTurboDecode ".smd")

# JPEGTurboEncode target
set(EXE_JPEGTURBOENCODE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/encode/jpegturbo_encode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
)
list(APPEND EXE_JPEGTURBOENCODE_SRCS ${LIB_MODULES_SRCS})
add_library(JPEGTurboEncode SHARED ${EXE_JPEGTURBOENCODE_SRCS})
target_include_directories(JPEGTurboEncode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/)
target_link_libraries(JPEGTurboEncode 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    libjpeg-turbo::turbojpeg
    )

signals_install_plugin(JPEGTurboEncode ".smd")

# Encoder target
set(EXE_ENCODER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/encode/libav_encode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
)
list(APPEND EXE_ENCODER_SRCS ${LIB_MODULES_SRCS})
add_library(Encoder SHARED ${EXE_ENCODER_SRCS})
target_include_directories(Encoder 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(Encoder 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(Encoder ".smd")

# Decoder target
set(EXE_DECODER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/decode/decoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
)
list(APPEND EXE_DECODER_SRCS ${LIB_MODULES_SRCS})
add_library(Decoder SHARED ${EXE_DECODER_SRCS})
target_include_directories(Decoder 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
    )
target_link_libraries(Decoder 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(Decoder ".smd")

# LibavDemux target
set(EXE_LIBAVDEMUX_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/demux/libav_demux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/restamp.cpp
)
list(APPEND EXE_LIBAVDEMUX_SRCS ${LIB_MODULES_SRCS})
add_library(LibavDemux SHARED ${EXE_LIBAVDEMUX_SRCS})
target_include_directories(LibavDemux 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
    )
target_link_libraries(LibavDemux 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(LibavDemux ".smd")

# LibavMux target
set(EXE_LIBAVMUX_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/mux/libav_mux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
)
list(APPEND EXE_LIBAVMUX_SRCS ${LIB_MODULES_SRCS})
add_library(LibavMux SHARED ${EXE_LIBAVMUX_SRCS})
target_include_directories(LibavMux 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
    )
target_link_libraries(LibavMux 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(LibavMux ".smd")

# LibavFilter target
set(EXE_LIBAVFILTER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/libavfilter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/picture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/libav.cpp
)
list(APPEND EXE_LIBAVFILTER_SRCS ${LIB_MODULES_SRCS})
add_library(LibavFilter SHARED ${EXE_LIBAVFILTER_SRCS})
target_include_directories(LibavFilter 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
    )
target_link_libraries(LibavFilter 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    ${FFMPEG_LIBRARIES}
)

signals_install_plugin(LibavFilter ".smd")

# GPACMuxMP4 target
set(EXE_GPACMUXMP4_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/mux/gpac_mux_mp4.cpp
)
list(APPEND EXE_GPACMUXMP4_SRCS ${LIB_MODULES_SRCS})
add_library(GPACMuxMP4 SHARED ${EXE_GPACMUXMP4_SRCS})
target_include_directories(GPACMuxMP4 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    ${FFMPEG_INCLUDE_DIRS}
    )
target_link_libraries(GPACMuxMP4 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    gpac::gpac
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(GPACMuxMP4 ".smd")

# GPACMuxMP4MSS target
set(EXE_GPACMUXMP4MSS_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/mux/gpac_mux_mp4_mss.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mux/gpac_mux_mp4.cpp
)
list(APPEND EXE_GPACMUXMP4MSS_SRCS ${LIB_MODULES_SRCS})
add_library(GPACMuxMP4MSS SHARED ${EXE_GPACMUXMP4MSS_SRCS})
target_include_directories(GPACMuxMP4MSS 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(GPACMuxMP4MSS 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    gpac::gpac
    )

signals_install_plugin(GPACMuxMP4MSS ".smd")

# GPACDemuxMP4Simple target
set(EXE_GPACDEMUXMP4SIMPLE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/demux/gpac_demux_mp4_simple.cpp
)
list(APPEND EXE_GPACDEMUXMP4SIMPLE_SRCS ${LIB_MODULES_SRCS})
add_library(GPACDemuxMP4Simple SHARED ${EXE_GPACDEMUXMP4SIMPLE_SRCS})
target_include_directories(GPACDemuxMP4Simple 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(GPACDemuxMP4Simple 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    gpac::gpac
    )

signals_install_plugin(GPACDemuxMP4Simple ".smd")

# GPACDemuxMP4Full target
set(EXE_GPACDEMUXMP4FULL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/demux/gpac_demux_mp4_full.cpp
)
list(APPEND EXE_GPACDEMUXMP4FULL_SRCS ${LIB_MODULES_SRCS})
add_library(GPACDemuxMP4Full SHARED ${EXE_GPACDEMUXMP4FULL_SRCS})
target_include_directories(GPACDemuxMP4Full 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(GPACDemuxMP4Full 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils 
    gpac::gpac
    ${FFMPEG_LIBRARIES}
    )

signals_install_plugin(GPACDemuxMP4Full ".smd")

# FileSystemSink target
set(EXE_FILESYSTEMSINK_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/out/filesystem.cpp
)
list(APPEND EXE_FILESYSTEMSINK_SRCS ${LIB_MODULES_SRCS})
add_library(FileSystemSink SHARED ${EXE_FILESYSTEMSINK_SRCS})
target_include_directories(FileSystemSink 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(FileSystemSink 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils
    )

signals_install_plugin(FileSystemSink ".smd")

# LogoOverlay target
set(EXE_LOGOOVERLAY_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/logo_overlay.cpp
)
list(APPEND EXE_LOGOOVERLAY_SRCS ${LIB_MODULES_SRCS})
add_library(LogoOverlay SHARED ${EXE_LOGOOVERLAY_SRCS})

target_include_directories(LogoOverlay 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(LogoOverlay 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils
    )
    
signals_install_plugin(LogoOverlay ".smd")

# HTTPSink target
set(EXE_HTTPSINK_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/out/http_sink.cpp
)
list(APPEND EXE_FILESYSTEMSINK_SRCS ${LIB_MODULES_SRCS})
add_library(HttpSink SHARED ${EXE_HTTPSINK_SRCS})
target_include_directories(HttpSink 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
    )
target_link_libraries(HttpSink 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils
    )

signals_install_plugin(HttpSink ".smd")

# Rectifier target
set(EXE_RECTIFIER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/transform/rectifier.cpp
)
list(APPEND EXE_RECTIFIER_SRCS ${LIB_MODULES_SRCS})
add_library(Rectifier SHARED ${EXE_RECTIFIER_SRCS})
target_include_directories(Rectifier 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/src/
)
target_link_libraries(Rectifier 
    PRIVATE 
    media 
    modules 
    pipeline 
    appcommon 
    utils
)

signals_install_plugin(Rectifier ".smd")