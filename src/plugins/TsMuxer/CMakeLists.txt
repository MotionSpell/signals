# Define the plugin library
add_library(TsMuxer SHARED
  mpegts_muxer.cpp
  pes.cpp
  crc.cpp
    )

# Link dependencies if any
target_link_libraries(TsMuxer 
    ${CMAKE_THREAD_LIBS_INIT}
    modules
    )

# Include directories
target_include_directories(TsMuxer PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

signals_install_plugin(TsMuxer ".smd")
