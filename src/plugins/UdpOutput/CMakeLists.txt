# Define the plugin library (UdpOutput)
set(UdpOutput_src
    ${CMAKE_CURRENT_SOURCE_DIR}/udp_output.cpp  # Main plugin source
)

# Platform-specific socket source files
if(MSVC)
    # MSVC-specific socket file
    list(APPEND UdpOutput_src ${CMAKE_CURRENT_SOURCE_DIR}/socket_mingw.cpp)
elseif(MINGW)
    # MinGW-specific socket file
    list(APPEND UdpOutput_src ${CMAKE_CURRENT_SOURCE_DIR}/socket_mingw.cpp)
elseif(APPLE)
    # macOS/Darwin-specific socket file
    list(APPEND UdpOutput_src ${CMAKE_CURRENT_SOURCE_DIR}/socket_gnu.cpp)
elseif(UNIX)
    # Linux-specific socket file
    list(APPEND UdpOutput_src ${CMAKE_CURRENT_SOURCE_DIR}/socket_gnu.cpp)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Add the UdpOutput plugin
add_library(UdpOutput SHARED ${UdpOutput_src})

# Link dependencies
target_link_libraries(UdpOutput
    ${CMAKE_THREAD_LIBS_INIT} 
    modules 
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_link_libraries(UdpOutput ws2_32)
endif()

# Include directories
target_include_directories(UdpOutput PUBLIC
    ${SIGNALS_TOP_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}  
)

signals_install_plugin(UdpOutput ".smd")
