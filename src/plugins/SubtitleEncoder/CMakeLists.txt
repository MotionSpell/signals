add_library(SubtitleEncoder SHARED
    subtitle_encoder.cpp
)

target_include_directories(SubtitleEncoder PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

target_link_libraries(SubtitleEncoder
    modules
)

signals_install_plugin(SubtitleEncoder ".smd")