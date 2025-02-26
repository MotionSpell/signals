# Define the SDLVideo plugin
set(SDLVideo_src
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_video.cpp  # Main source for SDLVideo
)

# Link SDL2 package for SDLVideo
find_package(SDL2 REQUIRED)

# Define the SDLVideo target
add_library(SDLVideo SHARED ${SDLVideo_src})

# Link dependencies
target_link_libraries(SDLVideo
    ${CMAKE_THREAD_LIBS_INIT}  
    modules
    SDL2::SDL2  
)

# Include directories
target_include_directories(SDLVideo PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

signals_install_plugin(SDLVideo ".smd")

# Define the SDLAudio plugin
set(SDLAudio_src
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_audio.cpp  
)

# Define the SDLAudio target
add_library(SDLAudio SHARED ${SDLAudio_src})

# Link dependencies
target_link_libraries(SDLAudio
    ${CMAKE_THREAD_LIBS_INIT} 
    modules 
    SDL2::SDL2  # Link SDL2
)

signals_install_plugin(SDLAudio ".smd")
