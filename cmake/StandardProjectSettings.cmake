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
# MinGW (GCC/Clang on Windows)
# ------------------------------------------------------------

if(MINGW)

    # Allow more sections in large translation units (equivalent to MSVC /bigobj).
    add_compile_options(
        -Wa,-mbig-obj
    )


    # --------------------------------------------------------
    # Stage the GCC runtime DLLs beside the executables.
    #
    # A MinGW build is not self-contained: libstdc++, libgcc and
    # libwinpthread live in the toolchain's bin directory, and Windows
    # only searches an executable's own directory and PATH. Without a
    # copy next to the binary a freshly linked executable cannot load
    # its runtime, which surfaces as STATUS_ENTRYPOINT_NOT_FOUND
    # (0xC0000139) when an unrelated DLL of the same name is found
    # first, or STATUS_DLL_NOT_FOUND (0xC0000135) when none is. The
    # former makes CTest discovery fall back to a bogus
    # "<module>.discovery_failed" test; the latter makes an npm package
    # built with MinGW unrunnable off the build machine.
    #
    # The patterns cover the 32-bit (libgcc_s_dw2) and 64-bit
    # (libgcc_s_seh) runtimes. This also puts the DLLs where
    # scripts/build-npm.sh looks when it packs the npm binary, so its
    # existing runtime-copying step keeps working unchanged.
    # --------------------------------------------------------

    get_filename_component(
        GUCHHO_MINGW_RUNTIME_DIR
        "${CMAKE_CXX_COMPILER}"
        DIRECTORY
    )

    file(
        GLOB
        GUCHHO_MINGW_RUNTIME_DLLS
        "${GUCHHO_MINGW_RUNTIME_DIR}/libstdc++-*.dll"
        "${GUCHHO_MINGW_RUNTIME_DIR}/libgcc_s_*.dll"
        "${GUCHHO_MINGW_RUNTIME_DIR}/libwinpthread-*.dll"
    )

    if(GUCHHO_MINGW_RUNTIME_DLLS)

        file(
            COPY
            ${GUCHHO_MINGW_RUNTIME_DLLS}
            DESTINATION
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        )

    endif()

endif()


# ------------------------------------------------------------
# Windows thread stack size
#
# The JavaScript parser is recursive descent, so parseStmt takes a
# new frame for every level of nesting in the source. That makes the
# depth a file may nest a property of the stack Windows hands out
# rather than of the parser: each thread, including the ones
# std::thread creates, is limited to the SizeOfStackReserve named in
# the image header, which defaults to 1 MB. "TestMinifyNestedLabelsNoBundle"
# nests around 350 labeled statements and overflowed that, dying on
# the guard page with a SIGSEGV part way through the parse.
#
# Raise it to 16 MB - the same size helpers::kWorkerStackSize asks
# for per-thread on POSIX, where there is no image-wide switch (see
# src/helpers/thread.cpp). The reservation is virtual -
# SizeOfStackCommit still asks for one page - so nothing is touched
# until a thread actually walks deep enough to need it.
# ------------------------------------------------------------

if(WIN32)

    set(
        GUCHHO_WINDOWS_STACK_RESERVE
        16777216
    )

    if(MINGW)

        # GNU ld spells it --stack, with the size as a comma argument.
        add_link_options(
            "-Wl,--stack,${GUCHHO_WINDOWS_STACK_RESERVE}"
        )

    elseif(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")

        add_link_options(
            "/STACK:${GUCHHO_WINDOWS_STACK_RESERVE}"
        )

    else()

        # clang++ driving lld-link in GNU mode: forward the option past
        # the driver, which does not accept MSVC-style link flags itself.
        add_link_options(
            "-Xlinker"
            "/STACK:${GUCHHO_WINDOWS_STACK_RESERVE}"
        )

    endif()

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