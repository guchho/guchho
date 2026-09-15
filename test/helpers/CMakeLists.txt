# Helpers test sources.

add_library(guchho_helpers_test_objects OBJECT
    utf8_test.cpp
    base64_test.cpp
    xxhash_test.cpp
    strings_test.cpp
    url_test.cpp
    joiner_test.cpp
    typo_test.cpp
    path_test.cpp
    timer_test.cpp
    quote_test.cpp
    comment_test.cpp
    dataurl_test.cpp
)

target_include_directories(
    guchho_helpers_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_helpers_test_objects)
guchho_enable_sanitizers(guchho_helpers_test_objects)