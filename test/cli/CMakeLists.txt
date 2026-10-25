# Command line test sources.
#
# One file per command, in the order the commands appear in the usage text, so
# that reading this list top to bottom is the same as reading what a person sees
# after typing "guchho". The last file is the odd one out: it runs the built
# executable instead of calling the entry point, and its comment says why.

add_library(guchho_cli_test_objects OBJECT
    cli_run_test.cpp
    cli_build_test.cpp
    cli_minify_test.cpp
    cli_lint_test.cpp
    cli_multi_config_test.cpp
    cli_transform_test.cpp
    cli_project_test.cpp
    cli_init_test.cpp
    cli_runtime_test.cpp
    cli_serve_test.cpp
    cli_watch_test.cpp
    cli_plugins_test.cpp
    cli_service_test.cpp
    cli_service_compile_test.cpp
    cli_pack_test.cpp
    cli_unpack_test.cpp
    cli_exe_test.cpp
    wasm_abi_test.cpp
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

# The WebAssembly C ABI is only built into guchho_cli when the WebAssembly
# flavor is selected, and a native build is not that flavor. The wrapper over
# RunServiceRequest is worth testing natively all the same, so wasm_abi.cpp is
# compiled into the test objects here. GUCHHO_VERSION_STRING is normally a
# PRIVATE definition on guchho_cli (src/CMakeLists.txt), which does not reach
# this object library, so it is named again here.
#
# Guarded rather than unconditional: a native build that turned GUCHHO_WASM on
# already has these symbols in guchho_cli, and compiling the file twice would
# be a duplicate definition.
if(NOT GUCHHO_WASM)
    target_sources(guchho_cli_test_objects PRIVATE
        ${PROJECT_SOURCE_DIR}/src/cli/wasm_abi.cpp
    )
    target_compile_definitions(guchho_cli_test_objects PRIVATE
        GUCHHO_VERSION_STRING="${PROJECT_VERSION}"
    )
endif()

target_link_libraries(
    guchho_cli_test_objects
    PRIVATE
        guchho_cli
        guchho_common
)

guchho_set_warnings(guchho_cli_test_objects)
guchho_enable_sanitizers(guchho_cli_test_objects)
