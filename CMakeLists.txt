cmake_minimum_required(VERSION 3.28)

project(Signals VERSION 1.0)

# Add Doxygen support after project declaration
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_INPUT_DIR ${PROJECT_SOURCE_DIR}/src)
    set(DOXYGEN_OUTPUT_DIR ${PROJECT_BINARY_DIR}/docs)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    set(DOXYFILE_IN ${PROJECT_SOURCE_DIR}/docs/doxyfile)
    set(DOXYFILE_OUT ${PROJECT_BINARY_DIR}/doxyfile)

    # Configure the Doxyfile
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    # Add documentation target
    add_custom_target(docs ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)

endif()

# Set toplevel signals directory here, so the CMakefiles work both standalone and under the lldash umbrella
set(SIGNALS_TOP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Add extension directories (for things like Find<package>)
set(CMAKE_MODULE_PATH
	${CMAKE_CURRENT_LIST_DIR}/CMakeFiles
	${CMAKE_MODULE_PATH}
	)

include(SignalsMacros)

    # Set the C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Change some compiler options that are dependent on the target platform.
# xxxjack some are actually dependent on the toolchain...
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)
  
    # Debug-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3)
    endif()

    # Release-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-w -DNDEBUG)
    endif()
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)
    # Debug-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3)
    endif()

    # Release-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-w -DNDEBUG)
    endif()
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)

    # Debug-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3)
    endif()

    # Release-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-w -DNDEBUG)
    endif()
else()
    message(FATAL_ERROR " Unknown CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}")
endif()

# Define paths for source and scripts
set(SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/scripts")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(SIGNALS_VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/version") # xxxjack not very elegant...
file(MAKE_DIRECTORY ${SIGNALS_VERSION_HEADER_DIR})

# Generate signals_version.h using version.sh
set(SIGNALS_VERSION_HEADER "${SIGNALS_VERSION_HEADER_DIR}/signals_version.h")
file(WRITE "${SIGNALS_VERSION_HEADER}" "\"unknown-unknown-unknown\"")
#if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
#    execute_process(
#        COMMAND "${SCRIPTS_DIR}/version.sh"
#        OUTPUT_FILE "${SIGNALS_VERSION_HEADER}"
#    )
#endif()

message(STATUS "Generated signals_version.h available at ${SIGNALS_VERSION_HEADER}")

# Install version header
install(FILES ${SIGNALS_VERSION_HEADER} DESTINATION include/signals)
# Ensure SYSROOT_PATH is valid
if(NOT SYSROOT_PATH)
elseif(NOT EXISTS ${SYSROOT_PATH})
    message(STATUS "No SYSROOT_PATH pre-set, not using sysroot")
else()

    message(STATUS "Using SYSROOT_PATH: ${SYSROOT_PATH}")

    # Set pkg-config path
    set(ENV{PKG_CONFIG_PATH} "${SYSROOT_PATH}/lib/pkgconfig")
endif()

find_package(PkgConfig REQUIRED)
find_package(SDL2 REQUIRED)
find_package(libjpeg-turbo REQUIRED)
find_package(CURL REQUIRED)
find_package(FFmpeg REQUIRED COMPONENTS AVUTIL AVFILTER AVCODEC AVFORMAT AVDEVICE SWSCALE SWRESAMPLE)
if (NOT TARGET gpac::gpac)
    find_package(GPAC QUIET)
    if (NOT GPAC_FOUND)
        # Find it using pkgconfig, for now.
        # This is a huge hack, especially the way we get the windows IMPORTED_LOCATION
        pkg_check_modules(GPAC REQUIRED gpac)
        add_library(gpac::gpac SHARED IMPORTED)
        if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
            # xxxjack this is valid for mingw.
            set_target_properties(gpac::gpac PROPERTIES
                IMPORTED_IMPLIB "${GPAC_LIBDIR}/libgpac.dll.a"
                IMPORTED_LOCATION "${GPAC_LIBDIR}/../bin/libgpac.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${GPAC_INCLUDEDIR}"
            )
        else()
            set_target_properties(gpac::gpac PROPERTIES
                IMPORTED_LOCATION "${GPAC_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}gpac${CMAKE_SHARED_LIBRARY_SUFFIX}"
                INTERFACE_INCLUDE_DIRECTORIES "${GPAC_INCLUDEDIR}"
            )
        endif()
        set(GPAC_FOUND TRUE)
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Create format target - BEFORE add_subdirectory(src)
add_custom_target(format
    COMMAND ${CMAKE_COMMAND} -E echo "Running reformat..."
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/reformat.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Formatting source code..."
)

# Add all the source subdirectories
add_subdirectory(src)

# Make all targets depend on format
add_dependencies(utils format)
add_dependencies(modules format)
add_dependencies(appcommon format)
add_dependencies(pipeline format)
add_dependencies(media format)
add_dependencies(unittests format)

# Add custom targets for easy builds - remove format from DEPENDS
add_custom_target(build_all ALL
    DEPENDS
        utils
        appcommon
        pipeline
        modules
        media
        dashcastx
        player
        mcastdump
        mp42tsx
        monitor
        unittests
    COMMENT "Building all targets."
)

# Install CMake config files for find_package
include(CMakePackageConfigHelpers)

# Configure the package version
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/signals-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Install configuration
#install(EXPORT SignalsTargets
#    FILE SignalsTargets.cmake
#    NAMESPACE Signals::
#    DESTINATION lib/cmake/Signals
#)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles/signals-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/signals-config.cmake"
    INSTALL_DESTINATION lib/cmake/signals
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/signals-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/signals-config-version.cmake"
    DESTINATION lib/cmake/signals
)
