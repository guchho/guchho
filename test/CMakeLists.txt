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

