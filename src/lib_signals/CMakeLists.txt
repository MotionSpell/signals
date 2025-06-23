set(LIB_SIGNALS_INCS
    executor_threadpool.hpp
    executor.hpp
    signal.hpp
    signals.hpp
)
# Add the library target
add_library(lib_signals INTERFACE)
add_library(signals::lib_signals ALIAS lib_signals)

# Specify include directories for the library
target_include_directories(lib_signals
    INTERFACE
        ${SIGNALS_TOP_SOURCE_DIR}/src/tests
        ${SIGNALS_TOP_SOURCE_DIR}/src

)

# Link dependencies for lib_signals
target_link_libraries(lib_signals
    INTERFACE
        appcommon
        media
        modules
        pipeline
        utils
)

install(TARGETS lib_signals
	EXPORT lib_signals
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION include/signals
)


install(FILES ${LIB_SIGNALS_INCS}
    DESTINATION include/signals/lib_signals
)

#install(DIRECTORY ${PROJECT_SOURCE_DIR}
#        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
#)

# xxxjack install(FILES ${PROJECT_SOURCE_DIR}/CMakeFiles/cwipc_util-config.cmake DESTINATION lib/cmake/cwipc_util)

# xxxjack install(EXPORT cwipc_util DESTINATION lib/cmake/cwipc_util)

if(WIN32)
	# Copy the dependent DLLs that cmake/vcpkg have created
	# install(FILES $<TARGET_RUNTIME_DLLS:cwipc_util> DESTINATION ${CMAKE_INSTALL_BINDIR})
	# Copy the PDB file, if it exists
	# install(FILES $<TARGET_PDB_FILE:cwipc_util> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
endif()




# Add unit tests
if(BUILD_TESTING)
    file(GLOB UNIT_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/unittests/*.cpp
        ${SIGNALS_TOP_SOURCE_DIR}/src/tests/tests.cpp
    )

    add_executable(lib_signals_unittests
        ${UNIT_TEST_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib_appcommon/options.cpp
    )
    target_include_directories(lib_signals_unittests PRIVATE
    ${SIGNALS_TOP_SOURCE_DIR}/src/lib_appcommon
    ${SIGNALS_TOP_SOURCE_DIR}/src/tests
)


    target_link_libraries(lib_signals_unittests
        PRIVATE
            lib_signals
            appcommon
            media
            modules
            pipeline
            utils
    )

    # Add test to CTest
    add_test(
        NAME lib_signals_unittests
        COMMAND lib_signals_unittests
    )
endif()
