set(LIB_PIPELINE_INCS
    filter_input.hpp
    filter.hpp
    pipeline.hpp
    graph.hpp
    graph_builder.hpp
    stats.hpp
)

set(LIB_PIPELINE_SRCS
    filter.cpp
    filter.hpp
    graph_builder.cpp
    pipeline.cpp
    pipeline.hpp
)

add_library(pipeline STATIC ${LIB_PIPELINE_SRCS})
add_library(signals::pipeline ALIAS pipeline)

signals_install_library(pipeline STATIC)

# Add include directories
target_include_directories(pipeline PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>               
    $<BUILD_INTERFACE:${SIGNALS_TOP_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/signals>
)

## Link libraries
target_link_libraries(pipeline 
    PRIVATE 
    appcommon 
    modules
    utils
    )

install(FILES ${LIB_PIPELINE_INCS}
    DESTINATION include/signals/lib_pipeline
)
