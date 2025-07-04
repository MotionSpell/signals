# List of source files for the utils library
set(LIB_UTILS_INCS
    clock.hpp
    fifo.hpp
    fraction.hpp
    json.hpp
    log.hpp
    os.hpp
    profiler.hpp
    scheduler.hpp
    time.hpp
    timer.hpp
    tools.hpp
    sax_xml_parser.hpp
    small_map.hpp
    span.hpp
    socket.hpp
    system_clock.hpp
    xml.hpp
)
set(LIB_UTILS_SRCS
    json.cpp
    log.cpp
    profiler.cpp
    sax_xml_parser.cpp
    scheduler.cpp
    time.cpp
    timer.cpp
    version.cpp
    sysclock.cpp
    syslog.cpp
    xml.cpp
)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    list(APPEND LIB_UTILS_SRCS os_mingw.cpp socket_mingw.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND LIB_UTILS_SRCS os_darwin.cpp socket_gnu.cpp)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    list(APPEND LIB_UTILS_SRCS os_gnu.cpp socket_gnu.cpp)
else()
    message(ERROR "Unknown CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}")
endif()

add_library(utils SHARED)
add_library(signals::utils ALIAS utils)

signals_install_library(utils SHARED)

target_sources(utils PRIVATE 
    ${LIB_UTILS_SRCS}
    ${LIB_UTILS_INCS}
    )

target_sources(utils INTERFACE 
    $<BUILD_INTERFACE:${LIB_UTIL_INCS}>
    $<INSTALL_INTERFACE:$<LIST:TRANSFORM,${LIB_UTIL_INCS},PREPEND,include/signals/lib_utils/>>
    )


# Platform-specific adjustments
if(APPLE)
    # On macOS, we need to include the os_darwin.cpp file
    list(APPEND LIB_UTILS_SRCS os_darwin.cpp)
    target_link_libraries(utils PRIVATE "-ldl" rapidjson)
elseif(UNIX AND NOT APPLE)
    # For Linux/Unix, include os_gnu.cpp and link against necessary libraries
    list(APPEND LIB_UTILS_SRCS os_gnu.cpp)
    target_link_libraries(utils PRIVATE "-ldl" "-lrt" rapidjson)
elseif(WIN32)
    # For Windows, include os_mingw.cpp (if needed for MinGW)
    list(APPEND LIB_UTILS_SRCS os_mingw.cpp)
    target_link_libraries(utils rapidjson ws2_32)
endif()

# Specify include directories for the utils target
target_include_directories(utils PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib_utils>
    $<BUILD_INTERFACE:${SIGNALS_VERSION_HEADER_DIR}>
    $<INSTALL_INTERFACE:include/signals>
)

install(FILES ${LIB_UTILS_INCS}
    DESTINATION include/signals/lib_utils
)
