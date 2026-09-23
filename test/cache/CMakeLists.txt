# Cache test sources.

add_library(guchho_cache_test_objects OBJECT
    cache_ast_test.cpp
    cache_fs_test.cpp
)

target_include_directories(
    guchho_cache_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_cache_test_objects)
guchho_enable_sanitizers(guchho_cache_test_objects)