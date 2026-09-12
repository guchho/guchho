# ============================================================
# Guchho compiler warnings
# ============================================================

option(
    GUCHHO_ENABLE_STRICT_WARNINGS
    "Enable strict compiler warnings"
    ON
)


function(guchho_set_warnings TARGET)

    # --------------------------------------------------------
    # Skip if strict warnings are disabled
    # --------------------------------------------------------

    if(NOT GUCHHO_ENABLE_STRICT_WARNINGS)
        return()
    endif()


    # --------------------------------------------------------
    # MSVC
    # --------------------------------------------------------

    if(MSVC)

        target_compile_options(${TARGET} PRIVATE

            # Warning level
            /W4

            # Enforce standard C++ behavior
            /permissive-

            # ------------------------------------------------
            # Important MSVC warnings
            # ------------------------------------------------

            # Conversion warnings
            /w14242

            # Possible loss of data
            /w14254

            # Function hiding / overriding
            /w14263

            # Exception specification
            /w14287

            # Conversion issues
            /w14296

            # Signed/unsigned conversion
            /w14311
        )


    # --------------------------------------------------------
    # Clang
    # --------------------------------------------------------

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

        target_compile_options(${TARGET} PRIVATE

            # ------------------------------------------------
            # Standard warnings
            # ------------------------------------------------

            -Wall
            -Wextra
            -Wpedantic

            # ------------------------------------------------
            # Code quality
            # ------------------------------------------------

            -Wshadow
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wunused

            # ------------------------------------------------
            # Type safety
            # ------------------------------------------------

            -Wconversion
            -Wsign-conversion

            # ------------------------------------------------
            # Correctness
            # ------------------------------------------------

            -Wnull-dereference

            # ------------------------------------------------
            # Cast/style
            # ------------------------------------------------

            -Wold-style-cast
            -Wcast-align

            # ------------------------------------------------
            # Format string checking
            # ------------------------------------------------

            -Wformat=2

            # ------------------------------------------------
            # Floating-point conversions
            # ------------------------------------------------

            -Wdouble-promotion
        )


    # --------------------------------------------------------
    # GCC
    # --------------------------------------------------------

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

        target_compile_options(${TARGET} PRIVATE

            # ------------------------------------------------
            # Standard warnings
            # ------------------------------------------------

            -Wall
            -Wextra
            -Wpedantic

            # ------------------------------------------------
            # Code quality
            # ------------------------------------------------

            -Wshadow
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wunused

            # ------------------------------------------------
            # Type safety
            # ------------------------------------------------

            -Wconversion
            -Wsign-conversion

            # ------------------------------------------------
            # Correctness
            # ------------------------------------------------

            -Wnull-dereference

            # ------------------------------------------------
            # Cast/style
            # ------------------------------------------------

            -Wold-style-cast
            -Wcast-align

            # ------------------------------------------------
            # Format string checking
            # ------------------------------------------------

            -Wformat=2

            # ------------------------------------------------
            # Floating-point conversions
            # ------------------------------------------------

            -Wdouble-promotion
        )

    endif()

endfunction()