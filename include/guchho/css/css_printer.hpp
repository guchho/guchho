#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/sourcemap.hpp"
#include "guchho/css/css_ast.hpp"


namespace guchho::css {

// Configuration options that control how the CSS printer formats its output.
// These options affect whitespace handling, source map generation, identifier
// escaping, comment preservation, and metafile output. The printer uses these
// options to produce either minified or human-readable CSS.
struct PrinterOptions {
    // When set, the printer uses this source map to map printed positions back
    // to the original source locations. This is used for remapping when bundling
    // multiple files that each already have source maps.
    sourcemap::SourceMapData* input_source_map{};


    // Line offset tables from the input source map, used for efficient lookup
    // of original positions during source map generation.
    std::vector<sourcemap::LineOffsetTable> line_offset_tables;

    // Maps compiler symbol references to their renamed identifiers. When a
    // symbol has been renamed during bundling (e.g., shortening a long CSS
    // class name), this map provides the new name. The printer looks up each
    // symbol reference here before printing.
    std::unordered_map<compiler::Ref, std::string, RefHash> local_names;

    // Maximum number of characters per line. When set to 0 (default), no line
    // wrapping is performed. When positive, the printer wraps lines that exceed
    // this limit using escaped newlines in strings and natural break points.
    int line_limit{};

    // Index of the source file being printed, used for source map generation
    // to correctly attribute output positions to the original file.
    uint32_t input_source_index{};

    // CSS features that the target environment does not support. The printer
    // will escape or transform syntax that relies on unsupported features
    // (e.g., escaping "</style" in inline CSS).
    compat::CSSFeature unsupported_features{};

    // When true, the printer produces minified output: all unnecessary
    // whitespace is removed, trailing semicolons in blocks are dropped, and
    // identifiers may be shortened.
    bool minify_whitespace{};

    // When true, non-ASCII characters in identifiers and strings are escaped
    // to their ASCII equivalents using Unicode escape sequences. This ensures
    // the output is safe for environments that do not support UTF-8.
    bool ascii_only{};

    // Controls the source map output mode: none, linked, external, or inline.
    config::SourceMap source_map{config::SourceMap::kNone};

    // When true, source mappings are emitted for each token and rule. This is
    // required for source map generation but adds overhead to printing.
    bool add_source_mappings{};

    // Controls how legal comments (those with @license, @preserve, etc.) are
    // handled: inline in the output, linked via a separate file, external, or
    // stripped entirely.
    config::LegalComments legal_comments{config::LegalComments::kInline};

    // When true, the printer records import paths in the JSON metadata for the
    // metafile output. The metafile lists all external dependencies and their
    // import kinds.
    bool needs_metafile{};

    // Format of the metafile JSON output: minified or unminified.
    config::MetafileFormat metafile_format{config::MetafileFormat::kUnminified};
};


// The result of printing a CSS AST. Contains the generated CSS text, any legal
// comments that were extracted (for linked/external comment modes), JSON metadata
// about imports, and an optional source map chunk for the printed output.
struct PrintResult {
    // The generated CSS text. This is the primary output of the printer.
    std::string css;

    // Legal comments extracted from the source. These are comments that should
    // be preserved in the output but were removed from the CSS text itself
    // (when legal_comments mode is linked or external).
    std::vector<std::string> extracted_legal_comments;

    // JSON entries describing external imports encountered during printing.
    // Each entry is a JSON object with "path" and "kind" fields, used by the
    // metafile to track external dependencies.
    std::vector<std::string> json_metadata_imports;

    // A source map chunk containing VLQ-encoded mappings for the printed CSS.
    // This is not a complete source map; the bundler joins many chunks from
    // different files to produce the final source map.
    sourcemap::Chunk source_map_chunk;
};

// Prints a parsed CSS AST to textual CSS output. This is the main entry point
// for the CSS printer. The options parameter controls formatting, minification,
// source map generation, and comment handling. The symbols parameter provides
// access to the symbol map for identifier renaming.
//
// Example: Print(tree, symbols, opts) => PrintResult{css: "a{color:red}", ...}
// Example: Print(tree, symbols, minify_opts) => PrintResult{css: "a{color:red}", ...}
PrintResult Print(const AST& tree, compiler::SymbolMap& symbols, const PrinterOptions& options);

}
