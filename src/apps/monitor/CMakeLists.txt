set(EXE_MONITOR_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)


# Add an executable target
add_executable(monitor ${EXE_MONITOR_SRCS})

# Set the output directory for the executable
set_target_properties(monitor PROPERTIES
    OUTPUT_NAME "monitor.exe"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Include directories for the executable
target_include_directories(monitor PRIVATE
)

# Link necessary libraries (adjust as needed)
target_link_libraries(monitor PRIVATE
    media
    pipeline
    utils
    appcommon
)
