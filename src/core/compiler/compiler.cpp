#include "guchho/helpers.hpp"
#include "guchho/compiler.hpp"

#include <algorithm>

namespace guchho::compiler {

    // Converts an ImportKind enum value to its human-readable string
    // representation for use in metafile output. Internal imports produce an
    // empty string since they are not exposed in the metafile JSON.
    //
    // Example:
    //   ImportKindToStringForMetafile(ImportKind::kStmt)  => "import-statement"
    //   ImportKindToStringForMetafile(ImportKind::kDynamic) => "dynamic-import"
    //   ImportKindToStringForMetafile(ImportKind::kInternal) => ""
    std::string_view ImportKindToStringForMetafile(ImportKind kind)
    {
        switch (kind) {
            case ImportKind::kStmt: return "import-statement";
            case ImportKind::kRequire: return "require-call";
            case ImportKind::kDynamic: return "dynamic-import";
            case ImportKind::kRequireResolve: return "require-resolve";
            case ImportKind::kInternal: return "";
            case ImportKind::kAt: return "import-rule";
            case ImportKind::kComposesFrom: return "composes-from";
            case ImportKind::kUrl: return "url-token";
            case ImportKind::kEntryPoint: return "entry-point";
            case ImportKind::kFile: return "file-loader";
        }

        return "";
    }

    // Returns the string representation of an AssertOrWithKeyword value.
    // This corresponds to the two possible keywords used in import assertions:
    // the legacy "assert" keyword and the newer "with" keyword.
    //
    // Example:
    //   AssertOrWithKeywordToString(AssertOrWithKeyword::kWith) => "with"
    //   AssertOrWithKeywordToString(AssertOrWithKeyword::kAssert) => "assert"
    std::string_view AssertOrWithKeywordToString(AssertOrWithKeyword kw) {
        switch (kw) {
            case AssertOrWithKeyword::kAssert: return "assert";
            case AssertOrWithKeyword::kWith: return "with";
        }
        return "";
    }


    // Searches through the list of import assertions (or import attributes)
    // to find one whose key matches the given name string. Returns a pointer
    // to the matching entry, or nullptr if no match is found.
    //
    // The comparison uses UTF-16-aware equality so that assertion keys
    // containing non-ASCII characters are handled correctly.
    //
    // Example:
    //   assertions = [{ key: "type", value: "json" }]
    //   FindAssertOrWithEntry(assertions, "type") => &assertions[0]
    //   FindAssertOrWithEntry(assertions, "lang") => nullptr
    //
    // Edge case: If multiple assertions share the same key, the first match
    // is returned.
    const AssertOrWithEntry* FindAssertOrWithEntry(
        const std::vector<AssertOrWithEntry>& assertions,
        std::string_view name) {
        for (const auto& assertion : assertions) {
            if (helpers::UTF16EqualsString(assertion.key, name)) {
                return &assertion;
            }
        }
        return nullptr;
    }

    // Non-const overload of FindAssertOrWithEntry. Returns a mutable pointer
    // to the matching assertion entry, allowing callers to modify the
    // assertion in-place. Otherwise identical to the const overload.
    AssertOrWithEntry* FindAssertOrWithEntry(
        std::vector<AssertOrWithEntry>& assertions,
        std::string_view name) {
        for (auto& assertion : assertions) {
            if (helpers::UTF16EqualsString(assertion.key, name)) {
                return &assertion;
            }
        }
        return nullptr;
    }



    // Merges the metadata from an older symbol declaration into this symbol.
    // This is used when two declarations of the same symbol are found in
    // different scopes and need to be unified into a single canonical symbol.
    //
    // The merge performs three operations:
    //   1. Accumulates use count estimates from the old symbol.
    //   2. If the old symbol was marked as unrenamable (e.g. a global or
    //      exported name) and this one was not, the unrenamable flag and
    //      original name are propagated.
    //   3. If the old symbol required a capital-letter start for JSX, this
    //      requirement is propagated to the merged result.
    //
    // This function does not modify the old symbol; it only reads from it.
    // After calling this function, the old symbol's link should be updated
    // to point to this symbol so that future lookups resolve correctly.
    void Symbol::MergeContentsWith(const Symbol& old_symbol) {
        use_count_estimate += old_symbol.use_count_estimate;
        if (Has(old_symbol.flags, SymbolFlags::kMustNotBeRenamed) &&
            !Has(flags, SymbolFlags::kMustNotBeRenamed)) {
            original_name = old_symbol.original_name;
            flags = flags | SymbolFlags::kMustNotBeRenamed;
        }
        if (Has(old_symbol.flags, SymbolFlags::kMustStartWithCapitalLetterForJSX)) {
            flags = flags | SymbolFlags::kMustStartWithCapitalLetterForJSX;
        }
    }


    // Determines the slot namespace for this symbol based on its kind and
    // flags. The slot namespace controls which naming strategy is used when
    // assigning a final identifier during minification.
    //
    // Returns one of:
    //   - kMustNotBeRenamed: for unbound symbols or those explicitly marked
    //     as unrenamable (globals, exports, external references).
    //   - kPrivateName: for private class members (#name syntax).
    //   - kLabel: for statement labels (used with break/continue).
    //   - kMangledProp: for property names that should be mangled.
    //   - kDefault: for all other local bindings.
    //
    // Example:
    //   A local variable "count" with no special flags
    //     => SlotNamespace::kDefault
    //   A private field "#secret" in a class
    //     => SlotNamespace::kPrivateName
    SlotNamespace Symbol::SlotNamespace() const {
        if (kind == SymbolKind::kUnbound || Has(flags, SymbolFlags::kMustNotBeRenamed)) {
            return SlotNamespace::kMustNotBeRenamed;
        }
        if (SymbolKindIsPrivate(kind)) {
            return SlotNamespace::kPrivateName;
        }
        if (kind == SymbolKind::kLabel) {
            return SlotNamespace::kLabel;
        }
        if (kind == SymbolKind::kMangledProp) {
            return SlotNamespace::kMangledProp;
        }
        return SlotNamespace::kDefault;
    }


    // Updates this SlotCounts instance to be the element-wise maximum of
    // itself and the other SlotCounts. This is used during scope analysis
    // to track the peak slot usage across nested scopes. Each slot kind
    // (catch variables, var declarations, etc.) is tracked independently.
    //
    // Example:
    //   this = {2, 1, 0, 3}
    //   other = {1, 4, 0, 2}
    //   after UnionMax: {2, 4, 0, 3}
    void SlotCounts::UnionMax(const SlotCounts& other) {
        for (size_t i = 0; i < 4; i++) {
            if (data[i] < other.data[i]) {
                data[i] = other.data[i];
            }
        }
    }


    // Scans a string of text and updates the character frequency table.
    // The frequency table is a 64-element array indexed as follows:
    //   [0..25]   = 'a'..'z' (lowercase letters)
    //   [26..51]  = 'A'..'Z' (uppercase letters)
    //   [52..61]  = '0'..'9' (digits)
    //   [62]      = '_'      (underscore)
    //   [63]      = '$'      (dollar sign)
    //
    // The delta parameter controls whether characters are added (+1) or
    // removed (-1) from the frequency table. A delta of 0 is a no-op.
    //
    // Characters outside the ASCII alphanumeric set plus '_' and '$' are
    // silently ignored. Only the low byte of each character is considered;
    // multi-byte UTF-8 sequences are not specially handled.
    //
    // Example:
    //   CharFreq freq;
    //   freq.Scan("foo_bar", +1);
    //   // freq.freq['f'-'a'] += 1, freq.freq['o'-'a'] += 2,
    //   // freq.freq['b'-'a'] += 1, freq.freq['a'-'a'] += 1,
    //   // freq.freq['_'] += 1, freq.freq['r'-'a'] += 1
    void CharFreq::Scan(std::string_view text, int32_t delta) {
        if (delta == 0) return;
        for (size_t i = 0; i < text.size(); i++) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c >= 'a' && c <= 'z') {
                freq[static_cast<size_t>(c - 'a')] += delta;
            } else if (c >= 'A' && c <= 'Z') {
                freq[static_cast<size_t>(c - 'A' + 26)] += delta;
            } else if (c >= '0' && c <= '9') {
                freq[static_cast<size_t>(c - '0' + 52)] += delta;
            } else if (c == '_') {
                freq[62] += delta;
            } else if (c == '$') {
                freq[63] += delta;
            }
        }
    }

    // Adds the character frequencies from another CharFreq table into this
    // one by summing each corresponding bucket. This is used to aggregate
    // frequency data from multiple source files or scopes.
    //
    // Example:
    //   CharFreq a, b;
    //   a.Scan("foo", +1);   // f:1, o:2
    //   b.Scan("bar", +1);   // b:1, a:1, r:1
    //   a.Include(b);
    //   // Now a has f:1, o:2, b:1, a:1, r:1
    void CharFreq::Include(const CharFreq& other) {
        for (size_t i = 0; i < 64; i++) {
            freq[i] += other.freq[i];
        }
    }


    // Helper struct used internally by NameMinifier::ShuffleByCharFreq.
    // Pairs a single-character string with its frequency count and
    // original positional index, enabling stable sorting by frequency.
    struct CharAndCount {
        std::string_view character;
        int32_t count{};
        uint8_t index{};
    };

    // Produces a new NameMinifier with its character sets reordered by
    // descending frequency. Characters that appear more often in the
    // source code are placed earlier in the minifier's head and tail
    // strings, so they are assigned to minified names first. This
    // reduces the average output size of minified identifiers.
    //
    // The head string contains only valid JavaScript identifier start
    // characters (letters, '_', '$'), while the tail string additionally
    // includes digits. After sorting, the head and tail are reconstructed
    // from the sorted characters, preserving the invariant that head is
    // a prefix of the valid-start-character subset.
    //
    // Example:
    //   Original minifier head = "ab" (a=10, b=5)
    //   After ShuffleByCharFreq: head = "ba" (b is less frequent,
    //   so 'a' comes first; wait, sort is descending, so a=10 > b=5
    //   means head stays "ab")
    //
    // Edge case: If the frequency table is empty or zeroed, the original
    // character order is preserved (stable sort by original index).
    NameMinifier NameMinifier::ShuffleByCharFreq(const CharFreq& freq) const {
        std::vector<CharAndCount> array(64);
        for (size_t i = 0; i < tail.size(); i++) {
            array[i] = {
                .character = std::string_view(tail).substr(i, 1),
                .count = freq.freq[i],
                .index = static_cast<uint8_t>(i),
            };
        }

        std::sort(array.begin(), array.end(),
            [](const CharAndCount& a, const CharAndCount& b) {
                if (a.count != b.count) return a.count > b.count;
                return a.index < b.index;
            });

        NameMinifier minifier;
        for (const auto& item : array) {
            if (!item.character.empty()) {
                char c = item.character[0];
                if (c < '0' || c > '9') {
                    minifier.head += c;
                }
                minifier.tail += c;
            }
        }
        return minifier;
    }


    // Converts a zero-based integer index into a minified identifier name.
    // The first character is drawn from the head string (valid identifier
    // start characters), and subsequent characters are drawn from the tail
    // string (which additionally includes digits).
    //
    // The numbering scheme is a mixed-radix system: the head provides the
    // most significant "digit" and the tail provides all subsequent digits.
    // This guarantees that every generated name is a valid JavaScript
    // identifier (starts with a letter or underscore/dollar).
    //
    // Example with head="ab", tail="ab012":
    //   i=0 => "a"         (head[0])
    //   i=1 => "b"         (head[1])
    //   i=2 => "aa"        (head[0] + tail[0])
    //   i=3 => "ba"        (head[1] + tail[0])
    //   i=4 => "ab"        (head[0] + tail[1])
    //   i=5 => "bb"        (head[1] + tail[1])
    //   i=6 => "a0"        (head[0] + tail[2])
    //
    // Edge case: The function supports indices up to SIZE_MAX. The output
    // name grows logarithmically with the index.
    std::string NameMinifier::NumberToMinifiedName(int i) const {
        size_t n_head = head.size();
        size_t n_tail = tail.size();

        size_t j = static_cast<size_t>(i) % n_head;
        std::string name;
        name += head[j];
        i = static_cast<int>(static_cast<size_t>(i) / n_head);

        while (i > 0) {
            i--;
            j = static_cast<size_t>(i) % n_tail;
            name += tail[j];
            i = static_cast<int>(static_cast<size_t>(i) / n_tail);
        }

        return name;
    }


    // Default name minifier for JavaScript output. The head includes all
    // valid JavaScript identifier start characters (a-z, A-Z, _, $) and
    // the tail additionally includes digits (0-9). The $ is placed last
    // since it is the least common identifier character in typical code.
    const NameMinifier kDefaultNameMinifierJS = {
        .head = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$",
        .tail = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$",
    };

    // Default name minifier for CSS output. CSS identifiers cannot start
    // with a digit, so the head excludes digits. The tail includes digits
    // since they are valid in non-initial positions of CSS identifiers.
    // The $ is excluded entirely since it is not valid in CSS identifiers.
    const NameMinifier kDefaultNameMinifierCSS = {
        .head = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_",
        .tail = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_",
    };

    // ---------------------------------------------------------------------------
    // SymbolMap: a union-find data structure for tracking symbol identity
    // across scopes. Each source file has its own array of symbols, and
    // symbols can be linked together to represent that two declarations in
    // different scopes actually refer to the same binding. The link chain
    // is compressed via path compression in FollowSymbols.
    // ---------------------------------------------------------------------------

    // Creates a new SymbolMap with space allocated for the given number of
    // source files. Each source file will initially have an empty symbol
    // array; symbols are added later as declarations are discovered.
    //
    // Example:
    //   SymbolMap map = NewSymbolMap(3);
    //   // map.symbols_for_source has 3 empty vectors
    SymbolMap NewSymbolMap(size_t source_count) {
        SymbolMap map;
        map.symbols_for_source.resize(source_count);
        return map;
    }

    // Follows the chain of symbol links from the given reference to its
    // ultimate canonical representative. This implements path compression:
    // if a symbol points to another symbol that itself has a link, the
    // first symbol is updated to point directly to the final target.
    //
    // Returns the original reference if it has no link (it is already the
    // canonical representative).
    //
    // Example:
    //   Symbol A links to B, B links to C, C has no link.
    //   FollowSymbols(A) => C
    //   After the call, A is updated to point directly to C.
    //
    // Edge case: Circular links are not expected but would cause infinite
    // recursion. The caller must ensure that merge operations never create
    // cycles.
    Ref FollowSymbols(SymbolMap& symbols, Ref ref) {
        Symbol* symbol = symbols.Get(ref);
        if (symbol->link == kInvalidRef) {
            return ref;
        }

        Ref link = FollowSymbols(symbols, symbol->link);

        if (symbol->link != link) {
            symbol->link = link;
        }

        return link;
    }

    // Convenience function that follows all symbol links across the entire
    // SymbolMap. This is typically called once after all symbols have been
    // merged, to ensure that every symbol's link chain is fully compressed
    // before the map is read during code generation.
    void FollowAllSymbols(SymbolMap& symbols) {
        for (size_t source_index = 0; source_index < symbols.symbols_for_source.size(); source_index++) {
            for (size_t symbol_index = 0; symbol_index < symbols.symbols_for_source[source_index].size(); symbol_index++) {
                FollowSymbols(symbols, Ref{
                    static_cast<uint32_t>(source_index),
                    static_cast<uint32_t>(symbol_index),
                });
            }
        }
    }

    // Merges two symbol references into a single canonical symbol. This is
    // the core operation used when two declarations of the same binding
    // are discovered (e.g. a re-export and an original export, or two
    // imports of the same name).
    //
    // The merge follows these steps:
    //   1. If both references are the same, return immediately.
    //   2. If old_ref has an existing link, recursively merge that link
    //      with new_ref instead.
    //   3. If new_ref has an existing link, recursively merge old_ref
    //      with that link.
    //   4. Otherwise, link old_ref to new_ref and merge old_ref's
    //      metadata (use counts, rename flags) into new_ref.
    //
    // Returns the canonical reference that both symbols now resolve to.
    //
    // Example:
    //   old_ref = "exported_name" from module A
    //   new_ref = "exported_name" from module B
    //   MergeSymbols(map, old_ref, new_ref) => new_ref
    //   After the call, old_ref.link == new_ref, and new_ref's metadata
    //   includes the merged use counts from both declarations.
    //
    // Edge case: If either symbol already has a link, the merge is
    // redirected to follow the existing chain, ensuring the union-find
    // invariant is maintained.
    Ref MergeSymbols(SymbolMap& symbols, Ref old_ref, Ref new_ref) {
        if (old_ref == new_ref) {
            return new_ref;
        }

        Symbol* old_symbol = symbols.Get(old_ref);
        if (old_symbol->link != kInvalidRef) {
            old_symbol->link = MergeSymbols(symbols, old_symbol->link, new_ref);
            return old_symbol->link;
        }

        Symbol* new_symbol = symbols.Get(new_ref);
        if (new_symbol->link != kInvalidRef) {
            new_symbol->link = MergeSymbols(symbols, old_ref, new_symbol->link);
            return new_symbol->link;
        }

        old_symbol->link = new_ref;
        new_symbol->MergeContentsWith(*old_symbol);
        return new_ref;
    }

}
