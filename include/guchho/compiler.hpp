#pragma once

#include "guchho/logger.hpp"
#include "guchho/helpers.hpp"

#include <cstdint>
#include <string_view>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace guchho::compiler {

    // Describes the syntactic form of an import statement as written in the
    // source file.  The import kind drives every downstream decision: how the
    // specifier is resolved against the module graph, how the resulting code
    // is wrapped (ESM ↔ CJS interop), and what metadata appears in the
    // metafile output.
    //
    //   kEntryPoint     – a file passed on the command line as a build entry
    //   kStmt           – a bare `import "foo"` at the top level of a module
    //   kRequire        – a CommonJS `require("foo")` call
    //   kDynamic        – an `import("foo")` expression
    //   kRequireResolve – `require.resolve("foo")`, kept external
    //   kInternal       – an implicit import injected by Guchho itself
    //   kAt             – a CSS `@import` rule
    //   kComposesFrom   – a CSS `composes: ... from "..."` reference
    //   kUrl            – a CSS `url("...")` reference
    //   kFile           – a file-loaded import (e.g. `file:` protocol)
    enum class ImportKind : uint8_t {
        kEntryPoint,
        kStmt,
        kRequire,
        kDynamic,
        kRequireResolve,
        kInternal,
        kAt,
        kComposesFrom,
        kUrl,
        kFile,
    };

    // Returns the human-readable label that Guchho writes into the metafile's
    // `imports` array for a given import kind.
    //
    // Input:  ImportKind::kEntryPoint
    // Output: "entry-point"
    //
    // Input:  ImportKind::kRequire
    // Output: "require"
    std::string_view ImportKindToStringForMetafile(ImportKind kind);

    // Returns true when the import originated from inside a CSS file.
    // This covers @import rules, composes-from references, and url()
    // references.
    //
    // Input:  ImportKind::kAt
    // Output: true
    //
    // Input:  ImportKind::kStmt
    // Output: false
    inline bool ImportKindIsFromCSS(ImportKind kind) {
        return kind == ImportKind::kAt || kind == ImportKind::kComposesFrom || kind == ImportKind::kUrl;
    }

    // Returns true when the import target must itself be resolved as, and
    // loaded from, a CSS module.  This is narrower than ImportKindIsFromCSS:
    // url() references may point to a non-CSS asset, so they are excluded.
    //
    // Input:  ImportKind::kComposesFrom
    // Output: true
    //
    // Input:  ImportKind::kUrl
    // Output: false
    inline bool ImportKindMustResolveToCSS(ImportKind kind) {
        return kind == ImportKind::kAt || kind == ImportKind::kComposesFrom;
    }

    // Controls when during evaluation the imported module becomes available.
    //
    //   kEvaluation – the module is loaded eagerly as part of the normal
    //                 evaluation order (the default for most imports)
    //   kDefer      – the module is deferred until a later evaluation step
    //   kSource     – only the raw source text is pulled in, used for
    //                 import attributes that need to inspect the module
    enum class ImportPhase : uint8_t {
        kEvaluation,
        kDefer,
        kSource,
    };

    // Bitfield flags that record facts Guchho discovered about an import
    // during bundling.  These flags influence code generation decisions
    // such as whether to wrap the import for CJS/ESM interop, whether to
    // mark the import as unused, and whether to emit special runtime calls.
    //
    // Flags are combined with the | operator and tested with Has().
    enum class ImportRecordFlags : uint16_t {
        kNone = 0,
        kIsUnused = 1 << 0,
        kContainsImportStar = 1 << 1,
        kContainsDefaultAlias = 1 << 2,
        kContainsESModuleAlias = 1 << 3,
        kCallsRunTimeReExportFn = 1 << 4,
        kWrapWithToESM = 1 << 5,
        kWrapWithToCJS = 1 << 6,
        kCallRuntimeRequire = 1 << 7,
        kHandlesImportErrors = 1 << 8,
        kWasOriginallyBareImport = 1 << 9,
        kIsExternalWithoutSideEffects = 1 << 10,
        kAssertTypeJSON = 1 << 11,
        kShouldNotBeExternalInMetafile = 1 << 12,
        kWasLoadedWithEmptyLoader = 1 << 13,
        kContainsUniqueKey = 1 << 14,
    };

    // Bitwise OR: combines two flag sets.
    //
    // Input:  kIsUnused | kAssertTypeJSON
    // Output: flags with both bits set
    inline ImportRecordFlags operator|(ImportRecordFlags a, ImportRecordFlags b) {
        return static_cast<ImportRecordFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }

    // Bitwise AND: intersects two flag sets.
    inline ImportRecordFlags operator&(ImportRecordFlags a, ImportRecordFlags b) {
        return static_cast<ImportRecordFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
    }

    // Returns true when `flag` is set within `flags`.
    //
    // Input:  flags = kIsUnused | kAssertTypeJSON, flag = kIsUnused
    // Output: true
    inline bool Has(ImportRecordFlags flags, ImportRecordFlags flag) {
        return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
    }

    // Index32 - a 32-bit index stored with its bits flipped, so that 0 (the
    // default) means "unset" and a real index is any non-zero value. This
    // allows an uninitialized Index32 to be distinguished from a valid index
    // of 0, which is important for Guchho's symbol resolution where index 0
    // is a legitimate entry.
    struct Index32 {
        uint32_t flipped_bits{};

        // Creates an Index32 from a raw index value.
        //
        // Input:  index = 5
        // Output: Index32 with flipped_bits = ~5
        static Index32 Make(uint32_t index) {
            Index32 result;
            result.flipped_bits = ~index;
            return result;
        }

        // Returns true when this holds a valid (non-default) index.
        bool IsValid() const {
            return flipped_bits != 0;
        }

        // Recovers the original index from the flipped representation.
        //
        // Input:  flipped_bits = ~5
        // Output: 5
        uint32_t GetIndex() const {
            return ~flipped_bits;
        }
    };

    // Forward declarations
    struct ImportAssertOrWith;
    struct GlobPattern;

    // Describes a single import statement that Guchho encountered in a source
    // file.  Each import in the bundle gets one ImportRecord, which carries
    // the resolved path, source range, import assertions/with clause, and the
    // flags and phase that shape how the import is emitted.
    struct ImportRecord {
        std::shared_ptr<ImportAssertOrWith> assert_or_with{};
        GlobPattern* glob_pattern{};
        logger::Path path;
        logger::Range range{};
        logger::Loc error_handler_loc{};
        Index32 source_index{};
        Index32 copy_source_index{};
        ImportRecordFlags flags{};
        ImportPhase phase{};
        ImportKind kind{};
    };

    // Distinguishes the two keywords Guchho recognizes on an import clause:
    // `assert` (the older syntax) versus `with` (the newer standard syntax).
    enum class AssertOrWithKeyword : uint8_t {
        kAssert,
        kWith,
    };

    // Returns the string spelling of the keyword.
    //
    // Input:  AssertOrWithKeyword::kWith
    // Output: "with"
    std::string_view AssertOrWithKeywordToString(AssertOrWithKeyword kw);

    // One key/value pair from an import assertion or with clause.  Each entry
    // preserves the source locations of both the key and value tokens so that
    // diagnostics can point at the exact offending pair.
    struct AssertOrWithEntry {
        std::u16string key;
        std::u16string value;
        logger::Loc key_loc;
        logger::Loc value_loc;
        bool prefer_quoted_key{};
    };

    // The complete import assertion / with block, including the keyword used
    // and the source locations of its opening and closing braces.
    struct ImportAssertOrWith {
        std::vector<AssertOrWithEntry> entries;
        logger::Loc keyword_loc;
        logger::Loc inner_open_brace_loc;
        logger::Loc inner_close_brace_loc;
        logger::Loc outer_open_brace_loc;
        logger::Loc outer_close_brace_loc;
        AssertOrWithKeyword keyword{};
    };

    // Searches the assertion/with entries for one whose key matches `name`.
    // Returns a pointer to the matching entry, or nullptr if no entry has
    // that key.
    //
    // Input:  assertions = [{key:"type", value:"json"}, {key:"loader", value:"ts"}]
    //         name = "type"
    // Output: pointer to the first entry
    //
    // Input:  assertions = [{key:"type", value:"json"}]
    //         name = "loader"
    // Output: nullptr
    const AssertOrWithEntry* FindAssertOrWithEntry(const std::vector<AssertOrWithEntry>& assertions, std::string_view name);
    AssertOrWithEntry* FindAssertOrWithEntry(std::vector<AssertOrWithEntry>& assertions, std::string_view name);

    // A parsed glob import pattern, holding the individual path segments
    // (split on glob metacharacters) and an optional export alias when the
    // glob is used in an export-from statement.
    struct GlobPattern {
        std::vector<helpers::GlobPart> parts;
        std::string export_alias;
        ImportKind kind{};
    };

    // Classifies how an identifier was declared and how it hoists, which
    // determines its visibility scope, renaming behavior, and lowering
    // strategy.  The kind is assigned during the binding pass and remains
    // immutable throughout linking.
    enum class SymbolKind : uint8_t {
        kUnbound,
        kHoisted,
        kHoistedFunction,
        kCatchIdentifier,
        kGeneratorOrAsyncFunction,
        kArguments,
        kClass,
        kClassInComputedPropertyKey,
        kPrivateField,
        kPrivateMethod,
        kPrivateGet,
        kPrivateSet,
        kPrivateGetSetPair,
        kPrivateStaticField,
        kPrivateStaticMethod,
        kPrivateStaticGet,
        kPrivateStaticSet,
        kPrivateStaticGetSetPair,
        kLabel,
        kTSEnum,
        kTSNamespace,
        kImport,
        kConst,
        kInjected,
        kMangledProp,
        kGlobalCSS,
        kLocalCSS,
        kOther,
    };

    // Returns true when the symbol is any private class member kind
    // (instance fields, methods, getters, setters, and their static
    // counterparts).
    //
    // Input:  SymbolKind::kPrivateField
    // Output: true
    //
    // Input:  SymbolKind::kHoisted
    // Output: false
    inline bool SymbolKindIsPrivate(SymbolKind kind) {
        return kind >= SymbolKind::kPrivateField && kind <= SymbolKind::kPrivateStaticGetSetPair;
    }

    // Returns true when the symbol is hoisted to the top of its enclosing
    // scope.  Hoisted symbols (var declarations and function declarations)
    // are visible before their textual position in the source.
    inline bool SymbolKindIsHoisted(SymbolKind kind) {
        return kind == SymbolKind::kHoisted || kind == SymbolKind::kHoistedFunction;
    }

    // Returns true when the symbol is hoisted, or is a generator/async
    // function that has similar early-visibility semantics.
    inline bool SymbolKindIsHoistedOrFunction(SymbolKind kind) {
        return SymbolKindIsHoisted(kind) || kind == SymbolKind::kGeneratorOrAsyncFunction;
    }

    // Returns true when the symbol is a function of any kind (including
    // generator and async functions).
    inline bool SymbolKindIsFunction(SymbolKind kind) {
        return kind == SymbolKind::kHoistedFunction || kind == SymbolKind::kGeneratorOrAsyncFunction;
    }

    // Returns true when the symbol is unbound (not declared in any visible
    // scope) or was injected by Guchho during bundling.
    inline bool SymbolKindIsUnboundOrInjected(SymbolKind kind) {
        return kind == SymbolKind::kUnbound || kind == SymbolKind::kInjected;
    }

    // Ref - a stable, position-independent identity for a symbol.  Every
    // symbol is uniquely identified by (source_index, inner_index), where
    // source_index names the source file and inner_index names the symbol
    // within that file.  This two-level addressing survives merging and
    // renaming because the indices are stable across the link.
    struct Ref {
        uint32_t source_index{};
        uint32_t inner_index{};
    };

    inline bool operator==(Ref a, Ref b) {
        return a.source_index == b.source_index && a.inner_index == b.inner_index;
    }

    inline bool operator!=(Ref a, Ref b) {
        return !(a == b);
    }

    // Sentinel value meaning "no symbol" — distinct from a valid Ref that
    // happens to have source_index=0, inner_index=0.
    constexpr Ref kInvalidRef{0xFFFFFFFF, 0xFFFFFFFF};

    // Pairs a byte-offset source location with a symbol reference, allowing
    // Guchho to map from a token position in the source back to its symbol.
    struct LocRef {
        logger::Loc loc{};
        Ref ref;
    };

    // Tracks whether an imported binding has been satisfied during the
    // linking pass.
    //
    //   kNone       – not yet resolved
    //   kGenerated  – Guchho generated a wrapper or re-export for it
    //   kMissing    – the binding could not be found in the target module
    enum class ImportItemStatus : uint8_t {
        kNone,
        kGenerated,
        kMissing,
    };

    // Bitfield flags on a Symbol that influence renaming and lowering.
    // These are set during the binding and linking passes and read during
    // code generation to decide whether a symbol must keep its original
    // name, whether it was already exported, and whether special lowering
    // transforms apply.
    enum class SymbolFlags : uint16_t {
        kNone = 0,
        kMustNotBeRenamed = 1 << 0,
        kMustStartWithCapitalLetterForJSX = 1 << 1,
        kDidKeepName = 1 << 2,
        kPrivateSymbolMustBeLowered = 1 << 3,
        kRemoveOverwrittenFunctionDeclaration = 1 << 4,
        kDidWarnAboutCommonJSInESM = 1 << 5,
        kCouldPotentiallyBeMutated = 1 << 6,
        kWasExported = 1 << 7,
        kIsEmptyFunction = 1 << 8,
        kIsIdentityFunction = 1 << 9,
        kCallCanBeUnwrappedIfUnused = 1 << 10,
    };

    inline SymbolFlags operator|(SymbolFlags a, SymbolFlags b) {
        return static_cast<SymbolFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }

    inline SymbolFlags operator&(SymbolFlags a, SymbolFlags b) {
        return static_cast<SymbolFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
    }

    inline bool Has(SymbolFlags flags, SymbolFlags flag) {
        return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
    }

    // Forward declarations
    struct NamespaceAlias;
    struct SymbolMap;

    // Slot namespaces partition the identifier space so that symbols from
    // different scopes can never collide when Guchho generates short names.
    // Each namespace gets its own independent numbering, so a default slot 3
    // and a label slot 3 refer to different identifiers in the output.
    enum class SlotNamespace : uint8_t {
        kDefault,
        kLabel,
        kPrivateName,
        kMangledProp,
        kMustNotBeRenamed,
    };

    // Holds the running totals of slot counters for the first four slot
    // namespaces (default, label, private name, mangled prop).  Used to
    // merge scope information when combining modules.
    struct SlotCounts {
        std::array<uint32_t, 4> data{};

        // Merges `other` into this by raising each counter to the maximum
        // of the two values.  After UnionMax, every counter is at least as
        // large as it was in either operand.
        void UnionMax(const SlotCounts& other);
    };

    // An alias that Guchho introduces for a namespace import (`import * as
    // ns`), pairing the generated alias name with a reference to the
    // namespace symbol it refers to.
    struct NamespaceAlias {
        std::string alias;
        Ref namespace_ref;
    };

    // The core record for a single identifier in a linked bundle.  Each
    // source-level declaration becomes one Symbol, which carries the
    // original name, its source location (via Ref), usage frequency, and
    // the flags and kind that guide renaming and lowering.
    struct Symbol {
        NamespaceAlias* namespace_alias{};
        std::string original_name;
        Ref link;
        uint32_t use_count_estimate{};
        Index32 chunk_index;
        Index32 nested_scope_slot;
        SymbolFlags flags{};
        SymbolKind kind{};
        ImportItemStatus import_item_status{};

        // Merges the properties of `old_symbol` into this symbol during
        // linking.  This is used when two references to the same
        // declaration are unified; flags are OR'd, use counts are summed,
        // and the surviving link target is kept.
        void MergeContentsWith(const Symbol& old_symbol);

        // Returns the slot namespace this symbol belongs to, derived from
        // its kind.  Private symbols go to kPrivateName, labels to kLabel,
        // etc.
        SlotNamespace SlotNamespace() const;
    };

    // A flat lookup table mapping (source_index, inner_index) to its Symbol.
    // The outer vector is indexed by source file, the inner vector by symbol
    // index within that file.
    struct SymbolMap {
        std::vector<std::vector<Symbol>> symbols_for_source;

        // Returns a mutable pointer to the symbol identified by `ref`.
        //
        // Input:  ref = {source_index=2, inner_index=5}
        // Output: pointer to symbols_for_source[2][5]
        Symbol* Get(Ref ref) {
            return &symbols_for_source[ref.source_index][ref.inner_index];
        }

        const Symbol* Get(Ref ref) const {
            return &symbols_for_source[ref.source_index][ref.inner_index];
        }
    };

    // Creates a SymbolMap pre-sized to hold symbols for `source_count`
    // source files.  Each source starts with an empty symbol vector.
    SymbolMap NewSymbolMap(size_t source_count);

    // Follows a symbol's link chain to the symbol it ultimately resolves to.
    // During linking, symbols may point to other symbols (via Symbol::link);
    // this function walks the chain until it reaches a symbol whose link is
    // kInvalidRef (the final target).
    //
    // Input:  ref pointing to a symbol whose link points to another symbol
    // Output: the Ref of the terminal symbol in the chain
    Ref FollowSymbols(SymbolMap& symbols, Ref ref);

    // Resolves every symbol's link in the map to its final target in a
    // single pass.  Equivalent to calling FollowSymbols on every symbol,
    // but avoids redundant traversals.
    void FollowAllSymbols(SymbolMap& symbols);

    // Merges `old_ref` into `new_ref`, redirecting the old symbol's link
    // to the new one.  Returns the surviving reference (always new_ref
    // unless old_ref was already the target).
    //
    // Input:  old_ref = A, new_ref = B  (A.link was kInvalidRef)
    // Output: B  (A now links to B)
    Ref MergeSymbols(SymbolMap& symbols, Ref old_ref, Ref new_ref);

    // Counts how often each character appears in scanned text.  The name
    // minifier uses this to assign the shortest identifiers to the most
    // frequent characters, reducing overall bundle size.
    struct CharFreq {
        std::array<int32_t, 64> freq{};

        // Adjusts character frequencies by `delta` for every character in
        // `text`.  A positive delta increments counts (scanning); a
        // negative delta decrements them (unscanning).
        void Scan(std::string_view text, int32_t delta);

        // Merges another CharFreq's counts into this one by adding
        // corresponding elements.
        void Include(const CharFreq& other);
    };

    // Produces short, safe identifier names for minified output.
    // `head` is the character pool for the first generated name, `tail`
    // is the pool for every subsequent name.  The pools are ordered so
    // that common characters yield shorter names.
    struct NameMinifier {
        std::string head;
        std::string tail;

        // Returns a new NameMinifier whose character pools are reordered
        // so that the most frequently used characters (from `freq`) map to
        // the shortest names.
        //
        // Input:  freq where 'a' appears 100 times, 'z' appears 1 time
        // Output: a NameMinifier where 'a' maps to a shorter name than 'z'
        NameMinifier ShuffleByCharFreq(const CharFreq& freq) const;

        // Converts an integer index into its minified identifier string.
        //
        // Input:  i = 0
        // Output: first character from `head` pool
        //
        // Input:  i = len(head)
        // Output: first character from `tail` pool (with length=2 name)
        std::string NumberToMinifiedName(int i) const;
    };

    // Default name minifiers used for JavaScript and CSS output
    // respectively.  The JS minifier uses characters safe for JS
    // identifiers; the CSS minifier uses characters safe for CSS class
    // names and keyframes.
    extern const NameMinifier kDefaultNameMinifierJS;
    extern const NameMinifier kDefaultNameMinifierCSS;

}
