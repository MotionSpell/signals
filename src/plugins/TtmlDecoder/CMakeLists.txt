add_library(TTMLDecoder SHARED
    ttml_decoder.cpp
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_utils/sax_xml_parser.cpp
)

target_include_directories(TTMLDecoder PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
    
)

target_link_libraries(TTMLDecoder
    modules
)

signals_install_plugin(TTMLDecoder ".smd")