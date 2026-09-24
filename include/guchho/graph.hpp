#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/css/css_ast.hpp"
#include "guchho/helpers.hpp"
#include "guchho/html/html_bridge.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/logger.hpp"

namespace guchho::sourcemap {
    struct SourceMapData;
}

namespace guchho::resolver {
    struct SideEffectsData;
}

namespace guchho::graph {

    // ======================================================================
    // Meta section
    //
    // Bookkeeping that the Guchho compile phase needs but the scan phase does
    // not. Keeping it in a dedicated section lets the scan pass stay focused
    // on discovery, while each linking operation attaches its own copy of this
    // metadata to the files it works with.
    // ======================================================================

    // Selects the wrapper style Guchho emits around a module whose evaluation
    // must be deferred until something actually needs it (a conditional
    // require, a dynamic import, and similar lazy boundaries).
    enum class WrapKind : uint8_t {
        // The module needs no wrapper; its statements are emitted inline and
        // run in ordinary source order with the rest of the chunk.
        kNone,

        // CommonJS-style wrapper. The module body becomes a factory registered
        // with "__commonJS", and every consumer invokes the factory to obtain
        // the exports object. The factory runs at most once per bundle:
        //
        //   // foo.js
        //   let require_foo = __commonJS((exports, module) => {
        //     exports.foo = 123;
        //   });
        //
        //   // bar.js
        //   let foo = flag ? require_foo() : null;
        //
        // Input:  flag = true on the first evaluation of the conditional
        // Output: foo receives { foo: 123 }; later calls reuse the cached
        //         exports object without rerunning the body
        kCJS,

        // ESM-style wrapper. The module body becomes a lazy initializer made
        // by "__esm", paired with an exports namespace so live bindings stay
        // accurate even though evaluation is postponed:
        //
        //   // foo.js
        //   var foo, foo_exports = {};
        //   __export(foo_exports, { foo: () => foo });
        //   let init_foo = __esm(() => { foo = 123; });
        //
        //   // bar.js
        //   let foo = flag ? (init_foo(), __toCommonJS(foo_exports)) : null;
        //
        // Input:  flag = true on the first evaluation of the conditional
        // Output: foo receives 123, and foo_exports exposes "foo" through a
        //         getter that keeps returning the current value
        kESM,
    };

    // Everything the linker must know about one imported symbol once Guchho
    // has paired it with the declaration that actually provides it.
    struct ImportData {
        // The chain of intermediate re-export statements the symbol passed
        // through before reaching its final declaration. Both "export * from"
        // and "export {x} from" contribute hops. An "export *" chain can fan
        // out into a diamond when several files independently re-export the
        // same underlying symbol, so a single import may record more than one
        // path:
        //
        // Input:  entry.js star-exports from a.js and b.js; both of those
        //         star-export from c.js, which declares "x"
        // Output: the record for "x" lists two hops (via a.js and via b.js)
        //         that converge on the same declaration in c.js
        std::vector<javascript::Dependency> re_exports{};

        // Position of the imported name in the source, used by diagnostics
        // that should underline the identifier itself. A zero value means no
        // precise position is available and the field should be ignored.
        logger::Loc name_loc{};

        // The symbol this import resolves to; combine with "source_index" to
        // identify the file that declares it.
        compiler::Ref ref;

        // Source index of the file that declares the imported symbol.
        uint32_t source_index{};
    };

    // Everything the linker must know about one exported symbol after export
    // resolution has run.
    struct ExportData {
        // Star-export candidates parked until import matching can decide
        // whether they are ambiguous. Export stars are resolved before imports
        // are matched, so at that earlier point Guchho cannot yet tell whether
        // a duplicated name refers to two different symbols (a real conflict)
        // or to one symbol reached by two routes (harmless). Consider:
        //
        //   // entry.js
        //   export * from './a'
        //   export * from './b'
        //
        //   // a.js
        //   export * from './c'
        //
        //   // b.js
        //   export {x} from './c'
        //
        //   // c.js
        //   export let x = 1, y = 2
        //
        // Input:  entry.js collects star exports from a.js and b.js
        // Output: after import matching, entry.js exposes exactly "x" and "y",
        //         neither ambiguous, because both routes for "x" end at the
        //         same declaration in c.js
        std::vector<ImportData> potentially_ambiguous_export_star_refs{};

        // Symbol backing this export.
        compiler::Ref ref;

        // File that originally declares the exported name. When this record
        // describes a re-export, that file differs from the file owning this
        // object.
        //
        // Position of the exported name in the source; a zero value means no
        // precise position is available and the field should be ignored.
        logger::Loc name_loc{};

        // Source index of the file that owns this export record.
        uint32_t source_index{};
    };

    // Linker-specific metadata attached to a single file discovered during the
    // scan phase. It lives in a separate structure because the data serves one
    // linking operation at a time, and because parallel linking operations may
    // decorate the same underlying file with different metadata.
    struct JSReprMeta {
        // TypeScript-only map of imports that failed to resolve to a runtime
        // value. A miss is legal in TypeScript because the name is usually
        // just a type. Guchho strips unused imports during parsing, which
        // already removes pure type imports, yet some re-export shapes leave
        // the type-versus-value question undecidable:
        //
        //   import {typeOrNotTypeWhoKnows} from 'path';
        //   export {typeOrNotTypeWhoKnows};
        //
        // Enabling the TypeScript "isolatedModules" flag makes the type
        // checker demand an explicit "export type" for such re-exports, which
        // is the recommended discipline for bundlers that compile each file
        // independently without full type checking. Guchho still tolerates the
        // looser form and records the doubtful import here so the linker knows
        // not to treat it as a runtime dependency.
        std::unordered_map<compiler::Ref, bool, javascript::RefHash> is_probably_typescript_type;

        // Imports that have been matched to exports but not yet bound to
        // them. "Binding" means attaching non-local dependencies: every part
        // in the importing file that uses the symbol gains a dependency on the
        // parts in the exporting file that declare it.
        //
        // Matching and binding must remain separate passes because of the
        // probably-a-type bookkeeping above. The export namespace part cannot
        // be generated until matching is finished (type-only names must be
        // left out of that namespace), and binding cannot run until the
        // namespace part exists (that part itself takes part in the dependency
        // edges). This map parks the matched imports between the two passes.
        std::unordered_map<compiler::Ref, ImportData, javascript::RefHash> imports_to_bind;

        // Every name this file exports once resolution completes. Explicit
        // "export" statements are copied from the parsed AST's named export
        // list; star exports arrive from other files once "export * from"
        // statements have been expanded.
        std::unordered_map<std::string, ExportData> resolved_exports;

        // The surviving star export, if any, retained so the names it
        // contributes can still be attributed back to it during later passes.
        std::optional<ExportData> resolved_export_star;

        // Spell-checker support over the export names, letting diagnostics
        // suggest a close match when an import references a name the module
        // does not export (for example "expot" next to "export").
        std::optional<helpers::TypoDetector> resolved_export_typos;

        // The export names code generation is allowed to emit, pre-filtered
        // and sorted. Never walk "resolved_exports" directly: some of its
        // entries are internal and must stay out of the output, and hashing
        // containers iterate in an unstable order. Using this vector keeps
        // builds byte-for-byte reproducible.
        std::vector<std::string> sorted_and_filtered_export_aliases;

        // Part assignments layered on top of the map the parser produced
        // during the scan phase. Always read through "TopLevelSymbolToParts"
        // so both layers are consulted together; never query this overlay on
        // its own.
        std::unordered_map<compiler::Ref, std::vector<uint32_t>, javascript::RefHash> top_level_symbol_to_parts_overlay;

        // Entry-point files only: one spare temporary symbol for each name in
        // "sorted_and_filtered_export_aliases". Guchho uses these scratch
        // symbols to stash copies of CommonJS re-exports when the output
        // format expects ESM.
        std::vector<compiler::Ref> cjs_export_copies;

        // Index of the synthetic part that stands in for this file's CommonJS
        // or ESM wrapper. The part holds no statements; it exists so tree
        // shaking and code splitting can treat the wrapper as a single node.
        // The wrapper itself cannot be stored inside a part because it
        // contains other parts, which the part model cannot express. Files
        // that receive a wrapper own exactly one of these indices.
        compiler::Index32 wrapper_part_index;

        // Index of the synthetic part that anchors entry-point-specific
        // wiring. Anything the entry point must keep -- including otherwise
        // removable parts that nothing else references -- attaches to this
        // part as a dependency so it survives tree shaking. Only
        // entry-point files own one of these indices.
        compiler::Index32 entry_point_part_index;

        // True when this file is affected by top-level await: either it awaits
        // at the top level itself, or it imports (transitively) a file that
        // does. Such modules evaluate asynchronously, so Guchho rejects
        // "require()" calls against them.
        bool is_async_or_has_async_dependency{};

        // Wrapper style chosen for this file; "kNone" means no wrapper.
        WrapKind wrap{WrapKind::kNone};

        // True when the generated code must declare "var exports = {};".
        // Guchho sets it for ESM files whose namespace is captured with
        // "import * as" and for ESM files that are also the target of a
        // "require()" call.
        bool needs_exports_variable{};

        // True when the "__export(exports, { ... })" call must be kept even
        // though no part currently reads "exports"; otherwise the tree shaking
        // pass would delete the call. Entry points set this when the selected
        // output format needs the "exports" variable.
        bool force_include_exports_for_entry_point{};

        // True when the "__export" runtime symbol still has to be imported
        // into the part at "ns_export_part_index". Doing it inside
        // "createExportsForFile" would race on shared maps, so the import is
        // deferred to a later serialized step.
        bool needs_export_symbol_from_runtime{};

        // Wrapped files must wrap their dependencies as well. This flag marks
        // whether that propagation already happened for this file, letting the
        // fixed-point traversal know when it can stop.
        bool did_wrap_dependencies{};
    };

    // ======================================================================
    // Input section
    //
    // Data that flows from the scan phase into the compile phase. The single
    // exception is JSReprMeta, which is compile-only; it rides along with the
    // JavaScript representation for convenience and to avoid an extra layer of
    // indirection, while living in its own type so responsibilities stay
    // separated.
    // ======================================================================

    // How Guchho classified a file's side effects. Tree shaking consults this
    // verdict: files proven effect-free can be dropped entirely once nothing
    // references their exports.
    enum class SideEffectsKind : uint8_t {
        // Conservative starting point -- assume evaluating the file can be
        // observed, so it is retained whenever it is reachable.
        kHasSideEffects,

        // A "package.json" in a containing directory listed this file in its
        // "sideEffects" field as effect-free.
        kNoSideEffectsPackageJSON,

        // Parsing produced an empty AST, so running the file cannot do
        // anything observable. Declaration-only files land here.
        kNoSideEffectsEmptyAST,

        // The file arrived through a data-oriented loader (the "text" loader,
        // for example) whose result is inert by construction.
        kNoSideEffectsPureData,

        // Same conclusion as pure data, except a plugin produced the bytes.
        // Guchho suppresses unused-import warnings for these files: executing
        // the plugin is itself observable, so deleting the import would change
        // program behavior.
        kNoSideEffectsPureDataFromPlugin,
    };

    // The side-effect verdict for one file, plus optional provenance Guchho
    // can quote when explaining the verdict in a diagnostic.
    struct SideEffects {
        // Supporting detail for error messages (which "package.json" rule
        // applied, which loader ran, ...). Shared because many files may cite
        // the same explanation.
        std::shared_ptr<resolver::SideEffectsData> data;

        SideEffectsKind kind{SideEffectsKind::kHasSideEffects};
    };

    // An auxiliary artifact written to the output directory next to the main
    // bundle; produced by loaders such as "file" and "copy".
    struct OutputFile {
        // Partial JSON describing this artifact. Guchho concatenates every
        // file's chunk into the complete metadata document once all outputs
        // are known.
        std::string json_metadata_chunk;

        std::string abs_path;
        std::vector<uint8_t> contents;
        bool is_executable{};
    };

    struct JSRepr;
    struct CSSRepr;
    struct HTMLRepr;
    struct CopyRepr;

    // Tagged union naming the kind of content a scanned file holds. Each
    // variant sits behind a shared pointer so cloning an input file (which the
    // linker does) copies only the pointer; CloneLinkerGraph then installs
    // private clones, leaving the original scan-phase data immutable and safe
    // to share across parallel link operations.
    using InputFileRepr = std::variant<
        std::monostate,
        std::shared_ptr<JSRepr>,
        std::shared_ptr<CSSRepr>,
        std::shared_ptr<HTMLRepr>,
        std::shared_ptr<CopyRepr>>;

    // One file as handed from the scan phase to the linker, covering
    // everything known about it before content-specific details.
    struct InputFile {
        InputFileRepr repr;
        std::shared_ptr<sourcemap::SourceMapData> input_source_map;

        // Extra files to emit alongside the bundle if this file makes it into
        // the output; populated by the "file" and "copy" loaders.
        std::vector<OutputFile> additional_files;

        // Discriminator that keeps "additional_files" destinations unique
        // across the whole build, so two loaders can never claim the same
        // output path.
        std::string unique_key_for_additional_file;

        SideEffects side_effects;
        logger::Source source;
        config::Loader loader{config::Loader::kNone};

        // When true, this file is left out of source maps and the metadata
        // file, hiding generated or auxiliary content from debug tooling.
        bool omit_from_source_maps_and_metafile{};
    };

    // Content-specific payload for a JavaScript module: its parsed AST plus
    // the compile-phase metadata described above.
    struct JSRepr {
        JSReprMeta meta{};
        javascript::AST ast;

        // When set, identifies the CSS file this JavaScript stub stands in
        // for. Guchho synthesizes a stub whenever a JavaScript file imports a
        // CSS file, giving the module graph a JavaScript handle on the
        // stylesheet.
        compiler::Index32 css_source_index{};

        // Read-only access to the import records parsed from this module.
        const std::vector<compiler::ImportRecord>& ImportRecords() const {
            return ast.import_records;
        }

        // Mutable access to the import records; linker passes rewrite them in
        // place (rewriting paths to their final output locations, for
        // example).
        std::vector<compiler::ImportRecord>& ImportRecords() {
            return ast.import_records;
        }

        // Returns which parts of this file declare the given top-level
        // symbol, consulting both the parser's map and the compile-phase
        // overlay so callers see the complete picture.
        //
        // Input:  ref for a helper function declared in part 2 of this file
        // Output: the part indices carrying that declaration (for example
        //         { 2 }), which must be linked wherever the helper is used
        const std::vector<uint32_t>& TopLevelSymbolToParts(compiler::Ref ref) const;
    };

    // Content-specific payload for a stylesheet.
    struct CSSRepr {
        css::AST ast;

        // When set, identifies the JavaScript stub paired with this CSS file.
        // The stub is generated when JavaScript imports the stylesheet, giving
        // the module system a JavaScript-side handle on the CSS content.
        compiler::Index32 js_source_index{};

        // Read-only access to the stylesheet's import records (URLs found in
        // "@import" and "url()" references).
        const std::vector<compiler::ImportRecord>& ImportRecords() const {
            return ast.import_records;
        }

        // Mutable access to the stylesheet's import records.
        std::vector<compiler::ImportRecord>& ImportRecords() {
            return ast.import_records;
        }
    };

    // Payload for a file copied into the output byte-for-byte -- images,
    // fonts, and similar assets. Its contents never join the module graph;
    // only the replacement URL does.
    struct CopyRepr {
        // URL substituted into every import record that referenced this file,
        // so generated code points at the asset's final resting place.
        std::string url_for_code;

        // Copy files carry no import records, so both overloads answer with a
        // null pointer rather than an empty vector. Callers that branch on
        // null keep behaving exactly as before.
        const std::vector<compiler::ImportRecord>* ImportRecords() const { return nullptr; }
        std::vector<compiler::ImportRecord>* ImportRecords() { return nullptr; }
    };

    // A maximal stretch of consecutive inline <script> or <style> elements
    // with no external resource of the same kind interrupting the stretch.
    // Guchho folds the whole run into one virtual entry, emits a single chunk
    // for it, re-inlines that chunk at the position of the run's first
    // element, and empties the remaining elements.
    struct HtmlInlineSegment {
        // The inline elements that form this run, in document order. The
        // pointers target the scan-phase AST that the HTML output pass
        // serializes, so they stay valid for the length of that pass.
        std::vector<const html::Node*> members;

        // Source index of the virtual JS/CSS entry that received this run's
        // concatenated text; UINT32_MAX when the run was empty and therefore
        // produced no chunk.
        uint32_t chunk_source_index = UINT32_MAX;
    };

    // The virtual entry points carved out of one HTML file's inline script
    // and style content. Scripts and styles are partitioned into separate runs
    // so Guchho concatenates neighboring blocks only when that merge is safe:
    // the blocks must share the same classic script or style scope with no
    // external resource boundary sitting between them.
    struct HtmlInlineInfo {
        std::vector<HtmlInlineSegment> js_segments;
        std::vector<HtmlInlineSegment> css_segments;
    };

    // Content-specific payload for an HTML document.
    struct HTMLRepr {
        // The parsed HTML tree together with the import records the bridge
        // extracted from resource-bearing elements (<script src>, <link href>,
        // <img src>, ...). The tree supports deep copying so the linker can
        // clone it per graph just like the JavaScript and CSS payloads.
        html::AST ast;

        // Read-only access to the document's import records.
        const std::vector<compiler::ImportRecord>& ImportRecords() const {
            return ast.import_records;
        }

        // Mutable access to the document's import records.
        std::vector<compiler::ImportRecord>& ImportRecords() {
            return ast.import_records;
        }
    };

    // Variant dispatch helper standing in for a virtual ImportRecords method:
    // it returns the address of the import record vector held by the active
    // variant, or nullptr when the variant carries none (CopyRepr or an empty
    // monostate). The null-versus-empty distinction and the order of the
    // checks match what callers expect when they branch on a null result.
    //
    // Input:  a repr currently holding a shared_ptr<JSRepr> with two records
    // Output: pointer to that vector of two import records
    //
    // Input:  a repr currently holding a shared_ptr<CopyRepr>
    // Output: nullptr
    inline std::vector<compiler::ImportRecord>* GetImportRecords(InputFileRepr& repr) {
        if (auto* js = std::get_if<std::shared_ptr<JSRepr>>(&repr); js && *js) return &(*js)->ast.import_records;
        if (auto* css = std::get_if<std::shared_ptr<CSSRepr>>(&repr); css && *css) return &(*css)->ast.import_records;
        if (auto* html = std::get_if<std::shared_ptr<HTMLRepr>>(&repr); html && *html) return &(*html)->ast.import_records;
        return nullptr;
    }
    inline const std::vector<compiler::ImportRecord>* GetImportRecords(const InputFileRepr& repr) {
        if (auto* js = std::get_if<std::shared_ptr<JSRepr>>(&repr); js && *js) return &(*js)->ast.import_records;
        if (auto* css = std::get_if<std::shared_ptr<CSSRepr>>(&repr); css && *css) return &(*css)->ast.import_records;
        if (auto* html = std::get_if<std::shared_ptr<HTMLRepr>>(&repr); html && *html) return &(*html)->ast.import_records;
        return nullptr;
    }

    // ======================================================================
    // Linker graph section
    //
    // The working set of files for one linker operation. Guchho builds a
    // single graph when code splitting is enabled, and a separate graph per
    // entry point when code splitting is disabled.
    // ======================================================================

    // Whether a file acts as an entry point, and how it became one.
    enum class EntryPointKind : uint8_t {
        // Ordinary file; not an entry point.
        kNone,
        // Named directly by the user on the command line or in configuration.
        kUserSpecified,
        // Reached through a dynamic import(), which establishes a chunk
        // boundary of its own.
        kDynamicImport,
    };

    // One file as the linker sees it: the shared scan-phase results plus the
    // per-link state such as chunk membership, distance from an entry point,
    // and tree shaking liveness.
    struct LinkerFile {
        // The set of entry points that can reach this file. Part-to-chunk
        // assignment for this file is driven by this bit set.
        helpers::BitSet entry_bits;

        // Resolves byte offsets to line/column pairs, but only for files that
        // actually log warnings. Created on first use because warnings should
        // be uncommon, keeping the common case allocation-free.
        std::unique_ptr<logger::LineColumnTracker> lazy_line_column_tracker;

        InputFile input_file;

        // Shortest number of module edges any entry point must traverse to
        // arrive at this file; useful for ordering and for diagnostics.
        uint32_t distance_from_entry_point{};

        // When "entry_point_kind" differs from "kNone", the index of the chunk
        // that represents this entry point.
        uint32_t entry_point_chunk_index{};

        // This file is an entry point exactly when this value differs from
        // "kNone". Note that a dynamically-imported file may also be listed by
        // the user, in which case it reports "kUserSpecified" instead of
        // "kDynamicImport".
        EntryPointKind entry_point_kind{EntryPointKind::kNone};

        // True once the tree shaking algorithm has marked this file as live,
        // meaning its code can still be observed and must be emitted.
        bool is_live{};

        // True when this file is an entry point of any kind.
        bool IsEntryPoint() const {
            return entry_point_kind != EntryPointKind::kNone;
        }

        // True only when the user named this file as an entry point
        // explicitly.
        bool IsUserSpecifiedEntryPoint() const {
            return entry_point_kind == EntryPointKind::kUserSpecified;
        }

        // Returns the lazily-created position tracker for this file,
        // constructing it on first use. Deliberately not guarded by a mutex:
        // call it only from code paths that are not running in parallel with
        // other work on the same file.
        //
        // Input:  first request for a file whose warning points at line 3
        // Output: a tracker ready to translate that file's offsets into
        //         line/column positions
        logger::LineColumnTracker& LineColumnTracker();
    };

    // One entry point registered with the linker.
    struct EntryPoint {
        // Where this entry point's chunk lands on disk. The value may be
        // absolute or relative: an absolute path is first rebased to be
        // relative to the "outbase" directory, then joined onto "outdir" to
        // produce the final output path.
        std::string output_path;

        // Source index of the entry point file. That file must carry a valid
        // entry point kind -- anything except "none".
        uint32_t source_index{};

        // True when the user supplied "output_path" by hand. Manually written
        // paths are excluded while Guchho computes the default "outbase"
        // directory, which otherwise becomes the lowest common ancestor of
        // every automatically generated output path.
        bool output_path_was_auto_generated{};
    };

    struct LinkerGraph;

    // Produces a private LinkerGraph for one linking operation. It copies the
    // reachable slice of "input_files" (listed by "reachable_files"),
    // duplicates the shared content payloads so this graph can mutate without
    // disturbing the scan-phase originals, and installs the entry point list.
    // Guchho calls it once per entry point when code splitting is off, and
    // once for the entire build when code splitting is on.
    //
    // Input:  three scanned files, reachable_files = {0, 2}, one entry point,
    //         code_splitting = false
    // Output: a LinkerGraph holding LinkerFile entries for files 0 and 2 with
    //         freshly cloned JS/CSS/HTML payloads and one registered entry
    //         point
    LinkerGraph CloneLinkerGraph(
        const std::vector<InputFile>& input_files,
        const std::vector<uint32_t>& reachable_files,
        const std::vector<EntryPoint>& original_entry_points,
        bool code_splitting);

    // The per-operation workspace the linker manipulates: reachable files, the
    // symbol table, cross-module constant tables, and the helpers that extend
    // the graph while linking proceeds.
    struct LinkerGraph {
        // Every file registered with this linking operation.
        std::vector<LinkerFile> files;

        // The symbol table; every compiler::Ref in the build resolves through
        // this map.
        compiler::SymbolMap symbols;

        // TypeScript enum members grouped by the module that declares them,
        // enabling cross-module inlining of enum constants.
        std::unordered_map<compiler::Ref, std::unordered_map<std::string, javascript::TSEnumValue>, javascript::RefHash> ts_enums;

        // Literal values detected as safe to inline across module boundaries
        // (numbers, strings, and booleans that never change).
        std::unordered_map<compiler::Ref, javascript::ConstValue, javascript::RefHash> const_values;

        // Every file reachable from any entry point. Linking deliberately
        // avoids scanning the whole "files" vector: a large build may need
        // only a handful of files for a given operation, which is the norm in
        // incremental compilation. Iterate this vector instead. It is sorted
        // into a stable order because raw source indices are assigned
        // unpredictably.
        std::vector<uint32_t> reachable_files;

        // Maps each unstable source index to its position inside
        // "reachable_files". Use it as a deterministic sort key whenever
        // something must be ordered by source index -- symbol references such
        // as compiler::Ref, for instance.
        std::vector<uint32_t> stable_source_indices;

        // Read-only view of the entry points, so packages building on Guchho
        // cannot add or remove entry points after the graph exists.
        const std::vector<EntryPoint>& EntryPoints() const {
            return entry_points_;
        }

        // Appends "part" to the file identified by "source_index" and reports
        // where it landed.
        //
        // Input:  source_index = 4, part = a function body with two statements
        //         (file 4 already contains parts 0 through 6)
        // Output: 7 -- the index the new part now occupies in file 4
        uint32_t AddPartToFile(uint32_t source_index, javascript::Part part);

        // Mints a brand-new symbol owned by "source_index" and hands back a
        // reference the rest of the link can import and use.
        //
        // Input:  source_index = 2, kind = kImport, original_name = "myHelper"
        // Output: a fresh compiler::Ref bound to the name "myHelper" inside
        //         file 2, ready to be imported elsewhere
        compiler::Ref GenerateNewSymbol(uint32_t source_index, compiler::SymbolKind kind, std::string original_name);

        // Notes that the part at ("source_index", "part_index") consumes "ref"
        // exactly "use_count" times, importing it from
        // "source_index_to_import_from" when that differs from the using file.
        // The accumulated counts feed tree shaking and chunk dependency
        // tracking.
        //
        // Input:  file 5, part 0, a ref declared in file 2, use_count = 3
        // Output: file 5's part 0 gains a dependency on the file-2 declaration
        //         carrying a weight of 3 uses
        void GenerateSymbolImportAndUse(
            uint32_t source_index,
            uint32_t part_index,
            compiler::Ref ref,
            uint32_t use_count,
            uint32_t source_index_to_import_from);

        // The same bookkeeping as GenerateSymbolImportAndUse, but for helpers
        // that live in Guchho's runtime library: the helper is looked up by
        // name rather than by an existing symbol reference.
        //
        // Input:  file 6, part 1, name = "__toESM", use_count = 2
        // Output: part 1 records a runtime import of "__toESM" with a weight
        //         of 2 uses
        void GenerateRuntimeSymbolImportAndUse(
            uint32_t source_index,
            uint32_t part_index,
            std::string_view name,
            uint32_t use_count);

    private:
        // CloneLinkerGraph is allowed to fill the private entry point list
        // directly while constructing the graph.
        friend LinkerGraph CloneLinkerGraph(
            const std::vector<InputFile>& input_files,
            const std::vector<uint32_t>& reachable_files,
            const std::vector<EntryPoint>& original_entry_points,
            bool code_splitting);

        std::vector<EntryPoint> entry_points_;
    };

}
