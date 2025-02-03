# Define the path to the sources
set(DASHCASTX_SRCS
    main.cpp
    pipeliner_dashcastx.cpp
    ../../lib_appcommon/safemain.cpp
)


# Combine all source files into a single list
list(APPEND DASHCASTX_SRCS 
    ${LIB_MEDIA_SRCS}
    ${LIB_MODULES_SRCS}
)

# Define the executable
add_executable(dashcastx ${DASHCASTX_SRCS})

# Include directories for libraries (e.g., appcommon, media, etc.)
target_include_directories(dashcastx PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib_appcommon
    ${CMAKE_SOURCE_DIR}/src/lib_modules/
)

# Link libraries to the executable
target_link_libraries(dashcastx PRIVATE 
    media
    pipeline
    appcommon
    utils
)

# Optionally, set the output name or location (if needed)
set_target_properties(dashcastx PROPERTIES 
    OUTPUT_NAME "dashcastx.exe"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
