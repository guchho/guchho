# Common library (helpers + core, shared by every language)

set(GUCHHO_COMMON_SOURCES
    # helpers
    helpers/utf8.cpp
    helpers/base64.cpp
    helpers/joiner.cpp
    helpers/path.cpp
    helpers/strings.cpp
    helpers/timer.cpp
    helpers/typo.cpp
    helpers/url.cpp
    helpers/xxhash/xxhash.cpp

    # core/unicode/
    core/unicode/unicode.cpp
    core/unicode/range_table.cpp
)


add_library(guchho_common STATIC)

target_sources(guchho_common
    PRIVATE
        ${GUCHHO_COMMON_SOURCES}
)

target_include_directories(guchho_common
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(guchho_common
    PUBLIC
        cxx_std_20
)

guchho_set_warnings(guchho_common)
guchho_enable_sanitizers(guchho_common)

if(WIN32)
    target_link_libraries(guchho_common PRIVATE dbghelp)
endif()

# ============================================================
# Guchho executable
# ============================================================

add_executable(guchho
    main.cpp
)

target_link_libraries(guchho
    PRIVATE
        guchho_common
)

guchho_set_warnings(guchho)
guchho_enable_sanitizers(guchho)

guchho_enable_resources(guchho)

set_target_properties(guchho PROPERTIES
    OUTPUT_NAME "guchho"
)