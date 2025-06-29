add_library(HttpInput SHARED
    http_input.cpp
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_media/common/http_puller.cpp
)

target_include_directories(HttpInput PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

target_link_libraries(HttpInput
    modules
    CURL::libcurl
)

signals_install_plugin(HttpInput ".smd")