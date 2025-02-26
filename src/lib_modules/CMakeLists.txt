# Define the library target
add_library(modules STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/allocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/helper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/loader.cpp
)
add_library(signals::modules ALIAS modules)

signals_install_library(modules STATIC)

# Add include directories
target_include_directories(modules PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${SIGNALS_TOP_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${SIGNALS_TOP_SOURCE_DIR}/src/lib_modules/utils>
    $<BUILD_INTERFACE:${SIGNALS_TOP_SOURCE_DIR}/src/lib_modules/core>
    $<INSTALL_INTERFACE:include/signals>
)

# xxxjack should use POSITION_INDEPENDENT_CODE? See https://cmake.org/cmake/help/latest/prop_tgt/POSITION_INDEPENDENT_CODE.html#prop_tgt:POSITION_INDEPENDENT_CODE

# Add compile flags
target_compile_options(modules PUBLIC
    -Wall -Wextra -Werror
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    target_link_options(modules PUBLIC
    -pthread -Wl,--no-undefined
    )
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # xxxjack -undefined error is deprecated
    #target_link_options(modules PUBLIC
    #-undefined error
    #)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_options(modules PUBLIC
    -pthread -Wl,--no-undefined
    )
else()
    message(ERROR "Unknown CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}")
endif()
# Add linker flags

# Link dependencies if any
target_link_libraries(modules 
    PUBLIC 
    "$<LINK_LIBRARY:WHOLE_ARCHIVE:utils>"
    utils
    )

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DESTINATION include/signals/
    FILES_MATCHING
    PATTERN "*.hpp"
)
