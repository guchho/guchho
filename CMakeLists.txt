cmake_minimum_required(VERSION 3.25)

project(
    guchho
    VERSION 0.1.0
    DESCRIPTION "Fast build system for web applications."
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

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