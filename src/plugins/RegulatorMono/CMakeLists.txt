add_library(RegulatorMono SHARED
    regulator_mono.cpp
)

target_include_directories(RegulatorMono PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src
)

target_link_libraries(RegulatorMono
    modules
)

signals_install_plugin(RegulatorMono ".smd")