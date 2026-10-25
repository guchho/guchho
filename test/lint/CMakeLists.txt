# Dedicated lint engine test sources.

add_library(guchho_lint_test_objects OBJECT
    lint_rules_test.cpp
    lint_config_test.cpp
    lint_discovery_test.cpp
    lint_engine_test.cpp
    lint_output_test.cpp
    lint_project_test.cpp
)

target_include_directories(
    guchho_lint_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(
    guchho_lint_test_objects
    PRIVATE
        guchho_lint
        guchho_common
)

guchho_set_warnings(guchho_lint_test_objects)
guchho_enable_sanitizers(guchho_lint_test_objects)
