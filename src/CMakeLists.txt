add_subdirectory(lib_utils)
add_subdirectory(lib_appcommon)
add_subdirectory(lib_pipeline)
add_subdirectory(lib_modules)
add_subdirectory(lib_media)
add_subdirectory(lib_signals)

add_subdirectory(plugins)

add_subdirectory(apps)
add_subdirectory(tests)

# Optionally, group targets into folders in IDEs (like Visual Studio)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Function to group a target if it exists
function(group_target target_name folder_name)
    if (TARGET ${target_name})
        set_target_properties(${target_name} PROPERTIES FOLDER "${folder_name}")
    endif()
endfunction()

# Group libraries
group_target(utils "Libraries")
group_target(appcommon "Libraries")
group_target(pipeline "Libraries")
group_target(modules "Libraries")
group_target(media "Libraries")
group_target(signals "Libraries")

# Automatically group all plugins into the "Plugins" folder
get_property(PLUGIN_TARGETS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/plugins PROPERTY BUILDSYSTEM_TARGETS)
foreach(plugin ${PLUGIN_TARGETS})
    group_target(${plugin} "Plugins")
endforeach()


# Group tests
group_target(unit_tests "Tests")


## Export all library targets
#install(EXPORT SignalsTargets
#    FILE SignalsTargets.cmake
#    NAMESPACE Signals::
#    DESTINATION lib/cmake/Signals
#)
