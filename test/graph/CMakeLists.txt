# Graph test sources.

add_library(guchho_graph_test_objects OBJECT
    graph_test.cpp
)

target_include_directories(
    guchho_graph_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_graph_test_objects)
guchho_enable_sanitizers(guchho_graph_test_objects)