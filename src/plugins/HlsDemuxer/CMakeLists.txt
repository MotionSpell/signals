# Define source files for the HlsDemuxer target
set(EXE_HlsDemuxer_SRCS
    hls_demux.cpp
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_media/common/http_puller.cpp
)


list(APPEND EXE_HlsDemuxer_SRCS
    ${LIB_MODULES_SRCS}
)

# Add the target (HlsDemuxer.smd)
add_library(HlsDemuxer SHARED ${EXE_HlsDemuxer_SRCS})

# Include directories for the module
target_include_directories(HlsDemuxer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SIGNALS_TOP_SOURCE_DIR}/src/
    )

# Link the necessary libraries
target_link_libraries(HlsDemuxer PRIVATE
    media  pipeline appcommon utils CURL::libcurl
)

signals_install_plugin(HlsDemuxer ".smd")
