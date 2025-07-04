set(EXE_PLAYER_SRCS
    ${LIB_MODULES_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeliner_player.cpp
)


# Add an executable target
add_executable(player ${EXE_PLAYER_SRCS})

# Set the output directory for the executable
set_target_properties(player PROPERTIES
    OUTPUT_NAME "player.exe"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Include directories for the executable
target_include_directories(player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/lib_modules
)

# Link necessary libraries (adjust as needed)
target_link_libraries(player PRIVATE
    media
    pipeline
    utils
    appcommon
)
