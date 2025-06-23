add_library(TtmlDecoder SHARED
    ttml_decoder.cpp
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_utils/sax_xml_parser.cpp
)

target_include_directories(TtmlDecoder PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
    
)

target_link_libraries(TtmlDecoder
    modules
)

signals_install_plugin(TtmlDecoder ".smd")