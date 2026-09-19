#include "guchho/javascript/js_renamer.hpp"

#include <algorithm>
#include <thread>
#include <utility>

#include "guchho/javascript/js_lexer.hpp"

namespace guchho::javascript {

    namespace {

        // Walks a single scope and collects names that must not be renamed.
        // A name is reserved if its symbol is unbound (not defined in any
        // local scope) or is explicitly marked as must-not-be-renamed.
        // This is done for both user-defined and compiler-generated members.
        //
        // Example:
        //   Input:  scope with members {"foo" (unbound), "bar" (local)}
        //   Output: names map gets "foo" -> 1
        //
        // If the scope contains a direct eval(), the function recurses into
        // child scopes that also contain direct eval(), because an eval()
        // can reference any name visible in the enclosing scope tree.
        void ComputeReservedNamesForScope(Scope* scope, compiler::SymbolMap& symbols,
                                          std::unordered_map<std::string, uint32_t>& names) {
            for (const auto& [key, member] : scope->members) {
                (void)key;
                compiler::Symbol* symbol = symbols.Get(member.ref);
                if (symbol->kind == compiler::SymbolKind::kUnbound ||
                    compiler::Has(symbol->flags, compiler::SymbolFlags::kMustNotBeRenamed)) {
                    names[symbol->original_name] = 1;
                }
            }
            for (const compiler::Ref& ref : scope->generated) {
                compiler::Symbol* symbol = symbols.Get(ref);
                if (symbol->kind == compiler::SymbolKind::kUnbound ||
                    compiler::Has(symbol->flags, compiler::SymbolFlags::kMustNotBeRenamed)) {
                    names[symbol->original_name] = 1;
                }
            }

        // If this scope contains a direct eval(), we must continue walking
        // child scopes that also contain direct eval(). A direct eval() can
        // reference any name visible in the enclosing scope chain, so we
        // need to reserve all such names to prevent renaming collisions.
            if (scope->contains_direct_eval) {
                for (Scope* child : scope->children) {
                    if (child->contains_direct_eval) {
                        ComputeReservedNamesForScope(child, symbols, names);
                    }
                }
            }
        }

        // A slot index paired with its usage count. Used to sort symbols
        // by frequency when assigning minified names — the most-used
        // symbols get the shortest names.
        struct SlotAndCount {
            uint32_t slot;
            uint32_t count;
        };

        // Comparator for sorting SlotAndCount entries in descending order of
        // usage count. Ties are broken by slot index (ascending) to ensure
        // deterministic output across runs.
        //
        // Example:
        //   Input:  [{slot: 0, count: 5}, {slot: 1, count: 10}]
        //   Output: [{slot: 1, count: 10}, {slot: 0, count: 5}]
        bool SlotAndCountLess(const SlotAndCount& a, const SlotAndCount& b) {
            return a.count > b.count || (a.count == b.count && a.slot < b.slot);
        }

        // Recursively assigns nested-scope slot indices to symbols within a
        // scope and all of its descendants. Each symbol gets a unique slot
        // index within its namespace (default, label, private). Symbols that
        // already have a slot from a parent scope are skipped to avoid
        // reassignment. The function returns the maximum slot counts across
        // all child scopes so the parent can track the high-water mark.
        //
        // Example:
        //   Scope with symbols [x, y] and namespace kDefault:
        //     x.nested_scope_slot = 0
        //     y.nested_scope_slot = 1
        //
        // Labels are handled separately since they always live in a dedicated
        // label namespace and are declared in their own nested scope.
        compiler::SlotCounts AssignNestedScopeSlotsHelper(Scope* scope, std::vector<compiler::Symbol>& symbols,
                                                         compiler::SlotCounts slot) {
            // Sort member map keys for determinism
            std::vector<uint32_t> sorted_members;
            sorted_members.reserve(scope->members.size());
            for (const auto& [key, member] : scope->members) {
                (void)key;
                sorted_members.push_back(member.ref.inner_index);
            }
            std::sort(sorted_members.begin(), sorted_members.end());

            // Assign slots for this scope's symbols. Only do this if the slot is
            // not already assigned. Nested scopes have copies of symbols from parent
            // scopes and we want to use the slot from the parent scope, not child
            // scopes.
            for (uint32_t inner_index : sorted_members) {
                compiler::Symbol& symbol = symbols[inner_index];
                if (compiler::SlotNamespace ns = symbol.SlotNamespace();
                    ns != compiler::SlotNamespace::kMustNotBeRenamed && !symbol.nested_scope_slot.IsValid()) {
                    symbol.nested_scope_slot = compiler::Index32::Make(slot.data[static_cast<size_t>(ns)]);
                    slot.data[static_cast<size_t>(ns)]++;
                }
            }
            for (const compiler::Ref& ref : scope->generated) {
                compiler::Symbol& symbol = symbols[ref.inner_index];
                if (compiler::SlotNamespace ns = symbol.SlotNamespace();
                    ns != compiler::SlotNamespace::kMustNotBeRenamed && !symbol.nested_scope_slot.IsValid()) {
                    symbol.nested_scope_slot = compiler::Index32::Make(slot.data[static_cast<size_t>(ns)]);
                    slot.data[static_cast<size_t>(ns)]++;
                }
            }

            // Labels are always declared in a nested scope, so we don't need to check.
            if (scope->label.ref != compiler::kInvalidRef) {
                compiler::Symbol& symbol = symbols[scope->label.ref.inner_index];
                symbol.nested_scope_slot =
                    compiler::Index32::Make(slot.data[static_cast<size_t>(compiler::SlotNamespace::kLabel)]);
                slot.data[static_cast<size_t>(compiler::SlotNamespace::kLabel)]++;
            }

            // Assign slots for the symbols of child scopes
            compiler::SlotCounts slot_counts = slot;
            for (Scope* child : scope->children) {
                slot_counts.UnionMax(AssignNestedScopeSlotsHelper(child, symbols, slot));
            }
            return slot_counts;
        }

    } // namespace

    // Builds a map of all names that are reserved and must not be used as
    // minified identifiers. This includes:
    //   1. All JavaScript keywords (if, for, while, etc.)
    //   2. All strict-mode reserved words (implements, interface, etc.)
    //   3. All unbound symbols (globals from outer scopes or built-ins)
    //   4. Any symbol marked with kMustNotBeRenamed
    //
    // The resulting map is used by the renamer to skip over reserved names
    // when generating short identifiers.
    //
    // Example:
    //   Input:  module_scopes = [scope containing `console.log(x)`]
    //   Output: names map contains "if", "for", ..., "console", etc.
    std::unordered_map<std::string, uint32_t> ComputeReservedNames(
        const std::vector<Scope*>& module_scopes, compiler::SymbolMap& symbols) {
        std::unordered_map<std::string, uint32_t> names;

        // All keywords and strict mode reserved words are reserved names
        for (const auto& [word, kind] : kKeywords) {
            (void)kind;
            names[std::string(word)] = 1;
        }
        for (const auto& word : kStrictModeReservedWords) {
            names[std::string(word)] = 1;
        }

        // All unbound symbols must be reserved names
        for (Scope* scope : module_scopes) {
            ComputeReservedNamesForScope(scope, symbols, names);
        }

        return names;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // NoOpRenamer: A renamer that preserves original symbol names.
    // Used when minification is disabled or when the original names must
    // be kept for debugging or source map accuracy.
    ////////////////////////////////////////////////////////////////////////////////

    NoOpRenamer::NoOpRenamer(compiler::SymbolMap symbols) : symbols(std::move(symbols)) {}

    // Returns the original name for the given symbol reference, following
    // any alias chains to reach the underlying declaration.
    //
    // Example:
    //   Input:  ref pointing to `import { foo as bar }` (bar)
    //   Output: "foo" (the original name before aliasing)
    std::string NoOpRenamer::NameForSymbol(compiler::Ref ref) {
        ref = compiler::FollowSymbols(symbols, ref);
        return symbols.Get(ref)->original_name;
    }

    std::unique_ptr<Renamer> NewNoOpRenamer(compiler::SymbolMap symbols) {
        return std::make_unique<NoOpRenamer>(std::move(symbols));
    }

    ////////////////////////////////////////////////////////////////////////////////
    // MinifyRenamer: A renamer that assigns short, frequency-optimized names
    // to symbols. Most-used symbols get the shortest names (a, b, c, ...).
    // The renaming is split into a parallel phase (counting symbol uses) and
    // a serial phase (allocating slots and assigning names).
    ////////////////////////////////////////////////////////////////////////////////

    // Factory function that creates a MinifyRenamer. The first_top_level_slots
    // parameter specifies how many slots were pre-allocated for top-level
    // symbols before the parallel counting phase.
    std::unique_ptr<MinifyRenamer> NewMinifyRenamer(
        compiler::SymbolMap symbols, const compiler::SlotCounts& first_top_level_slots,
        const std::unordered_map<std::string, uint32_t>& reserved_names) {
        return std::make_unique<MinifyRenamer>(std::move(symbols), first_top_level_slots, reserved_names);
    }

    // Initializes the MinifyRenamer by pre-allocating empty slot arrays for
    // each namespace (default, label, private) based on the first top-level
    // slot counts. This ensures that slot indices assigned during the parallel
    // counting phase will be valid when names are assigned later.
    MinifyRenamer::MinifyRenamer(compiler::SymbolMap symbols, const compiler::SlotCounts& first_top_level_slots,
                                 const std::unordered_map<std::string, uint32_t>& reserved_names)
        : reserved_names(reserved_names), symbols(std::move(symbols)) {
        for (size_t ns = 0; ns < slots.size(); ns++) {
            uint32_t n = first_top_level_slots.data[ns];
            slots[ns].reserve(n);
            for (uint32_t i = 0; i < n; i++) {
                slots[ns].push_back(std::make_unique<SymbolSlot>());
            }
        }
    }

    // Returns the minified name for a given symbol reference.
    //
    // Name resolution follows this priority:
    //   1. If the symbol is pinned (kMustNotBeRenamed), return original name.
    //   2. If the symbol has a nested scope slot, use it directly.
    //   3. If the symbol is top-level, look up the slot via top_level_symbol_to_slot.
    //   4. If no slot is found (dead code), return the original name.
    //
    // Example:
    //   Input:  ref to `function foo() {}` with slot a
    //   Output: "a"
    std::string MinifyRenamer::NameForSymbol(compiler::Ref ref) {
        // Follow links to get to the underlying symbol
        ref = compiler::FollowSymbols(symbols, ref);
        compiler::Symbol* symbol = symbols.Get(ref);

        // Skip this symbol if the name is pinned
        compiler::SlotNamespace ns = symbol->SlotNamespace();
        if (ns == compiler::SlotNamespace::kMustNotBeRenamed) {
            return symbol->original_name;
        }

        // Check if it's a nested scope symbol
        compiler::Index32 i = symbol->nested_scope_slot;

        // If it's not (i.e. it's in a top-level scope), look up the slot
        if (!i.IsValid()) {
            auto it = top_level_symbol_to_slot.find(ref);
            if (it == top_level_symbol_to_slot.end()) {
                // If we get here, then we're printing a symbol that never had any
                // recorded uses. This is odd but can happen in certain scenarios.
                // For example, code in a branch with dead control flow won't mark
                // any uses but may still be printed. In that case it doesn't matter
                // what name we use since it's dead code.
                return symbol->original_name;
            }
            return slots[static_cast<size_t>(ns)][it->second]->name;
        }

        return slots[static_cast<size_t>(ns)][i.GetIndex()]->name;
    }

    // Accumulates usage counts for all symbols referenced in the given map.
    // This function is designed to be called in parallel — each thread
    // processes symbols from a different source file, so there are no
    // data races as long as each thread only touches its own symbols.
    //
    // Nested-scope symbols get their counts atomically incremented on their
    // slot. Top-level symbols are deferred to AllocateTopLevelSymbolSlots()
    // which runs serially after all parallel counting is complete.
    void MinifyRenamer::AccumulateSymbolUseCounts(
        StableSymbolCountArray& top_level_symbols,
        const std::unordered_map<compiler::Ref, SymbolUse, RefHash>& symbol_uses,
        const std::vector<uint32_t>& stable_source_indices) {
        // NOTE: This function is run in parallel. Make sure to avoid data races.

        for (const auto& [ref, use] : symbol_uses) {
            AccumulateSymbolCount(top_level_symbols, ref, use.count_estimate, stable_source_indices);
        }
    }

    // Accumulates the usage count for a single symbol. This function is
    // called in parallel and uses atomic operations for thread safety.
    //
    // For nested-scope symbols, the count is atomically added to the
    // symbol's slot. For top-level symbols, the count is appended to a
    // pending list that will be processed serially later.
    //
    // Symbols that are pinned or are namespace aliases are skipped.
    //
    // Example:
    //   Input:  ref to nested symbol `x` with count 3
    //   Effect: slots[kDefault][x.slot].count += 3
    void MinifyRenamer::AccumulateSymbolCount(StableSymbolCountArray& top_level_symbols, compiler::Ref ref,
                                              uint32_t count, const std::vector<uint32_t>& stable_source_indices) {
        // NOTE: This function is run in parallel. Make sure to avoid data races.

        // Follow links to get to the underlying symbol
        ref = compiler::FollowSymbols(symbols, ref);
        compiler::Symbol* symbol = symbols.Get(ref);
        while (symbol->namespace_alias != nullptr) {
            ref = compiler::FollowSymbols(symbols, symbol->namespace_alias->namespace_ref);
            symbol = symbols.Get(ref);
        }

        // Skip this symbol if the name is pinned
        compiler::SlotNamespace ns = symbol->SlotNamespace();
        if (ns == compiler::SlotNamespace::kMustNotBeRenamed) {
            return;
        }

        // Check if it's a nested scope symbol
        if (compiler::Index32 i = symbol->nested_scope_slot; i.IsValid()) {
            // If it is, accumulate the count using a parallel-safe atomic increment
            SymbolSlot* slot = slots[static_cast<size_t>(ns)][i.GetIndex()].get();
            slot->count.fetch_add(count);
            if (compiler::Has(symbol->flags, compiler::SymbolFlags::kMustStartWithCapitalLetterForJSX)) {
                slot->needs_capital_for_jsx = 1;
            }
            return;
        }

        // If it's a top-level symbol, defer it to later since we have
        // to allocate slots for these in serial instead of in parallel
        top_level_symbols.push_back(StableSymbolCount{
            /*stable_source_index=*/stable_source_indices[ref.source_index],
            /*ref=*/ref,
            /*count=*/count,
        });
    }

    // Sorts the pending top-level symbol list by usage count (descending),
    // then by source index (ascending), then by inner index (ascending).
    // This ensures deterministic slot assignment across runs: the most-used
    // symbols get the lowest slot indices and thus the shortest names.
    //
    // Example:
    //   Input:  [{ref: A, count: 10}, {ref: B, count: 10}, {ref: C, count: 5}]
    //   Output: [{ref: A, count: 10}, {ref: B, count: 10}, {ref: C, count: 5}]
    //           (A and B are ordered by source/inner index for determinism)
    void SortStableSymbolCounts(StableSymbolCountArray& top_level_symbols) {
        std::sort(top_level_symbols.begin(), top_level_symbols.end(),
                  [](const StableSymbolCount& a, const StableSymbolCount& b) {
                      if (a.count > b.count) {
                          return true;
                      }
                      if (a.count < b.count) {
                          return false;
                      }
                      if (a.stable_source_index < b.stable_source_index) {
                          return true;
                      }
                      if (a.stable_source_index > b.stable_source_index) {
                          return false;
                      }
                      return a.ref.inner_index < b.ref.inner_index;
                  });
    }

    // Processes the sorted list of top-level symbols and allocates slot
    // indices for each one. This runs serially after the parallel counting
    // phase. Symbols that share the same ref (e.g. re-exports) have their
    // counts merged into a single slot.
    //
    // Each symbol gets a slot index within its namespace (default, label,
    // private). The slot stores the accumulated usage count and whether
    // the symbol needs a capital letter for JSX compatibility.
    //
    // Example:
    //   Input:  top_level_symbols = [{ref: A, count: 5}, {ref: B, count: 3}]
    //   Effect: slot 0 assigned to A, slot 1 assigned to B
    void MinifyRenamer::AllocateTopLevelSymbolSlots(const StableSymbolCountArray& top_level_symbols) {
        for (const StableSymbolCount& stable : top_level_symbols) {
            compiler::Symbol* symbol = symbols.Get(stable.ref);
            std::vector<std::unique_ptr<SymbolSlot>>& slot_array = slots[static_cast<size_t>(symbol->SlotNamespace())];
            if (auto it = top_level_symbol_to_slot.find(stable.ref); it != top_level_symbol_to_slot.end()) {
                SymbolSlot* slot = slot_array[it->second].get();
                slot->count += stable.count;
                if (compiler::Has(symbol->flags, compiler::SymbolFlags::kMustStartWithCapitalLetterForJSX)) {
                    slot->needs_capital_for_jsx = 1;
                }
            } else {
                uint32_t needs_capital_for_jsx = 0;
                if (compiler::Has(symbol->flags, compiler::SymbolFlags::kMustStartWithCapitalLetterForJSX)) {
                    needs_capital_for_jsx = 1;
                }
                uint32_t i = static_cast<uint32_t>(slot_array.size());
                slot_array.push_back(std::make_unique<SymbolSlot>(stable.count, needs_capital_for_jsx));
                top_level_symbol_to_slot[stable.ref] = i;
            }
        }
    }

    // Assigns minified names to all slots by frequency. Symbols with higher
    // usage counts get shorter names (a, b, c, ...). The assignment respects:
    //   - Reserved names are skipped to avoid collisions with keywords.
    //   - Labels must not collide with keywords.
    //   - JSX component symbols must start with a capital letter.
    //   - Private names are prefixed with '#'.
    //
    // Example:
    //   Input:  slots = [{count: 100}, {count: 50}, {count: 1}]
    //   Output: slot 0 -> "a", slot 1 -> "b", slot 2 -> "c"
    void MinifyRenamer::AssignNamesByFrequency(const compiler::NameMinifier& minifier) {
        for (size_t ns = 0; ns < slots.size(); ns++) {
            std::vector<std::unique_ptr<SymbolSlot>>& slot_array = slots[ns];

            // Sort symbols by count
            std::vector<SlotAndCount> sorted;
            sorted.reserve(slot_array.size());
            for (size_t i = 0; i < slot_array.size(); i++) {
                sorted.push_back(SlotAndCount{static_cast<uint32_t>(i), slot_array[i]->count});
            }
            std::sort(sorted.begin(), sorted.end(), SlotAndCountLess);

            // Assign names to symbols
            int next_name = 0;
            for (const SlotAndCount& data : sorted) {
                SymbolSlot& slot = *slot_array[data.slot];
                std::string name = minifier.NumberToMinifiedName(next_name);
                next_name++;

                // Make sure we never generate a reserved name. We only have to worry
                // about collisions with reserved identifiers for normal symbols, and we
                // only have to worry about collisions with keywords for labels. We do
                // not have to worry about either for private names because they start
                // with a "#" character.
                switch (static_cast<compiler::SlotNamespace>(ns)) {
                case compiler::SlotNamespace::kDefault:
                    while (reserved_names.count(name) != 0) {
                        name = minifier.NumberToMinifiedName(next_name);
                        next_name++;
                    }

                    // Make sure names of symbols used in JSX elements start with a capital letter
                    if (slot.needs_capital_for_jsx != 0) {
                        while (name[0] >= 'a' && name[0] <= 'z') {
                            name = minifier.NumberToMinifiedName(next_name);
                            next_name++;
                        }
                    }
                    break;

                case compiler::SlotNamespace::kLabel:
                    while (kKeywords.count(name) != 0) {
                        name = minifier.NumberToMinifiedName(next_name);
                        next_name++;
                    }
                    break;

                default:
                    break;
                }

                // Private names must be prefixed with "#"
                if (static_cast<compiler::SlotNamespace>(ns) == compiler::SlotNamespace::kPrivateName) {
                    name = "#" + name;
                }

                slot.name = std::move(name);
            }
        }
    }

    // Assigns nested-scope slot indices to all symbols in a module scope's
    // children. Top-level symbols are temporarily marked as having valid
    // nested slots to prevent var-hoisted variables from getting duplicate
    // slot assignments, then restored to invalid afterward.
    //
    // Returns the maximum slot counts across all nested scopes, which is
    // used to size the slot arrays in the MinifyRenamer.
    //
    // Example:
    //   Module scope with var x = 1 in nested function:
    //     x is hoisted to module scope, so it gets no nested slot.
    compiler::SlotCounts AssignNestedScopeSlots(Scope* module_scope, std::vector<compiler::Symbol>& symbols) {
        // Temporarily set the nested scope slots of top-level symbols to valid so
        // they aren't renamed in nested scopes. This prevents us from accidentally
        // assigning nested scope slots to variables declared using "var" in a nested
        // scope that are actually hoisted up to the module scope to become a top-
        // level symbol.
        compiler::Index32 valid_slot = compiler::Index32::Make(1);
        for (const auto& [key, member] : module_scope->members) {
            (void)key;
            symbols[member.ref.inner_index].nested_scope_slot = valid_slot;
        }
        for (const compiler::Ref& ref : module_scope->generated) {
            symbols[ref.inner_index].nested_scope_slot = valid_slot;
        }

        // Assign nested scope slots independently for each nested scope
        compiler::SlotCounts slot_counts;
        for (Scope* child : module_scope->children) {
            slot_counts.UnionMax(AssignNestedScopeSlotsHelper(child, symbols, compiler::SlotCounts{}));
        }

        // Then set the nested scope slots of top-level symbols back to zero. Top-
        // level symbols are not supposed to have nested scope slots.
        for (const auto& [key, member] : module_scope->members) {
            (void)key;
            symbols[member.ref.inner_index].nested_scope_slot = compiler::Index32{};
        }
        for (const compiler::Ref& ref : module_scope->generated) {
            symbols[ref.inner_index].nested_scope_slot = compiler::Index32{};
        }
        return slot_counts;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // NumberRenamer: A renamer that assigns numeric identifiers to symbols,
    // preserving the original name when possible. Symbols are renamed to
    // avoid conflicts with reserved names and other symbols in the same scope.
    // This renamer is used for debugging-friendly output where names should
    // remain readable but still avoid collisions.
    ////////////////////////////////////////////////////////////////////////////////

    std::unique_ptr<NumberRenamer> NewNumberRenamer(compiler::SymbolMap symbols,
                                                    const std::unordered_map<std::string, uint32_t>& reserved_names) {
        return std::make_unique<NumberRenamer>(std::move(symbols), reserved_names);
    }

    // Initializes the NumberRenamer with the symbol map and reserved names.
    // The root NumberScope is created with the reserved names pre-loaded
    // so that no generated name can collide with them.
    NumberRenamer::NumberRenamer(compiler::SymbolMap symbols,
                                 const std::unordered_map<std::string, uint32_t>& reserved_names)
        : symbols(std::move(symbols)),
          names(this->symbols.symbols_for_source.size()),
          root(nullptr) {
        root.name_counts = reserved_names;
    }

    // Returns the renamed name for a symbol, or its original name if no
    // rename was assigned. The rename is looked up by source index and
    // inner index into the per-source names array.
    //
    // Example:
    //   Input:  ref to `var myLongVariableName = 1` with assigned name "a"
    //   Output: "a"
    std::string NumberRenamer::NameForSymbol(compiler::Ref ref) {
        ref = compiler::FollowSymbols(symbols, ref);
        std::vector<std::string>& inner = names[ref.source_index];
        if (!inner.empty()) {
            if (!inner[ref.inner_index].empty()) {
                return inner[ref.inner_index];
            }
        }
        return symbols.Get(ref)->original_name;
    }

    // Registers a top-level symbol for renaming. The symbol is added to
    // the root scope's name table with an unused name derived from its
    // original name (e.g. "foo" -> "foo" if available, "foo1" if not).
    void NumberRenamer::AddTopLevelSymbol(compiler::Ref ref) {
        assignName(root, ref);
    }

    // Assigns an unused name to a symbol within the given scope. The name
    // is derived from the symbol's original name, with a numeric suffix
    // added if needed to avoid collisions.
    //
    // Skipped symbols:
    //   - Already renamed (non-empty entry in names array)
    //   - Unbound or reserved (labels, private names)
    //   - JSX component names are forced to start with a capital letter
    //
    // Example:
    //   Input:  scope with names {"foo": used}, ref to `var foo = 1`
    //   Output: names[ref.source_index][ref.inner_index] = "foo1"
    void NumberRenamer::assignName(NumberScope& scope, compiler::Ref ref) {
        ref = compiler::FollowSymbols(symbols, ref);

        // Don't rename the same symbol more than once
        std::vector<std::string>& inner = names[ref.source_index];
        if (!inner.empty() && !inner[ref.inner_index].empty()) {
            return;
        }

        // Don't rename unbound symbols, symbols marked as reserved names, labels, or
        // private names
        compiler::Symbol* symbol = symbols.Get(ref);
        compiler::SlotNamespace ns = symbol->SlotNamespace();
        if (ns != compiler::SlotNamespace::kDefault && ns != compiler::SlotNamespace::kPrivateName) {
            return;
        }

        // Make sure names of symbols used in JSX elements start with a capital letter
        std::string original_name = symbol->original_name;
        if (compiler::Has(symbol->flags, compiler::SymbolFlags::kMustStartWithCapitalLetterForJSX) &&
            !original_name.empty()) {
            if (original_name[0] >= 'a' && original_name[0] <= 'z') {
                original_name[0] = static_cast<char>(original_name[0] - 'a' + 'A');
            }
        }

        // Compute a new name
        std::string name = scope.FindUnusedName(original_name, ns);

        // Store the new name
        if (inner.empty()) {
            // Note: This should not be a data race even though this method is run from
            // multiple threads. The parallel part only looks at symbols defined in
            // nested scopes, and those can only ever be accessed from within the file.
            // References to those symbols should never spread across files.
            //
            // While we could avoid the data race by densely preallocating the entire
            // "names" array ahead of time, that will waste a lot more memory for
            // builds that make heavy use of code splitting and have many chunks. Doing
            // things lazily like this means we use less memory but still stay safe.
            inner.assign(symbols.symbols_for_source[ref.source_index].size(), std::string{});
        }
        inner[ref.inner_index] = std::move(name);
    }

    // Creates a NumberScope for the given scope and renames all symbols
    // defined within it (both user-defined and generated). The scope's
    // name table inherits from the parent scope so that names used in
    // parent scopes are also reserved in child scopes.
    //
    // Member keys are sorted by inner index to ensure deterministic
    // name assignment across runs.
    std::unique_ptr<NumberScope> NumberRenamer::assignNamesInScope(Scope* scope, uint32_t source_index,
                                                                   NumberScope* parent, std::vector<uint32_t>& sorted) {
        std::unique_ptr<NumberScope> s = std::make_unique<NumberScope>(parent);

        if (!scope->members.empty()) {
            // Sort member map keys for determinism, reusing a shared memory buffer
            sorted.clear();
            sorted.reserve(scope->members.size());
            for (const auto& [key, member] : scope->members) {
                (void)key;
                sorted.push_back(member.ref.inner_index);
            }
            std::sort(sorted.begin(), sorted.end());

            // Rename all user-defined symbols in this scope
            for (uint32_t inner_index : sorted) {
                assignName(*s, compiler::Ref{source_index, inner_index});
            }
        }

        // Also rename all generated symbols in this scope
        for (const compiler::Ref& ref : scope->generated) {
            assignName(*s, ref);
        }

        return s;
    }

    // Recursively renames all symbols in a scope tree. For deeply nested
    // single-child scope chains (e.g. 10,000 nested scopes), this function
    // uses iteration instead of recursion to avoid stack overflow and improve
    // performance by 80% in extreme cases.
    //
    // Scope objects are only allocated when the scope contains symbols that
    // need renaming. Empty scopes are skipped to reduce memory overhead.
    void NumberRenamer::assignNamesRecursive(Scope* scope, uint32_t source_index, NumberScope* parent,
                                             std::vector<uint32_t>& sorted) {
        // For performance in extreme cases (e.g. 10,000 nested scopes), traversing
        // through singly-nested scopes uses iteration instead of recursion
        std::vector<std::unique_ptr<NumberScope>> owned;
        for (;;) {
            if (!scope->members.empty() || !scope->generated.empty()) {
                // For performance in extreme cases (e.g. 10,000 nested scopes), only
                // allocate a scope when it's necessary. I'm not quite sure why allocating
                // one scope per level is so much overhead. It's not that many objects.
                // Or at least there are already that many objects for the AST that we're
                // traversing, so I don't know why 80% of the time in these extreme cases
                // is taken by this function (if we don't avoid this allocation).
                owned.push_back(assignNamesInScope(scope, source_index, parent, sorted));
                parent = owned.back().get();
            }
            if (const std::vector<Scope*>& children = scope->children; children.size() == 1) {
                scope = children[0];
            } else {
                break;
            }
        }

        // Symbols in child scopes may also have to be renamed to avoid conflicts
        for (Scope* child : scope->children) {
            assignNamesRecursive(child, source_index, parent, sorted);
        }
    }

    // Assigns names to all nested-scope symbols across all source files.
    // Each source file's scopes are processed in a separate thread for
    // parallelism. Exceptions from worker threads are captured and
    // rethrown on the calling thread to maintain proper error handling.
    //
    // Example:
    //   Input:  nested_scopes = {0: [scope_a], 1: [scope_b]}
    //   Effect: scope_a and scope_b are renamed in parallel
    void NumberRenamer::AssignNamesByScope(const std::unordered_map<uint32_t, std::vector<Scope*>>& nested_scopes) {
        // Rename nested scopes from separate files in parallel
        std::vector<std::thread> threads;
        std::vector<std::exception_ptr> exceptions(nested_scopes.size());
        threads.reserve(nested_scopes.size());
        size_t thread_index = 0;
        for (const auto& [source_index, scopes] : nested_scopes) {
            size_t this_index = thread_index++;
            threads.emplace_back([this, source_index, &scopes, this_index, &exceptions]() {
                try {
                std::vector<uint32_t> sorted;
                for (Scope* scope : scopes) {
                    assignNamesRecursive(scope, source_index, &root, sorted);
                }
                } catch (...) {
                    exceptions[this_index] = std::current_exception();
                }
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
        // Rethrow on the calling thread so a panic inside a worker thread
        // becomes a normal exception instead of escaping a std::thread.
        for (auto& e : exceptions) {
            if (e) std::rethrow_exception(e);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // ExportRenamer: Generates unique names for re-exported bindings.
    // When a module re-exports the same name from multiple sources, the
    // renamer appends numeric suffixes to avoid collisions in the output.
    ////////////////////////////////////////////////////////////////////////////////

    // Returns the next available renamed name, appending a numeric suffix
    // if the base name is already in use.
    //
    // Example:
    //   Input:  name = "foo", used = {"foo": 1}
    //   Output: "foo2"
    //
    //   Input:  name = "bar", used = {}
    //   Output: "bar"
    std::string ExportRenamer::NextRenamedName(const std::string& name) {
        uint32_t tries = 1;
        if (auto it = used.find(name); it != used.end()) {
            tries = it->second;
            std::string prefix = name;
            std::string candidate = name;
            for (;;) {
                tries++;
                candidate = prefix + std::to_string(tries);
                if (used.count(candidate) == 0) {
                    break;
                }
            }
            used[candidate] = tries;
            return candidate;
        }
        used[name] = 1;
        return name;
    }

    // Returns the next minified name from the global name minifier.
    // Each call increments the counter, producing names like "a", "b",
    // "c", ..., "aa", "ab", etc.
    //
    // Example:
    //   Input:  count = 0
    //   Output: "a"
    std::string ExportRenamer::NextMinifiedName() {
        return compiler::kDefaultNameMinifierJS.NumberToMinifiedName(count++);
    }

} // namespace guchho::javascript