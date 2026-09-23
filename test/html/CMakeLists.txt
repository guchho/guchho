# HTML test sources.

add_library(guchho_html_test_objects OBJECT
    html_lexer_test.cpp
    html_parser_test.cpp
    html_printer_test.cpp
)

target_include_directories(
    guchho_html_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_html_test_objects)
guchho_enable_sanitizers(guchho_html_test_objects)