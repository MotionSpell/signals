# Define the plugin library
add_library(TsDemuxer SHARED
    ts_demuxer.cpp
    )

# Link dependencies if any
target_link_libraries(TsDemuxer 
    ${CMAKE_THREAD_LIBS_INIT}
    modules
    )

# Include directories
target_include_directories(TsDemuxer PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

signals_install_plugin(TsDemuxer ".smd")
