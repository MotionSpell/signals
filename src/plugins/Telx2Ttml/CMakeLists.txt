# Define the plugin library
add_library(TeletextToTTML SHARED
    telx2ttml.cpp
    telx.cpp
    )

# Link dependencies if any
target_link_libraries(TeletextToTTML 
    ${CMAKE_THREAD_LIBS_INIT} 
    modules
    ${FFMPEG_LIBRARIES}
    )

# Include directories
target_include_directories(TeletextToTTML 
    PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIRS}
)

signals_install_plugin(TeletextToTTML ".smd")
