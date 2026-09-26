# Bundler test sources.

add_library(guchho_bundler_test_objects OBJECT
    bundler_yarnpnp_test.cpp
    bundler_test.cpp
    bundler_tsconfig_test.cpp
    bundler_dce_test.cpp
    bundler_ts_test.cpp
    bundler_lower_test.cpp
    bundler_splitting_test.cpp
    bundler_importstar_ts_test.cpp
    bundler_importstar_test.cpp
    bundler_importphase_test.cpp
    bundler_packagejson_test.cpp
    bundler_loader_test.cpp
    bundler_glob_test.cpp
    bundler_css_test.cpp
    bundler_default_test.cpp
    bundler_guchhojson_test.cpp
    bundler_html_test.cpp

)

target_include_directories(
    guchho_bundler_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(
    guchho_bundler_test_objects
    PRIVATE
        guchho_bundler
        guchho_linker
)

guchho_set_warnings(guchho_bundler_test_objects)
guchho_enable_sanitizers(guchho_bundler_test_objects)
