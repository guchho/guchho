# Command line test sources.
#
# One file per command, in the order the commands appear in the usage text, so
# that reading this list top to bottom is the same as reading what a person sees
# after typing "guchho". The last file is the odd one out: it runs the built
# executable instead of calling the entry point, and its comment says why.

add_library(guchho_cli_test_objects OBJECT
    cli_run_test.cpp
    cli_build_test.cpp
    cli_transform_test.cpp
    cli_project_test.cpp
    cli_serve_test.cpp
    cli_watch_test.cpp
    cli_plugins_test.cpp
    cli_exe_test.cpp
)

target_include_directories(
    guchho_cli_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# The path to the executable under test, for the one file that runs it.
#
# This is on the object library and not only on the test executable, because that
# is where cli_exe_test.cpp is compiled: a definition added to the executable
# reaches the sources listed in it and not the ones already compiled into an
# object library, so the macro would be missing exactly where it is used.
target_compile_definitions(
    guchho_cli_test_objects
    PRIVATE
        GUCHHO_TEST_EXE="$<TARGET_FILE:guchho>"
)

target_link_libraries(
    guchho_cli_test_objects
    PRIVATE
        guchho_cli
        guchho_common
)

guchho_set_warnings(guchho_cli_test_objects)
guchho_enable_sanitizers(guchho_cli_test_objects)
