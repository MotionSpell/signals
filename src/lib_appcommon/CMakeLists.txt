set(LIB_APPCOMMON_INCS
    options.hpp
    timebomb.hpp
)
set(LIB_APPCOMMON_SRCS
    options.cpp
    safemain.cpp
    timebomb.cpp
)

add_library(appcommon STATIC ${LIB_APPCOMMON_SRCS})
add_library(signals::appcommon ALIAS appcommon)

target_include_directories(appcommon PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/signals>
    )

target_link_libraries(appcommon PUBLIC utils pipeline)

target_compile_options(appcommon PRIVATE
    -Wall -Wextra -Werror
)

signals_install_library(appcommon STATIC)

install(FILES ${LIB_APPCOMMON_INCS}
    DESTINATION include/signals/lib_appcommon
)
