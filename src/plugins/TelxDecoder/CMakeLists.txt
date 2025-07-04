# Define the plugin library
add_library(TeletextDecoder SHARED
    telx_decoder.cpp 
    telx.cpp 
 )

# Link dependencies if any
target_link_libraries(TeletextDecoder
    ${CMAKE_THREAD_LIBS_INIT}
    modules
    )

# Include directories
target_include_directories(TeletextDecoder PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

signals_install_plugin(TeletextDecoder ".smd")
