# Core test sources.

add_library(guchho_core_test_objects OBJECT
    unicode_test.cpp
)

target_include_directories(
    guchho_core_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_core_test_objects)
guchho_enable_sanitizers(guchho_core_test_objects)