# ============================================================
# Guchho Platform Resources
#
# Windows:
#   - Version information
#   - Application manifest
#   - Windows executable resources
#
# macOS:
#   - Optional application bundle resources
#   - Info.plist
#
# Linux:
#   - No native resource file is required.
#   - Version/build metadata can be supplied through
#     compile definitions when needed.
# ============================================================


function(guchho_enable_resources TARGET)

    # --------------------------------------------------------
    # Validate target
    # --------------------------------------------------------

    if(NOT TARGET "${TARGET}")

        message(
            FATAL_ERROR
            "guchho_enable_resources(): "
            "target '${TARGET}' does not exist"
        )

    endif()


    # ========================================================
    # Windows
    # ========================================================

    if(WIN32)

        # ----------------------------------------------------
        # Windows resource script
        # ----------------------------------------------------

        set(
            GUCHHO_RESOURCE
            "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.rc"
        )


        if(EXISTS
            "${PROJECT_SOURCE_DIR}/src/api/windows.rc.in"
        )

            configure_file(
                "${PROJECT_SOURCE_DIR}/src/api/windows.rc.in"
                "${GUCHHO_RESOURCE}"
                @ONLY
            )

            target_sources(
                "${TARGET}"
                PRIVATE
                    "${GUCHHO_RESOURCE}"
            )

        else()

            message(
                WARNING
                "Guchho Windows resource template not found: "
                "${PROJECT_SOURCE_DIR}/src/api/windows.rc.in"
            )

        endif()


        # ----------------------------------------------------
        # Windows application manifest
        # ----------------------------------------------------

        set(
            GUCHHO_MANIFEST
            "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.manifest"
        )


        if(EXISTS
            "${PROJECT_SOURCE_DIR}/src/api/app.manifest"
        )

            configure_file(
                "${PROJECT_SOURCE_DIR}/src/api/app.manifest"
                "${GUCHHO_MANIFEST}"
                COPYONLY
            )


            # Add manifest to the executable through linker
            # options. This works with MSVC.
            if(MSVC)

                target_link_options(
                    "${TARGET}"
                    PRIVATE
                        "/MANIFEST:EMBED"
                        "/MANIFESTINPUT:${GUCHHO_MANIFEST}"
                )

            endif()

        else()

            message(
                WARNING
                "Guchho Windows manifest not found: "
                "${PROJECT_SOURCE_DIR}/src/api/app.manifest"
            )

        endif()


        # ----------------------------------------------------
        # Windows Unicode
        # ----------------------------------------------------

        if(MSVC)

            target_compile_definitions(
                "${TARGET}"
                PRIVATE
                    UNICODE
                    _UNICODE
                )

        endif()


        return()

    endif()


    # ========================================================
    # macOS
    # ========================================================

    if(APPLE)

        # ----------------------------------------------------
        # Enable bundle support only when requested.
        #
        # Normal Guchho CLI builds do not need an .app bundle.
        # ----------------------------------------------------

        if(GUCHHO_MACOS_BUNDLE)

            set_target_properties(
                "${TARGET}"
                PROPERTIES
                    MACOSX_BUNDLE TRUE
            )


            # ------------------------------------------------
            # Info.plist
            # ------------------------------------------------

            set(
                GUCHHO_INFO_PLIST
                "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-Info.plist"
            )


            if(EXISTS
                "${PROJECT_SOURCE_DIR}/src/api/Info.plist.in"
            )

                configure_file(
                    "${PROJECT_SOURCE_DIR}/src/api/Info.plist.in"
                    "${GUCHHO_INFO_PLIST}"
                    @ONLY
                )


                set_target_properties(
                    "${TARGET}"
                    PROPERTIES
                        MACOSX_BUNDLE_INFO_PLIST
                        "${GUCHHO_INFO_PLIST}"
                )

            endif()


            # ------------------------------------------------
            # Optional bundle resources
            # ------------------------------------------------

            if(EXISTS
                "${PROJECT_SOURCE_DIR}/src/api/macos"
            )

                target_sources(
                    "${TARGET}"
                    PRIVATE
                        "${PROJECT_SOURCE_DIR}/src/api/macos"
                )

            endif()

        endif()


        # ----------------------------------------------------
        # macOS version metadata
        # ----------------------------------------------------

        target_compile_definitions(
            "${TARGET}"
            PRIVATE
                GUCHHO_PLATFORM_MACOS=1
        )


        return()

    endif()


    # ========================================================
    # Linux / Unix
    # ========================================================

    if(UNIX)

        target_compile_definitions(
            "${TARGET}"
            PRIVATE
                GUCHHO_PLATFORM_UNIX=1
        )


        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")

            target_compile_definitions(
                "${TARGET}"
                PRIVATE
                    GUCHHO_PLATFORM_LINUX=1
            )

        endif()


        return()

    endif()


    # ========================================================
    # Unknown platform
    # ========================================================

    message(
        STATUS
        "Guchho platform resources are not configured for "
        "${CMAKE_SYSTEM_NAME}"
    )

endfunction()