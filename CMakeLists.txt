cmake_minimum_required(VERSION 3.25)

project(
    guchho
    VERSION 1.0.9
    DESCRIPTION "Fast build system for web applications."
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# The WASM build is single-threaded so it needs no pthreads and no
# SharedArrayBuffer (which browsers only get behind COOP/COEP headers).
# helpers::Thread then runs its callable inline, and every fan-out site in
# the engine is join-style, so the whole pipeline serializes without
# deadlocking. See src/helpers/thread.cpp.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_compile_definitions(GUCHHO_WASM_SERIAL)
endif()

# GUCHHO_WASM selects the WebAssembly flavor of the engine: the dev server,
# the file watcher and child-process spawning are compiled out (they cannot
# work without OS support), and the C ABI entry points for embedding the
# engine in JavaScript are compiled in. It defaults to ON under Emscripten
# and OFF everywhere else, and a native build that turns it on deliberately
# gets the same feature set — which is what makes the gates testable.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(GUCHHO_WASM_DEFAULT ON)
else()
    set(GUCHHO_WASM_DEFAULT OFF)
endif()
option(GUCHHO_WASM "Build the WebAssembly engine flavor" ${GUCHHO_WASM_DEFAULT})
if(GUCHHO_WASM)
    add_compile_definitions(GUCHHO_WASM)
endif()

include(StandardProjectSettings)
include(CompilerWarnings)
include(Sanitizers)
include(Resources)

include(CTest)
include(GNUInstallDirs)

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

include(Packaging)