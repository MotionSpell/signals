if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)
    # xxxjack this is a hack. We need to find a better way to
    # find the C++ and other system libraries.
    set(CMAKE_MINGW_SYSTEM_LIBRARY_PATH "C:/msys64/ucrt64/bin")
endif()

macro(signals_install_library _component _type)

    set_target_properties(${_component} PROPERTIES 
        OUTPUT_NAME "signals-${_component}" 
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin 
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        )
    if (${_type} STREQUAL "STATIC")
        # No need to find runtime dependencies for static libraries
        install(TARGETS ${_component}
            EXPORT signals-${_component}
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
            FRAMEWORK DESTINATION lib
            PUBLIC_HEADER DESTINATION include/signals
        )
        set_target_properties(${_component} PROPERTIES
            OUTPUT_NAME signals_${_component}
            POSITION_INDEPENDENT_CODE TRUE
        )
    else()
        install(TARGETS ${_component}
            RUNTIME_DEPENDENCY_SET ${_component}_Deps
            EXPORT signals-${_component}
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
            FRAMEWORK DESTINATION lib
        )

        install(RUNTIME_DEPENDENCY_SET ${_component}_Deps
            PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
            POST_EXCLUDE_REGEXES 
                ".*system32/.*\\.dll" 
                "Frameworks"
                "/lib/x86_64-linux-gnu"
                "/lib64"
            DIRECTORIES ${CMAKE_SYSTEM_LIBRARY_PATH} ${CMAKE_MINGW_SYSTEM_LIBRARY_PATH}
        )
    endif()

    export(
        EXPORT signals-${_component}
        NAMESPACE signals::
    )

    install(EXPORT signals-${_component}
        NAMESPACE signals::
        DESTINATION lib/cmake/signals
    )
   
endmacro()

macro(signals_install_plugin _component _suffix)
    if (${_suffix} STREQUAL "")
        set(_suffix ".smd")
    endif()
    set_target_properties(${_component} PROPERTIES 
        OUTPUT_NAME "${_component}" 
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin 
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        SUFFIX "${_suffix}" 
        PREFIX ""
        )

    install(TARGETS ${_component}
        RUNTIME_DEPENDENCY_SET ${_component}_Deps
        EXPORT ${_component}
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        FRAMEWORK DESTINATION lib
    )

    install(RUNTIME_DEPENDENCY_SET ${_component}_Deps
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
        POST_EXCLUDE_REGEXES 
            ".*system32/.*\\.dll" 
            "Frameworks"
            "/lib/x86_64-linux-gnu"
            "/lib64"
        DIRECTORIES ${CMAKE_SYSTEM_LIBRARY_PATH} ${CMAKE_MINGW_SYSTEM_LIBRARY_PATH}
    )

endmacro()

macro(signals_install_app _component)

    set_target_properties(${_component} PROPERTIES 
        OUTPUT_NAME "${_component}" 
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        SUFFIX ".exe" 
        )

    install(TARGETS ${_component}
        RUNTIME_DEPENDENCY_SET ${_component}_Deps
        EXPORT ${_component}
        RUNTIME DESTINATION bin
    )

    install(RUNTIME_DEPENDENCY_SET ${_component}_Deps
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
        POST_EXCLUDE_REGEXES 
            ".*system32/.*\\.dll" 
            "Frameworks"
            "/lib/x86_64-linux-gnu"
            "/lib64"
        DIRECTORIES ${CMAKE_SYSTEM_LIBRARY_PATH} ${CMAKE_MINGW_SYSTEM_LIBRARY_PATH}
    )

endmacro()