# API test sources.

add_library(guchho_api_test_objects OBJECT
    api_serve_test.cpp
    api_test.cpp
    api_watcher_test.cpp
)

target_include_directories(
    guchho_api_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_api_test_objects)
guchho_enable_sanitizers(guchho_api_test_objects)
