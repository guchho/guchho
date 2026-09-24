# Resolver (config) test sources.

add_library(guchho_resolver_test_objects OBJECT
    guchho_config_test.cpp
    tsconfig_json_test.cpp
    package_json_test.cpp
    yarnpnp_test.cpp
)

target_include_directories(
    guchho_resolver_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_resolver_test_objects)
guchho_enable_sanitizers(guchho_resolver_test_objects)