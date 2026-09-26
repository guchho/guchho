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
    helpers/process.cpp
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


# ============================================================
# HTML library
# ============================================================

add_library(guchho_html STATIC)

target_sources(guchho_html
    PRIVATE
        language/html/html_analysis.cpp
        language/html/html_ast.cpp
        language/html/html_bridge.cpp
        language/html/html_helpers.cpp
        language/html/html_lexer.cpp
        language/html/html_parser.cpp
        language/html/html_printer.cpp
)

target_include_directories(guchho_html
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_html
    PUBLIC
        guchho_common
)

guchho_set_warnings(guchho_html)
guchho_enable_sanitizers(guchho_html)


# ============================================================
# CSS library
# ============================================================

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



# ============================================================
# JavaScript library
# ============================================================

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
    language/javascript/ts_parser.cpp
    
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
# Cache library
# ============================================================

add_library(guchho_cache STATIC)

target_sources(guchho_cache
    PRIVATE
        cache/cache.cpp
        cache/cache_fs.cpp
        cache/cache_ast.cpp
)

target_include_directories(guchho_cache
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_cache
    PUBLIC
        guchho_common
        guchho_js
        guchho_css
        guchho_html
)

guchho_set_warnings(guchho_cache)
guchho_enable_sanitizers(guchho_cache)


# ============================================================
# Resolver library
# ============================================================

set(GUCHHO_RESOLVER_SOURCES
    resolver/resolver.cpp
    resolver/guchho_json.cpp
    resolver/package_json.cpp
    resolver/tsconfig_json.cpp
    resolver/yarnpnp.cpp
)

add_library(guchho_resolver STATIC)

target_sources(guchho_resolver
    PRIVATE
        ${GUCHHO_RESOLVER_SOURCES}
)

target_include_directories(guchho_resolver
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_resolver
    PUBLIC
        guchho_common
        guchho_js
        guchho_cache
)

guchho_set_warnings(guchho_resolver)
guchho_enable_sanitizers(guchho_resolver)


# ============================================================
# Graph library
# ============================================================

set(GUCHHO_GRAPH_SOURCES
    graph/graph.cpp
    graph/input.cpp
)

add_library(guchho_graph STATIC)

target_sources(guchho_graph
    PRIVATE
        ${GUCHHO_GRAPH_SOURCES}
)

target_include_directories(guchho_graph
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_graph
    PUBLIC
        guchho_common
        guchho_js
        guchho_css
        guchho_html
)

guchho_set_warnings(guchho_graph)
guchho_enable_sanitizers(guchho_graph)


# ============================================================
# Bundler library (scan + link phases)
# ============================================================

set(GUCHHO_BUNDLER_SOURCES
    linker/linker.cpp
    linker/linker_scan.cpp
    linker/linker_chunk.cpp
    linker/linker_js.cpp
    linker/linker_mangle.cpp
    linker/linker_css.cpp

    bundler/bundler.cpp
    bundler/bundler_scan.cpp
    bundler/bundler_resolve.cpp
    bundler/bundler_scanner.cpp
    bundler/bundler_compile.cpp
    bundler/bundler_html.cpp
)

add_library(guchho_bundler STATIC)

target_sources(guchho_bundler
    PRIVATE
        ${GUCHHO_BUNDLER_SOURCES}
)

target_include_directories(guchho_bundler
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_bundler
    PUBLIC
        guchho_common
        guchho_js
        guchho_css
        guchho_html
        guchho_cache
        guchho_resolver
        guchho_graph
)

guchho_set_warnings(guchho_bundler)
guchho_enable_sanitizers(guchho_bundler)


# ============================================================
# API library (public one-shot / context / server entry points)
# ============================================================

set(GUCHHO_API_SOURCES
    api/api.cpp
    api/api_serve.cpp
    api/api_watcher.cpp
)

add_library(guchho_api STATIC)

target_sources(guchho_api
    PRIVATE
        ${GUCHHO_API_SOURCES}
)

target_include_directories(guchho_api
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_api
    PUBLIC
        guchho_common
        guchho_bundler
)

guchho_set_warnings(guchho_api)
guchho_enable_sanitizers(guchho_api)


# ============================================================
# CLI library (the command line surface)
# ============================================================

# One file per command, plus the flag grammar they all read. The declarations
# they share are in include/guchho/cli.hpp, under the section marked as being
# for the command line's own files rather than for an embedder.
set(GUCHHO_CLI_SOURCES
    cli/cli_build.cpp
    cli/cli_flags.cpp
    cli/cli_help.cpp
    cli/cli_options.cpp
    cli/cli_project.cpp
    cli/cli_run.cpp
    cli/cli_serve.cpp
    cli/cli_watch.cpp
    cli/cli_mangle.cpp
)

add_library(guchho_cli STATIC)

target_sources(guchho_cli
    PRIVATE
        ${GUCHHO_CLI_SOURCES}
)

target_include_directories(guchho_cli
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(guchho_cli
    PUBLIC
        guchho_api
)

guchho_set_warnings(guchho_cli)
guchho_enable_sanitizers(guchho_cli)


# ============================================================
# Guchho executable
# ============================================================

add_executable(guchho
    main.cpp
)

target_link_libraries(guchho
    PRIVATE
        guchho_common
        guchho_bundler
        guchho_cli
)

guchho_set_warnings(guchho)
guchho_enable_sanitizers(guchho)

guchho_enable_resources(guchho)

# main.cpp takes its arguments through the wide entry point on Windows so that
# the paths a person types survive the trip from the shell. MSVC finds that
# entry point by itself, but MinGW only calls it when the runtime is told to
# start in its Unicode flavor, so the flag belongs on the link line of the one
# executable that has a main.
if(MINGW)
    target_link_options(guchho PRIVATE -municode)
endif()

set_target_properties(guchho PROPERTIES
    OUTPUT_NAME "guchho"
)