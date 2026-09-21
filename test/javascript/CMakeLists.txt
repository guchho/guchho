# JavaScript test sources.

add_library(guchho_js_test_objects OBJECT
    js_lexer_test.cpp
    js_parser_test.cpp
    js_printer_test.cpp
    js_parser_lower_test.cpp
)

target_include_directories(
    guchho_js_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_js_test_objects)
guchho_enable_sanitizers(guchho_js_test_objects)