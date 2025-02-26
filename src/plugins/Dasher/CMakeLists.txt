# Define source files for the MPEG_DASH target
set(EXE_MPEG_DASH_SRCS
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_media/common/xml.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mpeg_dash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mpd.cpp
)
file(GLOB LIB_MODULES_SRCS 
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_modules/utils/*.cpp
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_modules/core/*.cpp
)


list(APPEND EXE_MPEG_DASH_SRCS
    ${LIB_MODULES_SRCS}
)

# Add the target (MPEG_DASH.smd)
add_library(MPEG_DASH SHARED ${EXE_MPEG_DASH_SRCS})

# Include directories for the module
target_include_directories(MPEG_DASH PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SIGNALS_TOP_SOURCE_DIR}/src/
    )

# Link the necessary libraries
target_link_libraries(MPEG_DASH PRIVATE
    media pipeline appcommon utils
)

signals_install_plugin(MPEG_DASH ".smd")

