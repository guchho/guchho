# ============================================================
# Guchho sanitizers
# ============================================================

option(
    GUCHHO_ENABLE_ASAN
    "Enable AddressSanitizer"
    OFF
)

option(
    GUCHHO_ENABLE_UBSAN
    "Enable UndefinedBehaviorSanitizer"
    OFF
)


function(guchho_enable_sanitizers TARGET)

    if(NOT TARGET "${TARGET}")
        message(
            FATAL_ERROR
            "guchho_enable_sanitizers(): "
            "target '${TARGET}' does not exist"
        )
    endif()


    # --------------------------------------------------------
    # MSVC
    # --------------------------------------------------------

    if(MSVC)

        if(GUCHHO_ENABLE_ASAN)

            # MSVC supports AddressSanitizer.
            target_compile_options(
                "${TARGET}"
                PRIVATE
                    /fsanitize=address
                )

        endif()


        if(GUCHHO_ENABLE_UBSAN)

            message(
                WARNING
                "Guchho: UndefinedBehaviorSanitizer is not "
                "supported by MSVC. GUCHHO_ENABLE_UBSAN ignored."
            )

        endif()

        return()

    endif()


    # --------------------------------------------------------
    # Clang / GNU
    # --------------------------------------------------------

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")

        set(GUCHHO_SANITIZERS "")


        # ----------------------------------------------------
        # AddressSanitizer
        # ----------------------------------------------------

        if(GUCHHO_ENABLE_ASAN)

            list(
                APPEND
                    GUCHHO_SANITIZERS
                    address
            )

            target_compile_options(
                "${TARGET}"
                PRIVATE
                    -fno-omit-frame-pointer
            )

        endif()


        # ----------------------------------------------------
        # UndefinedBehaviorSanitizer
        # ----------------------------------------------------

        if(GUCHHO_ENABLE_UBSAN)

            list(
                APPEND
                    GUCHHO_SANITIZERS
                    undefined
            )

        endif()


        # ----------------------------------------------------
        # Apply sanitizers
        # ----------------------------------------------------

        if(GUCHHO_SANITIZERS)

            string(
                JOIN
                    ","
                    GUCHHO_SANITIZER_FLAGS
                    ${GUCHHO_SANITIZERS}
            )


            target_compile_options(
                "${TARGET}"
                PRIVATE
                    "-fsanitize=${GUCHHO_SANITIZER_FLAGS}"
            )


            target_link_options(
                "${TARGET}"
                PRIVATE
                    "-fsanitize=${GUCHHO_SANITIZER_FLAGS}"
            )

        endif()

    endif()

endfunction()