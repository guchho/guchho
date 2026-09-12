# ============================================================
# Guchho Installation & Packaging
# ============================================================

include(GNUInstallDirs)


# ============================================================
# Install Targets
# ============================================================

install(
    TARGETS guchho
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)


# ============================================================
# Install Public Headers
# ============================================================

if(EXISTS "${PROJECT_SOURCE_DIR}/include")

    install(
        DIRECTORY
            "${PROJECT_SOURCE_DIR}/include/"
        DESTINATION
            "${CMAKE_INSTALL_INCLUDEDIR}"
    )

endif()


# ============================================================
# Install Documentation
# ============================================================

set(GUCHHO_DOCUMENTATION_FILES)

if(EXISTS "${PROJECT_SOURCE_DIR}/README.md")

    list(
        APPEND
        GUCHHO_DOCUMENTATION_FILES
        "${PROJECT_SOURCE_DIR}/README.md"
    )

endif()


if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE")

    list(
        APPEND
        GUCHHO_DOCUMENTATION_FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
    )

endif()


if(GUCHHO_DOCUMENTATION_FILES)

    install(
        FILES
            ${GUCHHO_DOCUMENTATION_FILES}
        DESTINATION
            "${CMAKE_INSTALL_DOCDIR}"
    )

endif()


# ============================================================
# Determine Guchho Platform
#
# Naming:
#
#   win32-x64
#   win32-arm64
#   linux-x64
#   linux-arm64
#   darwin-x64
#   darwin-arm64
# ============================================================

set(GUCHHO_PLATFORM "")


if(WIN32)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES
        "AMD64|amd64|x86_64|X86_64"
    )

        set(
            GUCHHO_PLATFORM
            "win32-x64"
        )

    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES
        "ARM64|arm64|aarch64|AARCH64"
    )

        set(
            GUCHHO_PLATFORM
            "win32-arm64"
        )

    endif()


elseif(APPLE)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES
        "ARM64|arm64|aarch64|AARCH64"
    )

        set(
            GUCHHO_PLATFORM
            "darwin-arm64"
        )

    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES
        "AMD64|amd64|x86_64|X86_64"
    )

        set(
            GUCHHO_PLATFORM
            "darwin-x64"
        )

    endif()


elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    if(CMAKE_SYSTEM_PROCESSOR MATCHES
        "ARM64|arm64|aarch64|AARCH64"
    )

        set(
            GUCHHO_PLATFORM
            "linux-arm64"
        )

    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES
        "AMD64|amd64|x86_64|X86_64"
    )

        set(
            GUCHHO_PLATFORM
            "linux-x64"
        )

    endif()

endif()


# ============================================================
# Fallback for unsupported architectures
# ============================================================

if(GUCHHO_PLATFORM STREQUAL "")

    string(
        TOLOWER
        "${CMAKE_SYSTEM_NAME}"
        GUCHHO_SYSTEM_NAME
    )

    string(
        TOLOWER
        "${CMAKE_SYSTEM_PROCESSOR}"
        GUCHHO_SYSTEM_PROCESSOR
    )

    set(
        GUCHHO_PLATFORM
        "${GUCHHO_SYSTEM_NAME}-${GUCHHO_SYSTEM_PROCESSOR}"
    )

endif()


# ============================================================
# CPack Metadata
# ============================================================

set(
    CPACK_PACKAGE_NAME
    "${PROJECT_NAME}"
)

set(
    CPACK_PACKAGE_VENDOR
    "CodeHemu"
)

set(
    CPACK_PACKAGE_CONTACT
    "hello@codehemu.in"
)

set(
    CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Fast build system for web applications."
)

set(
    CPACK_PACKAGE_DESCRIPTION
    "Guchho is a fast native C++ build system and bundler for modern web applications."
)


# ============================================================
# Package Version
# ============================================================

set(
    CPACK_PACKAGE_VERSION
    "${PROJECT_VERSION}"
)

set(
    CPACK_PACKAGE_VERSION_MAJOR
    "${PROJECT_VERSION_MAJOR}"
)

set(
    CPACK_PACKAGE_VERSION_MINOR
    "${PROJECT_VERSION_MINOR}"
)

set(
    CPACK_PACKAGE_VERSION_PATCH
    "${PROJECT_VERSION_PATCH}"
)


# ============================================================
# Package File Name
#
# Example:
#
#   guchho-0.1.0-win32-x64
#   guchho-0.1.0-linux-x64
#   guchho-0.1.0-darwin-arm64
# ============================================================

set(
    CPACK_PACKAGE_FILE_NAME
    "${PROJECT_NAME}-${PROJECT_VERSION}-${GUCHHO_PLATFORM}"
)


# ============================================================
# Package Installation Directory
# ============================================================

set(
    CPACK_PACKAGE_INSTALL_DIRECTORY
    "Guchho"
)


# ============================================================
# Package Resources
# ============================================================

if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE")

    set(
        CPACK_RESOURCE_FILE_LICENSE
        "${PROJECT_SOURCE_DIR}/LICENSE"
    )

endif()


if(EXISTS "${PROJECT_SOURCE_DIR}/README.md")

    set(
        CPACK_RESOURCE_FILE_README
        "${PROJECT_SOURCE_DIR}/README.md"
    )

endif()


# ============================================================
# Platform Package Generators
# ============================================================

if(WIN32)

    # Portable archive + Windows installer
    set(
        CPACK_GENERATOR
        "ZIP;NSIS"
    )


elseif(APPLE)

    # Portable macOS archive
    set(
        CPACK_GENERATOR
        "TGZ"
    )


elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    # Portable Linux archive
    set(
        CPACK_GENERATOR
        "TGZ"
    )


elseif(UNIX)

    # Generic Unix fallback
    set(
        CPACK_GENERATOR
        "TGZ"
    )

endif()


# ============================================================
# Windows NSIS
# ============================================================

if(WIN32)

    set(
        CPACK_NSIS_DISPLAY_NAME
        "Guchho"
    )

    set(
        CPACK_NSIS_PACKAGE_NAME
        "Guchho"
    )

    set(
        CPACK_NSIS_CONTACT
        "hello@codehemu.in"
    )

    set(
        CPACK_NSIS_HELP_LINK
        "https://github.com/guchho/guchho"
    )

    set(
        CPACK_NSIS_URL_INFO_ABOUT
        "https://github.com/guchho/guchho"
    )

    # Allow the installer to optionally add Guchho to PATH.
    set(
        CPACK_NSIS_MODIFY_PATH
        ON
    )

    # Start Menu shortcut.
    set(
        CPACK_NSIS_MENU_LINKS
        "https://github.com/guchho/guchho"
        "Guchho on GitHub"
    )

endif()


# ============================================================
# Archive Configuration
# ============================================================

set(
    CPACK_ARCHIVE_THREADS
    0
)


# ============================================================
# CPack
# ============================================================

include(CPack)