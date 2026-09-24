#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "guchho/graph.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/helpers.hpp"
#include "guchho/compiler.hpp"
#include "guchho/resolver.hpp"
#include "guchho/sourcemap.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_printer.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_printer.hpp"
#include "guchho/javascript/js_renamer.hpp"


namespace guchho::bundler {
    struct DataForSourceMap;
}

namespace guchho::linker {

    // Merges consecutive local-variable declarations into a single declaration
    // list so the printed output is as short as possible. Neighboring "var"
    // statements that all declare fresh locals can share one comma-separated
    // list without changing what the code does. It is declared here because
    // both the chunk-construction pass and the JS emission pass (separate
    // translation units) call it; the implementation lives in linker_chunk.cpp.
    //
    // Example:
    //   input : var a = 1;
    //           var b = a + 1;
    //   output: var a = 1, b = a + 1;
    std::vector<javascript::Stmt> MergeAdjacentLocalStmts(std::vector<javascript::Stmt>& stmts);

    ////////////////////////////////////////////////////////////////////////////////
    // Output pieces
    //
    // A link run produces a flat, ordered list of output pieces. Each piece is
    // either a chunk the linker generated (JavaScript, CSS, source maps, legal
    // comment files) or a plain file copied from the input (assets such as
    // images and fonts). Keeping everything as a piece list lets the printer
    // stream content out of a shared joiner, so pieces become available as soon
    // as their text is ready rather than being buffered wholesale.

    enum class OutputPieceIndexKind : uint8_t {
        kNone,
        kAssetIndex,
        kChunkIndex,
    };

    struct OutputPiece {
        std::vector<uint8_t> data;
        uint32_t index{};
        OutputPieceIndexKind kind{OutputPieceIndexKind::kNone};
    };

    struct IntermediateOutput {
        std::vector<OutputPiece> pieces;
        helpers::Joiner joiner;
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Cross-chunk dependencies
    //
    // When code splitting is enabled, one chunk can depend on exports produced
    // by another chunk. These types describe how chunks refer to each other
    // and how a chunk forwards names so imports resolve to the right producer.
    // A "stable" reference is keyed by a source index that survives hashing
    // and reordering, which is what the final deterministic output order is
    // based on.

    struct ChunkImport {
        uint32_t chunk_index{};
        compiler::ImportKind import_kind{};
    };

    struct CrossChunkImportItem {
        std::string export_alias{};
        compiler::Ref ref;
    };

    using CrossChunkImportItemArray = std::vector<CrossChunkImportItem>;

    struct CrossChunkImport {
        CrossChunkImportItemArray sorted_import_items;
        uint32_t chunk_index{};
    };

    using CrossChunkImportArray = std::vector<CrossChunkImport>;

    struct StableRef {
        uint32_t stable_source_index{};
        compiler::Ref ref;
    };

    using StableRefArray = std::vector<StableRef>;

    ////////////////////////////////////////////////////////////////////////////////
    // Matching imports to exports
    //
    // For every import statement in a file, the linker must find the exporting
    // source-file entry that provides the requested name. These types describe
    // one such lookup: what kind of match was found (a normal export, a
    // namespace export, a re-export cycle, a type-only name, ...), where the
    // name lives, and whether two different modules both claim it (reported as
    // an ambiguous match). A tracker also records what has already been
    // visited so cyclic imports terminate instead of recursing forever.

    enum class MatchImportKind : uint8_t {
        kIgnore,
        kNormal,
        kNamespace,
        kNormalAndNamespace,
        kCycle,
        kProbablyTypeScriptType,
        kAmbiguous,
    };

    struct MatchImportResult {
        std::string alias{};
        MatchImportKind kind{MatchImportKind::kIgnore};
        compiler::Ref namespace_ref{};
        uint32_t source_index{};
        logger::Loc name_loc{};
        uint32_t other_source_index{};
        logger::Loc other_name_loc{};
        compiler::Ref ref{};
    };

    struct ImportTracker {
        uint32_t source_index{};
        logger::Loc name_loc{};
        compiler::Ref import_ref{};
    };

    enum class ImportStatus : uint8_t {
        kNoMatch,
        kFound,
        kCommonJS,
        kDynamicFallback,
        kCommonJSWithoutExports,
        kDisabled,
        kExternal,
        kProbablyTypeScriptType,
    };

    ////////////////////////////////////////////////////////////////////////////////
    // CSS import ordering
    //
    // Stylesheets are combined in the order their @import rules are reached.
    // An entry can come from the source tree (identified by source index),
    // from a URL the build treats as external, or from @layer resolution.
    // Preserving the visit order is what keeps the resulting cascade correct,
    // so these types record both the condition chain guarding an import and
    // the records that produced it.

    enum class CssImportKind : uint8_t {
        kNone,
        kSourceIndex,
        kExternalPath,
        kLayers,
    };

    struct CssImportOrder {
        std::vector<css::ImportConditions> conditions;
        std::vector<compiler::ImportRecord> condition_import_records;

        std::vector<std::vector<std::string>> layers{};
        logger::Path external_path{};
        uint32_t source_index{};
        CssImportKind kind{CssImportKind::kNone};
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Deterministic chunk ordering
    //
    // Once reachable parts are assigned to chunks, the chunks themselves need
    // a deterministic order. Each chunk records how far it sits from its entry
    // point plus a tie-breaker, so unrelated spans still resolve to the same
    // reproducible sequence from one build to the next.

    struct ChunkOrder {
        uint32_t source_index{};
        uint32_t distance{};
        uint32_t tie_breaker{};
    };

    using ChunkOrderArray = std::vector<ChunkOrder>;

    ////////////////////////////////////////////////////////////////////////////////
    // Part ranges
    //
    // Each file is split into small units called parts, one per group of
    // top-level statements, so unused code can be dropped individually. A part
    // range names a contiguous window [begin, end) of parts inside a single
    // source file, which is how the linker says which slice of a file belongs
    // in a given chunk.

    struct PartRange {
        uint32_t source_index{};
        uint32_t part_index_begin{};
        uint32_t part_index_end{};
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Statement and code generation
    //
    // A statement list records how the code of one file is divided for output.
    // Some statements must be wrapped by the chunk's module factory, so they
    // are kept separately as a prefix or a suffix that lands inside or outside
    // that wrapper. When a file is converted to a different module format,
    // setter captures remember which namespace objects receive the lazily
    // assigned exports.

    struct StmtList {
        std::vector<javascript::Stmt> inside_wrapper_prefix;
        std::vector<javascript::Stmt> inside_wrapper_suffix;
        std::vector<javascript::Stmt> outside_wrapper_prefix;
    };

    struct SystemJSSetterCapture {
        std::string import_path;
        compiler::Ref namespace_ref;
    };

    struct CompileResultJS {
        javascript::PrintResult print_result;
        sourcemap::LineColumnOffset generated_offset{};
        uint32_t source_index{};
        std::vector<SystemJSSetterCapture> system_setter_captures;
    };

    struct CompileResultCSS {
        css::PrintResult print_result;
        sourcemap::LineColumnOffset generated_offset{};
        compiler::Index32 source_index;
        bool has_charset{};
    };

    struct LegalCommentEntry {
        uint32_t source_index{};
        std::vector<std::string> extracted_comments;
    };

    struct CompileResultForSourceMap {
        sourcemap::Chunk source_map_chunk{};
        sourcemap::LineColumnOffset generated_offset{};
        uint32_t source_index{};
        bool is_null_entry{};
    };

    ////////////////////////////////////////////////////////////////////////////////
    // External CSS imports
    //
    // A stylesheet referenced from CSS that must be left as a remote URL, for
    // example an absolute address beginning with a protocol. The path and the
    // import conditions guarding it are retained so the URL can be re-emitted
    // exactly where it was imported.

    struct ExternalImportCSS {
        logger::Path path;
        std::vector<css::ImportConditions> conditions;
        std::vector<compiler::ImportRecord> condition_import_records;
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Chunk representations
    //
    // A chunk carries either JavaScript or CSS output. The JavaScript form
    // lists the files and part ranges assigned to the chunk, the exports it
    // offers to other chunks, and the imports it pulls from them. Its
    // cross-chunk prefix and suffix hold statements that must run before and
    // after the chunk body itself, such as re-export getters. The CSS form is
    // simply the ordered list of imports whose styles make up the chunk.

    struct ChunkReprJS {
        std::vector<uint32_t> files_in_chunk_in_order;
        std::vector<PartRange> parts_in_chunk_in_order;

        std::unordered_map<compiler::Ref, std::string, javascript::RefHash> exports_to_other_chunks;
        std::unordered_map<uint32_t, CrossChunkImportItemArray> imports_from_other_chunks;
        std::vector<javascript::Stmt> cross_chunk_prefix_stmts;
        std::vector<javascript::Stmt> cross_chunk_suffix_stmts;

        uint32_t css_chunk_index{};
        bool has_css_chunk{};
    };

    struct ChunkReprCSS {
        std::vector<CssImportOrder> imports_in_chunk_in_order;
    };

    using ChunkReprVariant = std::variant<ChunkReprJS, ChunkReprCSS>;

    ////////////////////////////////////////////////////////////////////////////////
    // In-progress chunk state
    //
    // Everything the linker tracks about one output chunk while it is being
    // built: a stable unique key used for hashing, the parts that landed in
    // it, the imports it forwards to other chunks, the path template and final
    // relative path used when writing, and lazy callbacks that produce the
    // content hash only after the chunk's contents are final. Callbacks are
    // used rather than eager hashing so nothing is hashed until all content
    // that could affect it has actually been produced.

    struct ChunkInfo {
        std::string unique_key;

        std::unordered_map<uint32_t, bool> files_with_parts_in_chunk;
        helpers::BitSet entry_bits;

        std::vector<ChunkImport> cross_chunk_imports;

        ChunkReprVariant chunk_repr;

        std::vector<config::PathTemplate> final_template;
        std::string final_rel_path;

        std::vector<uint8_t> external_legal_comments;

        std::function<std::vector<uint8_t>()> wait_for_isolated_hash;
        std::function<helpers::Joiner(int)> json_metadata_chunk_callback;

        sourcemap::SourceMapPieces output_source_map;
        IntermediateOutput intermediate_output;

        uint32_t entry_point_bit{};
        uint32_t source_index{};
        bool is_entry_point{};
        bool is_executable{};
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Chunk metadata for downstream passes
    //
    // A compact, snapshot-style description of one output chunk, meant for
    // consumers that run after linking, most importantly the HTML output pass.
    // Unlike the mutable ChunkInfo used while linking, this form is stable
    // once produced, so it can be indexed and queried after the build graph is
    // gone. The HTML pass uses it to add module preload links for statically
    // imported chunks, to pair a CSS output with the JavaScript chunk it
    // belongs to, and to decide whether an entry chunk really emits statements
    // or is only a forwarding stub that can be omitted.
    struct ChunkMetadata {
        // Which source file this chunk was generated from. Split and shared
        // chunks report the source index of their facade file; chunks that are
        // not attached to any source (such as the runtime-only chunk) report
        // UINT32_MAX.
        uint32_t source_index = UINT32_MAX;

        // Where the chunk is written, relative to the output directory. This
        // may embed a content hash of the chunk's bytes.
        std::string final_rel_path;

        // Whether this chunk is itself an entry point of the build.
        bool is_entry_point{};

        // Whether this chunk produces runtime statements. A chunk that only
        // forwards other chunks (a pure re-export facade) is not executable,
        // so the HTML pass can drop it instead of emitting a stub page.
        bool is_executable{};

        // Indices (into LinkResult::chunk_metadata) of the chunks this chunk
        // statically imports; used to emit module preload links.
        std::vector<uint32_t> cross_chunk_imports;

        // For a JavaScript chunk that carries an associated stylesheet, the
        // final relative path of that CSS chunk, so the HTML pass can inject a
        // matching <link>. Empty when the chunk has no linked CSS.
        std::string associated_css_rel_path;
    };

    // The complete result of calling Link: every file that must be written to
    // the output directory, plus the chunk metadata that later passes (such as
    // the HTML output pipeline) consume.
    struct LinkResult {
        // The files to write: generated chunks, their source maps, any legal
        // comment files, and assets copied straight through from the input.
        // Deterministically ordered so repeated builds produce identical
        // output.
        std::vector<graph::OutputFile> output_files;

        // One ChunkMetadata entry per chunk, in chunk-index order. Empty when
        // the build produced no chunks.
        std::vector<ChunkMetadata> chunk_metadata;
    };

    // Holds the full state of one link run: the resolved input graph, the set
    // of chunks being assembled, and every transient helper (symbol renamer,
    // mangling caches, legal-comment lists) needed between phases. A context
    // is created by Link, driven through the pipeline — tree shaking, import
    // and export matching, chunk construction, code generation — and then
    // discarded when the run finishes.
    struct LinkerContext {
        config::Options* options{};
        helpers::Timer* timer{};
        logger::Log log;
        filesystem::Fs* fs{};
        resolver::Resolver* res{};
        graph::LinkerGraph graph;
        std::vector<ChunkInfo> chunks;
        std::vector<ImportTracker> cycle_detector;
        std::function<std::vector<bundler::DataForSourceMap>()> data_for_source_maps;
        std::string unique_key_prefix;
        std::vector<uint8_t> unique_key_prefix_bytes;
        std::unordered_map<compiler::Ref, std::string, javascript::RefHash> mangled_props;
        compiler::Ref unbound_module_ref{compiler::kInvalidRef};
        // Reference to the module-namespace object the runtime creates for a
        // transformed module. Lazy exports are assigned onto this object when
        // the module format calls for it.
        compiler::Ref unbound_exports_ref{compiler::kInvalidRef};
        compiler::Ref cjs_runtime_ref{compiler::kInvalidRef};
        compiler::Ref esm_runtime_ref{compiler::kInvalidRef};
        // Source indices of CSS files written as independent output files, for
        // example stylesheets loaded directly by an HTML page. Such
        // stylesheets must not be absorbed into the CSS chunk that a
        // JavaScript entry would normally drag along.
        std::unordered_set<uint32_t> separate_css_entry_sources;

        // --- Tree shaking and code splitting ---
        // Walks the reachable parts from every entry point, marks the parts
        // that are actually used, and layers the code-splitting structure on
        // top: a part reachable from several entries is placed in a shared
        // chunk instead of being duplicated into each one.
        void MarkFileLiveForTreeShaking(uint32_t source_index);
        void MarkPartLiveForTreeShaking(uint32_t source_index, uint32_t part_index);
        void MarkFileReachableForCodeSplitting(uint32_t source_index, uint32_t entry_point_bit, uint32_t distance);
        bool IsExternalDynamicImport(const compiler::ImportRecord& record, uint32_t source_index);
        // Runs the complete reachability and chunking pass: decides which
        // parts survive (tree shaking) and which chunk each surviving part is
        // assigned to (code splitting). When it returns, the chunk list holds
        // the parts and entry-point bits that every later phase filters on.
        void TreeShakingAndCodeSplitting();
        // Resolves each surviving file's imports against the exports declared
        // by the files it depends on, populating import trackers and
        // assignment structures. Import cycles are detected here rather than
        // during code generation.
        void ScanImportsAndExports();
        // Chooses the final set of chunks and their order: groups the scanned
        // parts into entry, shared, and CSS chunks based on entry-point bit
        // masks, then gives each group a deterministic ordering key.
        void ComputeChunks();
        // Once the chunks are fixed, works out which chunk imports what from
        // which other chunk, and precomputes the statements a chunk must emit
        // to obtain those dependencies.
        void ComputeCrossChunkDependencies();
        bool ShouldIncludePart(graph::JSRepr& repr, const javascript::Part& part);
        void FindImportedPartsInJSOrder(ChunkInfo& chunk, std::vector<uint32_t>& js_files, std::vector<PartRange>& js_parts);
        bool ShouldRemoveImportExportStmt(uint32_t source_index, StmtList& stmt_list, logger::Loc loc, compiler::Ref namespace_ref, uint32_t import_record_index, std::vector<SystemJSSetterCapture>* system_setter_captures);
        void ConvertStmtsForChunk(uint32_t source_index, StmtList& stmt_list, std::vector<javascript::Stmt>& part_stmts, std::vector<SystemJSSetterCapture>* system_setter_captures);
        // Emits the JavaScript body of one chunk: turns each file's parts into
        // statements, wraps them in a module factory, appends the entry-point
        // tail (side-effect setup and export forwarding), and pushes the result
        // through the printer and symbol renamer.
        void GenerateChunkJS(int chunk_index);
        void GenerateChunkCSS(int chunk_index);
        // Generates all chunk bodies concurrently and then assembles the
        // ordered list of files to write. Any extra files gathered earlier
        // (for example assets discovered during scanning) are appended after
        // the generated chunks.
        //
        // Example:
        //   input : a resolved graph whose chunks and parts are already
        //           assigned, plus one copied asset
        //   output: a LinkResult whose output_files hold the entry chunk, the
        //           shared chunk, their source maps, and the asset, with a
        //           ChunkMetadata entry per chunk
        LinkResult GenerateChunksInParallel(std::vector<graph::OutputFile> additional_files);
        std::string PathBetweenChunks(const std::string& from_rel_dir, const std::string& to_rel_path);
        void EnforceNoCyclicChunkImports();
        IntermediateOutput BreakJoinerIntoPieces(helpers::Joiner j);
        IntermediateOutput BreakOutputIntoPieces(std::string_view output);
        std::pair<helpers::Joiner, std::vector<sourcemap::SourceMapShift>> SubstituteFinalPaths(const IntermediateOutput& intermediate, std::function<std::string(std::string)> modify_path);
        void AppendIsolatedHashesForImportedChunks(helpers::Xxh64& hash, uint32_t chunk_index, std::vector<uint32_t>& visited, uint32_t visited_key);
        javascript::RequireOrImportMeta RequireOrImportMetaForSource(uint32_t source_index);
        // Builds the symbol renamer for one chunk. Private identifiers inside
        // the chunk are shortened while exported names are left untouched; the
        // renamer is scoped to the chunk, so the same short name can be reused
        // freely in another chunk.
        //
        // Example:
        //   input : files_in_order = { "a.js", "b.js" }, both using a local
        //           named "longIdentifier"
        //   output: a renamer that maps "longIdentifier" to a short name such
        //           as "a" for the duration of this chunk
        std::unique_ptr<javascript::Renamer> RenameSymbolsInChunk(ChunkInfo& chunk, std::vector<uint32_t>& files_in_order);
        std::string GenerateGlobalNamePrefix();
        static std::string SanitizeGlobalName(const std::string& id);
        // Emits the statements for one file's part range. The file is adapted
        // to the chunk's module format (wrapping the appropriate format,
        // injecting runtime require calls), and the printed result is returned
        // together with the generated source-map offset so later shifts can be
        // tracked accurately.
        CompileResultJS GenerateCodeForFileInChunkJS(
            javascript::Renamer& renamer,
            PartRange part_range,
            compiler::Ref to_commonjs_ref,
            compiler::Ref to_esm_ref,
            compiler::Ref runtime_require_ref,
            const std::vector<bundler::DataForSourceMap>& data_for_source_maps);
        // Emits the trailing code that runs once a chunk's entry file has
        // finished loading: it performs any deferred side-effect calls and
        // binds the entry's exports onto the runtime namespace.
        CompileResultJS GenerateEntryPointTailJS(
            javascript::Renamer& renamer,
            compiler::Ref to_commonjs_ref,
            compiler::Ref to_esm_ref,
            uint32_t source_index);

        void MaybeAppendLegalComments(
            config::LegalComments legal_comments,
            const std::vector<LegalCommentEntry>& legal_comment_list,
            ChunkInfo& chunk,
            helpers::Joiner& j,
            std::string_view slash_tag);
        void GenerateIsolatedHashInParallel(ChunkInfo& chunk);
        sourcemap::SourceMapPieces GenerateSourceMapForChunk(
            const std::vector<CompileResultForSourceMap>& compile_results_for_source_map,
            const std::string& chunk_abs_dir,
            const std::vector<bundler::DataForSourceMap>& data_for_source_maps,
            bool can_have_shifts);
        std::string GenerateExtraDataForFileJS(uint32_t source_index);
        int AccurateFinalByteCount(const IntermediateOutput& output, const std::string& chunk_final_rel_dir);
        void PreventExportsFromBeingRenamed(uint32_t source_index);
        void RecoverInternalError(uint32_t source_index);
        // Shortens property names that are safe to rename, using a per-build
        // cache so the same property keeps the same short name across chunks.
        //
        // Example:
        //   input : code reading ".internalCounter" on a private object
        //   output: the property rewritten to a shorter name such as ".a"
        void MangleProps(std::unordered_map<std::string, bool>& mangle_cache);
        // Replaces CSS class names that are referenced only from within this
        // build with short generated identifiers, tracking which names are
        // already taken so two different classes can never collide.
        void MangleLocalCSS(std::unordered_map<std::string, bool>& used_local_names);
        std::pair<std::vector<css::Rule>, std::vector<compiler::ImportRecord>> WrapRulesWithConditions(
            std::vector<css::Rule> rules,
            const std::vector<compiler::ImportRecord>& import_records,
            const std::vector<css::ImportConditions>& conditions,
            const std::vector<compiler::ImportRecord>& condition_import_records);
        std::vector<uint32_t> FindImportedCSSFilesInJSOrder(uint32_t entry_point);
        // Returns, in the order a browser would apply them, the CSS files the
        // given entry points transitively import, including how their @import
        // chains nest.
        //
        // Example:
        //   input : entry_points = { index of "app.css" }, which imports
        //           "reset.css"
        //   output: { entry for "reset.css", entry for "app.css" }
        std::vector<CssImportOrder> FindImportedFilesInCSSOrder(const std::vector<uint32_t>& entry_points);
        bool ImportConditionsAreEqual(const std::vector<css::ImportConditions>& a, const std::vector<css::ImportConditions>& b);
        bool IsConditionalImportRedundant(const std::vector<css::ImportConditions>& earlier, const std::vector<css::ImportConditions>& later);

        // --- Import and export scanning helpers ---
        // Wraps every reachable file in its module facade and installs the
        // exports that other files import, working from the deepest dependency
        // outward so each file sees its dependencies already resolved.
        void RecursivelyWrapDependencies(uint32_t source_index);
        bool HasDynamicExportsDueToExportStar(uint32_t source_index, std::unordered_set<uint32_t>& visited);
        void AddExportsForExportStar(
            std::unordered_map<std::string, graph::ExportData>& resolved_exports,
            uint32_t source_index,
            std::vector<uint32_t>& source_index_stack);
        void ValidateComposesFromProperties(graph::LinkerFile& root_file, graph::CSSRepr& root_repr);
        void GenerateCodeForLazyExport(uint32_t source_index);
        void CreateExportsForFile(uint32_t source_index);
        void CreateWrapperForFile(uint32_t source_index);
        void MatchImportsWithExportsForFile(uint32_t source_index);

        // --- Import tracking ---
        // Follows a single import as it travels through re-export chains until
        // it either finds a real export, hits a cycle, or is declared
        // unresolvable.
        ImportTracker AdvanceImportTracker(
            const ImportTracker& tracker,
            ImportStatus& status,
            std::vector<graph::ImportData>& potentially_ambiguous_export_star_refs);
        MatchImportResult MatchImportWithExport(
            ImportTracker tracker,
            std::vector<javascript::Dependency>& re_exports);

        // --- Small helpers ---
        // Targeted checks that keep diagnostics friendly: rejecting module
        // namespace aliases that are not valid identifiers, and suggesting a
        // likely correction when an imported name looks like a typo.
        void MaybeForbidArbitraryModuleNamespaceIdentifier(
            std::string_view kind, uint32_t source_index, logger::Loc loc, const std::string& alias);
        void MaybeCorrectObviousTypo(
            graph::JSRepr& repr, const std::string& alias, logger::Msg& msg);
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Link: the main entry point
    //
    // Links a fully scanned and resolved input graph into output files. This
    // is the single doorway into the linking phase: it takes the parsed input
    // files and their entry points, runs tree shaking and code splitting,
    // matches every import against its export, generates the chunk bodies and
    // their source maps, and returns both the files to write and the per-chunk
    // metadata the HTML pass consumes.
    //
    // Example:
    //   input : entry points "app.js" and "worker.js", plus a module
    //           "shared.js" that both of them import
    //   output: files for "app.js", "worker.js", and one shared chunk named
    //           with a content hash, each with its source map, together with
    //           ChunkMetadata describing which chunks the entries depend on
    LinkResult Link(
        config::Options* options,
        helpers::Timer& timer,
        logger::Log& log,
        filesystem::Fs& fs,
        resolver::Resolver& res,
        const std::vector<graph::InputFile>& input_files,
        const std::vector<graph::EntryPoint>& entry_points,
        const std::string& unique_key_prefix,
        const std::vector<uint32_t>& reachable_files,
        std::function<std::vector<bundler::DataForSourceMap>()> data_for_source_maps,
        const std::unordered_set<uint32_t>& separate_css_entry_sources = {});

} // namespace guchho::linker
