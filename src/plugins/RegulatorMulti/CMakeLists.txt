add_library(RegulatorMulti SHARED
    regulator_multi.cpp
)

target_include_directories(RegulatorMulti PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

target_link_libraries(RegulatorMulti
    modules
)

signals_install_plugin(RegulatorMulti ".smd")