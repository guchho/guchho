# CSS test sources.

add_library(guchho_css_test_objects OBJECT
    css_printer_test.cpp
    css_lexer_test.cpp
    css_helpers_test.cpp
)

target_include_directories(
    guchho_css_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_css_test_objects)
guchho_enable_sanitizers(guchho_css_test_objects)
