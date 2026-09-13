# Custom test framework + CTest discovery.

include(GuchhoTesting)

# The library targets expose cxx_std_20 through guchho_common (PUBLIC), but the
# test object libraries don't link it. Match the standard explicitly so test
# objects compile with the same C++ version as the rest of the project.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Test subdirectories

add_subdirectory(helpers)



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

