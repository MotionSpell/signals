add_library(SocketInput SHARED
    socket_input.cpp
)

# Platform-specific source selection
if(WIN32)
    target_sources(SocketInput PRIVATE
        ${SIGNALS_TOP_SOURCE_DIR}/src/lib_utils/socket_mingw.cpp
    )
    target_link_libraries(SocketInput PRIVATE ws2_32)
elseif(APPLE)
    target_sources(SocketInput PRIVATE
        ${SIGNALS_TOP_SOURCE_DIR}/src/lib_utils/socket_gnu.cpp
    )
else()
    target_sources(SocketInput PRIVATE
        ${SIGNALS_TOP_SOURCE_DIR}/src/lib_utils/socket_gnu.cpp
    )
endif()

target_include_directories(SocketInput PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

target_link_libraries(SocketInput
    modules
)

signals_install_plugin(SocketInput ".smd")