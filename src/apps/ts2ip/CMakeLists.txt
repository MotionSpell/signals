set(EXE_MCASTDUMP_SRCS
    ${LIB_MODULES_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)


# Add an executable target
add_executable(ts2ip ${EXE_MCASTDUMP_SRCS})

# Set the output directory for the executable
set_target_properties(ts2ip PROPERTIES
    OUTPUT_NAME "ts2ip.exe"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Include directories for the executable
target_include_directories(ts2ip PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib_modules
)

# Link necessary libraries (adjust as needed)
target_link_libraries(ts2ip PRIVATE
    media
    pipeline
    utils
    appcommon
)
