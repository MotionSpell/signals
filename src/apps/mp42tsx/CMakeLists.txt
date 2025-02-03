set(EXE_MP42TSX_SRCS
    ${LIB_MODULES_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/mp42tsx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/options.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeliner_mp42ts.cpp
    )


# Add an executable target
add_executable(mp42tsx ${EXE_MP42TSX_SRCS})

# Set the output directory for the executable
set_target_properties(mp42tsx PROPERTIES
    OUTPUT_NAME "mp42tsx.exe"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Include directories for the executable
target_include_directories(mp42tsx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link necessary libraries (adjust as needed)
target_link_libraries(mp42tsx PRIVATE
    media
    pipeline
    utils
    appcommon
)
