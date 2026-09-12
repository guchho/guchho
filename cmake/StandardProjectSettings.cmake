# ============================================================
# Guchho build configuration
# ============================================================

# ------------------------------------------------------------
# Default build type
#
# Only applies to single-config generators such as Ninja.
# Visual Studio/Xcode use CMAKE_CONFIGURATION_TYPES instead.
# ------------------------------------------------------------

if(
    NOT CMAKE_BUILD_TYPE
    AND NOT CMAKE_CONFIGURATION_TYPES
)

    set(
        CMAKE_BUILD_TYPE
        Debug
        CACHE STRING
        "Build type"
        FORCE
    )

    set_property(
        CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS
            Debug
            Release
            RelWithDebInfo
            MinSizeRel
    )

endif()


# ------------------------------------------------------------
# Output directories
# ------------------------------------------------------------

set(
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

set(
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)


# ------------------------------------------------------------
# Multi-config generators
#
# Visual Studio / Xcode may otherwise place binaries under:
#
#   bin/Debug
#   bin/Release
#
# Keep the same output layout as single-config builds.
# ------------------------------------------------------------

if(CMAKE_CONFIGURATION_TYPES)

    foreach(CONFIG IN LISTS CMAKE_CONFIGURATION_TYPES)

        string(
            TOUPPER
            "${CONFIG}"
            CONFIG_UPPER
        )

        set(
            CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER}
            "${CMAKE_BINARY_DIR}/bin"
        )

        set(
            CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER}
            "${CMAKE_BINARY_DIR}/lib"
        )

        set(
            CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER}
            "${CMAKE_BINARY_DIR}/lib"
        )

    endforeach()

endif()


# ------------------------------------------------------------
# MSVC
# ------------------------------------------------------------

if(MSVC)

    # Debug libraries can use a "d" postfix.
    set(
        CMAKE_DEBUG_POSTFIX
        "d"
        CACHE STRING
        "Debug library postfix"
    )


    # Parallel compilation.
    add_compile_options(
        /MP
    )


    # Treat source and execution strings as UTF-8.
    add_compile_options(
        /utf-8
    )


    # Allow more sections in large translation units.
    add_compile_options(
        /bigobj
    )


    # Force synchronous PDB access in Debug builds.
    add_compile_options(
        "$<$<CONFIG:Debug>:/FS>"
    )

endif()


# ------------------------------------------------------------
# Organize targets in IDEs
# ------------------------------------------------------------

set_property(
    GLOBAL
    PROPERTY
        USE_FOLDERS
        ON
)


# ------------------------------------------------------------
# Installation directories
# ------------------------------------------------------------

include(GNUInstallDirs)