# Linker test sources.

add_library(guchho_linker_test_objects OBJECT
linker_chunk_test.cpp
    linker_css_test.cpp
    linker_mangle_test.cpp
    linker_scan_test.cpp
)

target_include_directories(
    guchho_linker_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_linker_test_objects)
guchho_enable_sanitizers(guchho_linker_test_objects)