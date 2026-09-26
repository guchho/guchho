# Custom test framework + CTest discovery.

include(GuchhoTesting)

# The library targets expose cxx_std_20 through guchho_common (PUBLIC), but the
# test object libraries don't link it. Match the standard explicitly so test
# objects compile with the same C++ version as the rest of the project.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Test subdirectories

add_subdirectory(javascript)
add_subdirectory(helpers)
add_subdirectory(core)
add_subdirectory(css)
add_subdirectory(html)
add_subdirectory(cache)
add_subdirectory(resolver)
add_subdirectory(graph)
add_subdirectory(linker)
add_subdirectory(bundler)
add_subdirectory(api)



# JavaScript test executable

add_executable(guchho_js_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_js_test_objects>
)

target_link_libraries(
    guchho_js_tests
    PRIVATE
        guchho_js
        guchho_common
)

target_include_directories(
    guchho_js_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_js_tests)
guchho_enable_sanitizers(guchho_js_tests)

guchho_discover_tests(guchho_js_tests PREFIX "js." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/javascript")


# CSS test executable

add_executable(guchho_css_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_css_test_objects>
)

target_link_libraries(
    guchho_css_tests
    PRIVATE
        guchho_css
        guchho_common
)

target_include_directories(
    guchho_css_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_css_tests)
guchho_enable_sanitizers(guchho_css_tests)

guchho_discover_tests(guchho_css_tests PREFIX "css." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/css")


# HTML test executable

add_executable(guchho_html_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_html_test_objects>
)

target_link_libraries(
    guchho_html_tests
    PRIVATE
        guchho_html
        guchho_common
)

target_include_directories(
    guchho_html_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_html_tests)
guchho_enable_sanitizers(guchho_html_tests)

guchho_discover_tests(guchho_html_tests PREFIX "html." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/html")


# helpers test executable

add_executable(guchho_helpers_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_helpers_test_objects>
)


target_link_libraries(
    guchho_helpers_tests
    PRIVATE
        guchho_common
)

target_include_directories(
    guchho_helpers_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_helpers_tests)
guchho_enable_sanitizers(guchho_helpers_tests)

guchho_discover_tests(guchho_helpers_tests PREFIX "helpers.")


# core test executable

add_executable(guchho_core_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_core_test_objects>
)

target_link_libraries(
    guchho_core_tests
    PRIVATE
        guchho_common
        guchho_css
        guchho_js
)

target_include_directories(
    guchho_core_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_core_tests)
guchho_enable_sanitizers(guchho_core_tests)

guchho_discover_tests(guchho_core_tests PREFIX "core.")


# cache test executable

add_executable(guchho_cache_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_cache_test_objects>
)

target_link_libraries(
    guchho_cache_tests
    PRIVATE
        guchho_cache
)

target_include_directories(
    guchho_cache_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_cache_tests)
guchho_enable_sanitizers(guchho_cache_tests)

guchho_discover_tests(guchho_cache_tests PREFIX "cache." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cache")


# resolver test executable

add_executable(guchho_resolver_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_resolver_test_objects>
)

target_link_libraries(
    guchho_resolver_tests
    PRIVATE
        guchho_resolver
)

target_include_directories(
    guchho_resolver_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_resolver_tests)
guchho_enable_sanitizers(guchho_resolver_tests)

guchho_discover_tests(guchho_resolver_tests PREFIX "resolver." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resolver")


# graph test executable

add_executable(guchho_graph_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_graph_test_objects>
)

target_link_libraries(
    guchho_graph_tests
    PRIVATE
        guchho_graph
)

target_include_directories(
    guchho_graph_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_graph_tests)
guchho_enable_sanitizers(guchho_graph_tests)

guchho_discover_tests(guchho_graph_tests PREFIX "graph." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/graph")


# linker test executable

add_executable(guchho_linker_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_linker_test_objects>
)

target_link_libraries(
    guchho_linker_tests
    PRIVATE
        guchho_bundler
)

target_include_directories(
    guchho_linker_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_linker_tests)
guchho_enable_sanitizers(guchho_linker_tests)

guchho_discover_tests(guchho_linker_tests PREFIX "linker." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/linker")


# bundler test executable

add_executable(guchho_bundler_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_bundler_test_objects>
)

target_link_libraries(
    guchho_bundler_tests
    PRIVATE
        guchho_bundler
)

target_include_directories(
    guchho_bundler_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_bundler_tests)
guchho_enable_sanitizers(guchho_bundler_tests)

guchho_discover_tests(guchho_bundler_tests PREFIX "bundler." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bundler")


# api test executable

add_executable(guchho_api_tests
    test_assertions.cpp
    $<TARGET_OBJECTS:guchho_api_test_objects>
)

target_link_libraries(
    guchho_api_tests
    PRIVATE
        guchho_api
)

target_include_directories(
    guchho_api_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

guchho_set_warnings(guchho_api_tests)
guchho_enable_sanitizers(guchho_api_tests)

guchho_discover_tests(guchho_api_tests PREFIX "api." WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/api")

