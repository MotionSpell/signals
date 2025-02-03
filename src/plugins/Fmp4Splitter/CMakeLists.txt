# Define the plugin library
add_library(Fmp4Splitter SHARED
    fmp4_splitter.cpp
)

# Link dependencies if any
target_link_libraries(Fmp4Splitter 
    ${CMAKE_THREAD_LIBS_INIT}
    modules
    )

# Include directories
target_include_directories(Fmp4Splitter PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

signals_install_plugin(Fmp4Splitter ".smd")
