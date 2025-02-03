set(EXE_MCASTDUMP_SRCS
    ${LIB_MODULES_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)


# Add an executable target
add_executable(mcastdump ${EXE_MCASTDUMP_SRCS})

# Set the output directory for the executable
set_target_properties(mcastdump PROPERTIES
    OUTPUT_NAME "mcastdump.exe"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Include directories for the executable
target_include_directories(mcastdump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib_modules
)

# Link necessary libraries (adjust as needed)
target_link_libraries(mcastdump PRIVATE
    media
    pipeline
    utils
    appcommon
)
