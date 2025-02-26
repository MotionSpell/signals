file(GLOB LIB_MODULES_SRCS 
    ${CMAKE_SOURCE_DIR}/src/lib_modules/utils/*.cpp
    ${CMAKE_SOURCE_DIR}/src/lib_modules/core/*.cpp
)


file(GLOB LIB_MEDIA_SRCS 
    ${CMAKE_SOURCE_DIR}/src/lib_media/*.cpp
)


# Include subdirectories for each app
add_subdirectory(dashcastx)
add_subdirectory(ts2ip)
add_subdirectory(player)
add_subdirectory(mcastdump)
add_subdirectory(mp42tsx)
add_subdirectory(monitor)