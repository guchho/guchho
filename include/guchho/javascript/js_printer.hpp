#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "guchho/compiler.hpp"
#include "guchho/compat.hpp"
#include "guchho/config.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_renamer.hpp"
#include "guchho/sourcemap.hpp"

namespace guchho::javascript {

    // Describes how a require() or import statement should be rewritten for a
    // given source module. CommonJS files receive a wrapper function reference
    // and an invalid exports placeholder; ES modules receive an init function
    // and a real exports object reference. The is_wrapper_async flag indicates
    // whether the wrapper is an async function (used for top-level await in
    // lazily-initialized ES modules).
    struct RequireOrImportMeta {
        compiler::Ref wrapper_ref;
        compiler::Ref exports_ref;
        bool is_wrapper_async{};
    };

    // All configuration options accepted by the printer. These control how the
    // AST is serialized to JavaScript text, including minification settings,
    // source-map generation, cross-module inlining of TypeScript enums and
    // constants, property mangling, and legal-comment extraction.
    //
    // The require_or_import_meta_for_source callback is invoked once per
    // imported module so the bundler can supply per-file wrapper references.
    struct PrinterOptions {
        std::function<RequireOrImportMeta(uint32_t source_index)> require_or_import_meta_for_source;

        // Cross-module inlining of TypeScript enums is actually done during
        // printing. Each enum is represented as a map from member name to its
        // compile-time value.
        std::unordered_map<compiler::Ref, std::unordered_map<std::string, TSEnumValue>, RefHash> ts_enums;

        // Cross-module inlining of detected inlinable constants is also done
        // during printing. This maps a symbol reference to its constant value.
        std::unordered_map<compiler::Ref, ConstValue, RefHash> const_values;

        // Property mangling results go here. Maps original symbol refs to
        // their shortened names for property access expressions.
        std::unordered_map<compiler::Ref, std::string, RefHash> mangled_props;

        // If the input file had a source map, this pointer allows the printer
        // to map positions all the way back to the original source file(s).
        sourcemap::SourceMapData* input_source_map{};

        // When generating source maps, this table of line-start byte offsets
        // enables binary search to determine which source line corresponds to
        // a given AST node location.
        std::vector<sourcemap::LineOffsetTable> line_offset_tables;

        compiler::Ref to_commonjs_ref;
        compiler::Ref to_esm_ref;
        compiler::Ref runtime_require_ref;
        compat::JSFeature unsupported_features{};
        int indent{};
        int line_limit{};
        config::Format output_format{config::Format::kPreserve};
        bool minify_whitespace{};
        bool minify_identifiers{};
        bool minify_syntax{};
        bool ascii_only{};
        config::LegalComments legal_comments{config::LegalComments::kInline};
        config::SourceMap source_map{config::SourceMap::kNone};
        bool add_source_mappings{};
        bool needs_metafile{};
        bool omit_runtime_for_tests{};
        config::MetafileFormat metafile_format{config::MetafileFormat::kUnminified};
    };

    // The output of printing an AST. Contains the generated JavaScript text,
    // any extracted legal comments that were not inlined, JSON metadata for
    // dynamically-imported modules, and a source-map chunk that encodes
    // character-offset mappings for this particular file.
    struct PrintResult {
        std::string js;
        std::vector<std::string> extracted_legal_comments;
        std::vector<std::string> json_metadata_imports;

        // This source map chunk contains only the VLQ-encoded offsets for the
        // "js" field above. It is not a complete source map. The bundler will
        // merge many chunks together to produce the final source map for the
        // output bundle.
        sourcemap::Chunk source_map_chunk;
    };

    // Determines whether a JavaScript identifier must be escaped (quoted) to
    // remain valid in the target environment. Some names collide with reserved
    // words or contain characters that require escaping in older runtimes.
    //
    // Example:
    //   CanEscapeIdentifier("class", {}) == true   // "class" is a reserved word
    //   CanEscapeIdentifier("foo", {}) == false     // no escaping needed
    //   CanEscapeIdentifier("\x00test", {ES2015}) == true  // NUL byte needs escape
    bool CanEscapeIdentifier(std::string_view name, compat::JSFeature unsupported_features, bool ascii_only);

    // Serializes a complete AST into JavaScript source code, applying the
    // requested minification, source-map, and comment-preservation settings.
    // The symbols table and renamer provide the final identifier names after
    // minification. Returns a PrintResult containing the JS text and all
    // associated metadata.
    //
    // Side effects:
    //   - May mutate the symbol table to record newly-created symbols.
    //   - Populates source-map chunks when source-map generation is enabled.
    //   - Extracts legal comments according to the configured policy.
    PrintResult Print(const AST& tree, compiler::SymbolMap& symbols, Renamer& renamer, const PrinterOptions& options);

}
