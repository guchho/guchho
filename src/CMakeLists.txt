# Common library (helpers + core, shared by every language)

set(GUCHHO_COMMON_SOURCES
    # helpers
    helpers/xxhash/xxhash.cpp
    helpers/base64.cpp
    helpers/comment.cpp
    helpers/dataurl.cpp
    helpers/glob.cpp
    helpers/hash.cpp
    helpers/joiner.cpp
    helpers/mime.cpp
    helpers/path.cpp
    helpers/quote.cpp
    helpers/sha.cpp
    helpers/strings.cpp
    helpers/timer.cpp
    helpers/typo.cpp
    helpers/url.cpp
    helpers/utf8.cpp

    # core/unicode/
    core/unicode/unicode.cpp
    core/unicode/range_table.cpp

    # core/entities/
    core/entities/entities.cpp
    core/entities/entities_table.cpp

    # core/logger/
    core/logger/logger.cpp
    core/logger/terminal.cpp
    core/logger/messages.cpp

    # core/compiler/
    core/compiler/compiler.cpp

    # core/filesystem/
    core/filesystem/filepath.cpp
    core/filesystem/filesystem.cpp
    core/filesystem/filesystem_zip.cpp
    core/filesystem/miniz/miniz.cpp

    # core/sourcemap/
    core/sourcemap/sourcemap.cpp

    # core/compat/
    core/compat/compat.cpp

    # core/config/
    core/config/config.cpp
    core/config/globals.cpp
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



# CSS library

set(GUCHHO_CSS_SOURCES
    language/css/css_lexer.cpp
    language/css/css_ast.cpp
    language/css/css_parser.cpp
    language/css/css_selectors.cpp
    language/css/css_properties.cpp
    language/css/css_nesting.cpp
    language/css/css_media.cpp
    language/css/css_animation.cpp
    language/css/css_border.cpp
    language/css/css_box.cpp
    language/css/css_spaces.cpp
    language/css/css_color.cpp
    language/css/css_shadow.cpp
    language/css/css_calc.cpp
    language/css/css_container.cpp
    language/css/css_gradient.cpp
    language/css/css_helpers.cpp
    language/css/css_font.cpp
    language/css/css_transform.cpp
    language/css/css_composes.cpp
    language/css/css_list.cpp
    language/css/css_printer.cpp

    # CSS-only compat tables
    core/compat/css_table.cpp
)

add_library(guchho_css STATIC)

target_sources(guchho_css
    PRIVATE
        ${GUCHHO_CSS_SOURCES}
)

target_include_directories(guchho_css
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_css
    PUBLIC
        guchho_common
)

guchho_set_warnings(guchho_css)
guchho_enable_sanitizers(guchho_css)



# JavaScript library

set(GUCHHO_JS_SOURCES
    language/javascript/js_identifier.cpp
    language/javascript/js_lexer.cpp
    language/javascript/js_ast.cpp
    language/javascript/js_helpers.cpp
    language/javascript/js_runtime.cpp
    language/javascript/js_renamer.cpp
    language/javascript/js_printer.cpp
    language/javascript/js_bind.cpp
    language/javascript/js_parser.cpp
    language/javascript/js_parser_expression.cpp
    language/javascript/js_parser_import.cpp
    language/javascript/js_parser_lower.cpp
    language/javascript/js_parser_lower_class.cpp
    language/javascript/js_parser_mangle.cpp
    language/javascript/js_parser_statement.cpp
    language/javascript/js_sourcemap.cpp
    language/javascript/json_parser.cpp
    language/typescript/ts_parser.cpp
    
    # JS-only compat tables
    core/compat/js_table.cpp
)

add_library(guchho_js STATIC)

target_sources(guchho_js
    PRIVATE
        ${GUCHHO_JS_SOURCES}
)

target_include_directories(guchho_js
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_js
    PUBLIC
        guchho_common
)

guchho_set_warnings(guchho_js)
guchho_enable_sanitizers(guchho_js)



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