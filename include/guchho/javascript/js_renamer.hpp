#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_helpers.hpp"

namespace guchho::javascript {

// Scans all module scopes to build a map of names that must not be used by
// the renamer. This includes names that are already declared in external
// scopes, reserved words, and names that would collide with runtime
// helpers. The returned map counts how many times each name appears, which
// the renamer uses to decide whether a name can be reused in a child scope.
//
// Example:
//   If "foo" appears 3 times across all module scopes, the map contains
//   {"foo": 3}. A child scope that declares "foo" will see count 3 and
//   know the name is heavily used.
std::unordered_map<std::string, uint32_t> ComputeReservedNames(
    const std::vector<Scope*>& module_scopes, compiler::SymbolMap& symbols);

// Abstract interface for producing a name for a given symbol. Concrete
// implementations handle different renaming strategies: identity (no
// renaming), minification (frequency-based short names), and numbered
// suffixes for debugging.
struct Renamer {
    virtual ~Renamer() = default;
    virtual std::string NameForSymbol(compiler::Ref ref) = 0;
};

// A renamer that returns each symbol's original source name without any
// modification. This is used when minification is disabled or when
// debugging requires readable identifiers.
struct NoOpRenamer : Renamer {
    compiler::SymbolMap symbols;

    explicit NoOpRenamer(compiler::SymbolMap symbols);

    std::string NameForSymbol(compiler::Ref ref) override;
};

std::unique_ptr<Renamer> NewNoOpRenamer(compiler::SymbolMap symbols);

////////////////////////////////////////////////////////////////////////////////
// MinifyRenamer — frequency-based identifier shortening
////////////////////////////////////////////////////////////////////////////////

// Tracks how many times a symbol name is used and whether it needs
// capitalization for JSX compatibility. The atomic counters allow safe
// accumulation from multiple threads during parallel processing.
struct SymbolSlot {
    std::string name;
    std::atomic<uint32_t> count{0};
    // This is really a bool but needs to be atomic for thread safety.
    std::atomic<uint32_t> needs_capital_for_jsx{0};

    SymbolSlot() = default;

    SymbolSlot(uint32_t cnt, uint32_t capital)
        : count(cnt), needs_capital_for_jsx(capital) {}
};

// Wraps a compiler::Ref with a stable source index that remains consistent
// across parallel processing. The parser assigns InnerIndex values
// sequentially within a single file, but the main thread assigns
// SourceIndex values in discovery order, which can vary depending on
// processing order. Using the DFS order index instead provides stability.
struct StableSymbolCount {
    uint32_t stable_source_index{};
    compiler::Ref ref;
    uint32_t count{};
};

// A sortable collection of StableSymbolCount values. Sorting by count
// (descending) and then by source/ref order ensures deterministic name
// assignment across runs.
using StableSymbolCountArray = std::vector<StableSymbolCount>;

// Sorts the array by usage count in descending order, with tie-breaking
// by stable source index and then by inner index. This ordering is used
// to assign the shortest names to the most frequently used symbols.
void SortStableSymbolCounts(StableSymbolCountArray& top_level_symbols);

// The primary minifying renamer. It assigns short names to symbols based
// on usage frequency: the most-used symbols get single-character names,
// less-used symbols get longer names. The renamer maintains four tiers
// of name slots (a, b, ..., aa, ab, ..., etc.) and tracks which names
// are reserved to avoid collisions.
struct MinifyRenamer : Renamer {
    std::unordered_map<std::string, uint32_t> reserved_names;
    std::array<std::vector<std::unique_ptr<SymbolSlot>>, 4> slots;
    std::unordered_map<compiler::Ref, uint32_t, RefHash> top_level_symbol_to_slot;
    compiler::SymbolMap symbols;

    MinifyRenamer(compiler::SymbolMap symbols, const compiler::SlotCounts& first_top_level_slots,
                  const std::unordered_map<std::string, uint32_t>& reserved_names);

    std::string NameForSymbol(compiler::Ref ref) override;

    // Accumulates usage counts from a single part's symbol_uses map into the
    // global count array. This is called in parallel for each part, so it uses
    // atomic operations to avoid data races.
    //
    // Example:
    //   If symbol "x" appears 5 times in part A and 3 times in part B,
    //   after both calls its count in the array will be 8.
    void AccumulateSymbolUseCounts(StableSymbolCountArray& top_level_symbols,
                                   const std::unordered_map<compiler::Ref, SymbolUse, RefHash>& symbol_uses,
                                   const std::vector<uint32_t>& stable_source_indices);

    // Accumulates a single symbol's usage count. Called in parallel; uses
    // atomic operations internally.
    void AccumulateSymbolCount(StableSymbolCountArray& top_level_symbols, compiler::Ref ref, uint32_t count,
                               const std::vector<uint32_t>& stable_source_indices);

    // Assigns name slots to top-level symbols based on their sorted usage
    // counts. The most-used symbols receive the shortest available names.
    void AllocateTopLevelSymbolSlots(const StableSymbolCountArray& top_level_symbols);

    // Fills in the actual name strings for each slot using the provided
    // NameMinifier, which generates names in frequency order (a, b, ..., aa, ...).
    void AssignNamesByFrequency(const compiler::NameMinifier& minifier);
};

std::unique_ptr<MinifyRenamer> NewMinifyRenamer(
    compiler::SymbolMap symbols, const compiler::SlotCounts& first_top_level_slots,
    const std::unordered_map<std::string, uint32_t>& reserved_names);

// Assigns slot numbers to nested-scope variables. Each scope gets a
// contiguous range of slots for its local variables. The total slot
// counts across all nested scopes are returned so the runtime can
// pre-allocate the correct number of variables.
//
// Example:
//   function outer() {
//     let a = 1;
//     function inner() {
//       let b = 2;
//     }
//   }
//   // outer gets slots 0..0, inner gets slots 1..1
//   // returns SlotCounts{ total = 2 }
compiler::SlotCounts AssignNestedScopeSlots(Scope* module_scope, std::vector<compiler::Symbol>& symbols);

////////////////////////////////////////////////////////////////////////////////
// NumberRenamer — numeric suffix renaming for debugging
////////////////////////////////////////////////////////////////////////////////

// Tracks whether a name is unused, used in an ancestor scope, or used
// in the current scope. This distinction determines how collision
// resolution proceeds.
enum class NameUse : uint8_t {
    kUnused,
    kUsed,
    kUsedInSameScope,
};

// A scope in the number-renamer's scope chain. Tracks which names have
// been used at each scope level to detect collisions efficiently.
// The name_counts map also stores the collision counter for each name,
// allowing subsequent collisions to resume counting from where the last
// collision left off rather than starting from 1 each time.
struct NumberScope {
    NumberScope* parent{};

    // Maps each name to its collision count. When a name collides, the
    // counter is incremented and appended as a numeric suffix. Storing
    // the counter avoids O(n^2) behavior when many symbols share the
    // same base name.
    std::unordered_map<std::string, uint32_t> name_counts;

    explicit NumberScope(NumberScope* p) : parent(p) {}

    // Walks the scope chain to determine whether a name is already in use.
    // Returns kUnused if the name is not found in any ancestor scope,
    // kUsedInSameScope if it appears in this scope, or kUsed if it appears
    // in an ancestor scope.
    //
    // Example:
    //   Given scopes: root -> { "x" } -> current
    //   FindNameUse("x") in current returns kUsed
    //   FindNameUse("y") in current returns kUnused
    NameUse FindNameUse(const std::string& name) const {
        const NumberScope* original = this;
        const NumberScope* s = this;
        for (;;) {
            if (s->name_counts.count(name) != 0) {
                if (s == original) {
                    return NameUse::kUsedInSameScope;
                }
                return NameUse::kUsed;
            }
            s = s->parent;
            if (s == nullptr) {
                return NameUse::kUnused;
            }
        }
    }

    // Finds a name that does not collide with any name in this scope or
    // any ancestor scope. If the requested name is already in use, numeric
    // suffixes are appended until an unused name is found. The collision
    // counter is stored to avoid O(n^2) behavior on repeated collisions.
    //
    // Example:
    //   FindUnusedName("x", kVar) with "x" already used
    //     => checks "x2", "x3", ... until finding an unused name
    //
    //   FindUnusedName("#priv", kPrivateName) validates the identifier
    //   after stripping the '#' prefix.
    std::string FindUnusedName(const std::string& name, compiler::SlotNamespace ns) {
        std::string candidate = name;
        if (ns == compiler::SlotNamespace::kPrivateName) {
            std::string_view id(candidate);
            id.remove_prefix(1);
            if (!IsIdentifier(id)) {
                candidate = ForceValidIdentifier("#", id);
            }
        } else {
            if (!IsIdentifier(candidate)) {
                candidate = ForceValidIdentifier("", candidate);
            }
        }

        NameUse use = FindNameUse(candidate);
        if (use != NameUse::kUnused) {
            uint32_t tries = 1;
            if (use == NameUse::kUsedInSameScope) {
                // Resume from the last collision count to avoid O(n^2)
                tries = name_counts[candidate];
            }
            std::string prefix = candidate;

            for (;;) {
                tries++;
                candidate = prefix + std::to_string(tries);

                if (FindNameUse(candidate) == NameUse::kUnused) {
                    if (use == NameUse::kUsedInSameScope) {
                        name_counts[prefix] = tries;
                    }
                    break;
                }
            }
        }

        // Start each name's collision counter at 1 so the first collision
        // produces "name2" rather than "name1"
        name_counts[candidate] = 1;
        return candidate;
    }
};

// A renamer that assigns names by appending numeric suffixes to the
// original identifier. This is useful for debugging because the original
// name is preserved while avoiding collisions.
struct NumberRenamer : Renamer {
    compiler::SymbolMap symbols;
    std::vector<std::vector<std::string>> names;
    NumberScope root;

    NumberRenamer(compiler::SymbolMap symbols, const std::unordered_map<std::string, uint32_t>& reserved_names);

    std::string NameForSymbol(compiler::Ref ref) override;

    // Registers a top-level symbol so it gets a name in the root scope.
    void AddTopLevelSymbol(compiler::Ref ref);

    // Assigns names to all symbols in each scope. The nested_scopes map
    // groups scopes by their source index for deterministic processing.
    //
    // Example:
    //   For source 0 with scopes [{ "x" }, { "y" }]:
    //     "x" -> "x", "y" -> "y" (if no collisions)
    //     "x" -> "x2" if "x" was already used in an ancestor
    void AssignNamesByScope(const std::unordered_map<uint32_t, std::vector<Scope*>>& nested_scopes);

private:
    void assignName(NumberScope& scope, compiler::Ref ref);

    std::unique_ptr<NumberScope> assignNamesInScope(Scope* scope, uint32_t source_index, NumberScope* parent,
                                                    std::vector<uint32_t>& sorted);

    void assignNamesRecursive(Scope* scope, uint32_t source_index, NumberScope* parent, std::vector<uint32_t>& sorted);
};

std::unique_ptr<NumberRenamer> NewNumberRenamer(compiler::SymbolMap symbols,
                                                const std::unordered_map<std::string, uint32_t>& reserved_names);

////////////////////////////////////////////////////////////////////////////////
// ExportRenamer — deduplicates exported names
////////////////////////////////////////////////////////////////////////////////

// Manages name assignment for exported symbols to avoid collisions when
// multiple exports would produce the same name. Tracks used names and
// generates unique alternatives when needed.
struct ExportRenamer {
    std::unordered_map<std::string, uint32_t> used;
    int count{};

    // Returns a unique name for the given export. If the name is already
    // used, a numeric suffix is appended.
    //
    // Example:
    //   NextRenamedName("foo") => "foo"  (first use)
    //   NextRenamedName("foo") => "foo2" (second use)
    //   NextRenamedName("foo") => "foo3" (third use)
    std::string NextRenamedName(const std::string& name);

    // Generates the next available short name (a, b, ..., aa, ab, ...).
    // Used when the original export name cannot be preserved.
    //
    // Example:
    //   NextMinifiedName() => "a"
    //   NextMinifiedName() => "b"
    //   ... after 26 names ...
    //   NextMinifiedName() => "aa"
    std::string NextMinifiedName();
};

} // namespace guchho::javascript
