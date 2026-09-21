// JavaScript/TypeScript Parser Implementation
//
// This file implements the two-pass parser for JavaScript and TypeScript source code.
//
// Pass 1 (parse): Builds the AST from a token stream, declares symbols in scopes,
//                 creates scope trees, and collects import/export metadata.
//
// Pass 2 (visit): Walks the AST to perform symbol binding, type checking,
//                 error reporting, constant folding, dead code elimination,
//                 and syntax lowering (async/await, destructuring, classes, etc.).
//
// The main entry point is Parse() which orchestrates both passes and returns the
// final AST. The Parser struct holds all mutable state and is used as a shared
// context between the many helper functions that implement individual syntax forms.

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <regex>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_helpers.hpp"


namespace guchho::javascript {

    namespace {

        // Formats a human-readable description of the configured target environment,
        // including the original environment name and any override counts.
        // Used when emitting error messages about unsupported features.
        std::pair<std::string, std::vector<logger::MsgData>> prettyPrintTargetEnvironment(compat::JSFeature /*feature*/, const optionsThatSupportStructuralEquality &opts) {
            std::string where = "the configured target environment";
            std::string overrides;
            if (static_cast<uint64_t>(opts.unsupportedJSFeatureOverridesMask) != 0) {
                int count = 0;
                uint64_t mask = static_cast<uint64_t>(opts.unsupportedJSFeatureOverridesMask);
                while (mask != 0) {
                    if ((mask & 1) != 0) {
                        count++;
                    }
                    mask >>= 1;
                }
                std::string s = "s";
                if (count == 1) {
                    s = "";
                }
                overrides = " + " + std::to_string(count) + " override" + s;
            }
            if (!opts.originalTargetEnv.empty()) {
                where = where + " (" + opts.originalTargetEnv + overrides + ")";
            }
            return {where, {}};
        }

    }

    // Merges accumulated deferred errors into another deferredErrors struct.
    // Called when a speculative parse succeeds and its accumulated errors should be
    // propagated to the parent context. Only non-empty errors are merged to avoid
    // overwriting existing errors with empty ranges.
    void deferredErrors::mergeInto(deferredErrors *to) const {
        if (this->invalidExprDefaultValue.len > 0) {
            to->invalidExprDefaultValue = this->invalidExprDefaultValue;
        }
        if (this->invalidExprAfterQuestion.len > 0) {
            to->invalidExprAfterQuestion = this->invalidExprAfterQuestion;
        }
        if (this->arraySpreadFeature.len > 0) {
            to->arraySpreadFeature = this->arraySpreadFeature;
        }
        if (!this->invalidParens.empty()) {
            if (!to->invalidParens.empty()) {
                to->invalidParens.insert(to->invalidParens.end(), this->invalidParens.begin(), this->invalidParens.end());
            } else {
                to->invalidParens = this->invalidParens;
            }
        }
    }

    // Structural equality comparison for parser options. This allows two parser
    // invocations to share cached results when their configurations are identical.
    // All fields must be value-comparable — pointers like defines are compared by
    // their contents rather than their addresses.
    bool optionsThatSupportStructuralEquality::operator==(const optionsThatSupportStructuralEquality& other) const {
        return this->originalTargetEnv == other.originalTargetEnv &&
            this->moduleTypeData.source == other.moduleTypeData.source &&
            this->moduleTypeData.range.loc == other.moduleTypeData.range.loc &&
            this->moduleTypeData.range.len == other.moduleTypeData.range.len &&
            this->moduleTypeData.type == other.moduleTypeData.type &&
            this->unsupportedJSFeatures == other.unsupportedJSFeatures &&
            this->unsupportedJSFeatureOverrides == other.unsupportedJSFeatureOverrides &&
            this->unsupportedJSFeatureOverridesMask == other.unsupportedJSFeatureOverridesMask &&
            this->ts.Config.ExperimentalDecorators == other.ts.Config.ExperimentalDecorators &&
            this->ts.Config.ImportsNotUsedAsValues == other.ts.Config.ImportsNotUsedAsValues &&
            this->ts.Config.PreserveValueImports == other.ts.Config.PreserveValueImports &&
            this->ts.Config.Target == other.ts.Config.Target &&
            this->ts.Config.UseDefineForClassFields == other.ts.Config.UseDefineForClassFields &&
            this->ts.Config.VerbatimModuleSyntax == other.ts.Config.VerbatimModuleSyntax &&
            this->ts.Parse == other.ts.Parse &&
            this->ts.NoAmbiguousLessThan == other.ts.NoAmbiguousLessThan &&
            this->mode == other.mode &&
            this->platform == other.platform &&
            this->outputFormat == other.outputFormat &&
            this->logPathStyle == other.logPathStyle &&
            this->codePathStyle == other.codePathStyle &&
            this->asciiOnly == other.asciiOnly &&
            this->keepNames == other.keepNames &&
            this->minifySyntax == other.minifySyntax &&
            this->minifyIdentifiers == other.minifyIdentifiers &&
            this->minifyWhitespace == other.minifyWhitespace &&
            this->omitRuntimeForTests == other.omitRuntimeForTests &&
            this->omitJSXRuntimeForTests == other.omitJSXRuntimeForTests &&
            this->ignoreDCEAnnotations == other.ignoreDCEAnnotations &&
            this->treeShaking == other.treeShaking &&
            this->dropDebugger == other.dropDebugger &&
            this->mangleQuoted == other.mangleQuoted &&
            this->decodeHydrateRuntimeStateYarnPnP == other.decodeHydrateRuntimeStateYarnPnP;
    }

    // Creates a minimal Options struct for Yarn Plug'n'Play support.
    // This is used when the parser is invoked specifically to decode Yarn PnP state
    // without needing full parsing options.
    Options OptionsForYarnPnP() {
        Options options;
        options.optionsThatSupportStructuralEquality.decodeHydrateRuntimeStateYarnPnP = true;
        return options;
    }

    // Converts a config::Options struct into the parser's Options struct.
    // This maps all configuration fields from the general config format into the
    // parser-specific format, separating structurally-comparable options from those
    // that cannot be compared by value (like regex pointers and injected files).
    Options OptionsFromConfig(config::Options *options) {
        Options result;
        result.injectedFiles = options->InjectedFiles;
        result.jsx = options->JSX;
        result.defines = options->Defines;
        result.tsAlwaysStrict = options->TSAlwaysStrictData;
        result.mangleProps = options->MangleProps.get();
        result.reserveProps = options->ReserveProps.get();
        result.dropLabels = options->DropLabels;

        result.optionsThatSupportStructuralEquality.unsupportedJSFeatures = options->UnsupportedJSFeatures;
        result.optionsThatSupportStructuralEquality.unsupportedJSFeatureOverrides = options->UnsupportedJSFeatureOverrides;
        result.optionsThatSupportStructuralEquality.unsupportedJSFeatureOverridesMask = options->UnsupportedJSFeatureOverridesMask;
        result.optionsThatSupportStructuralEquality.originalTargetEnv = options->OriginalTargetEnv;
        result.optionsThatSupportStructuralEquality.ts = options->TS;
        result.optionsThatSupportStructuralEquality.mode = options->BuildMode;
        result.optionsThatSupportStructuralEquality.platform = options->OutputPlatform;
        result.optionsThatSupportStructuralEquality.outputFormat = options->OutputFormat;
        result.optionsThatSupportStructuralEquality.moduleTypeData = options->ModuleTypeData;
        result.optionsThatSupportStructuralEquality.asciiOnly = options->ASCIIOnly;
        result.optionsThatSupportStructuralEquality.keepNames = options->KeepNames;
        result.optionsThatSupportStructuralEquality.minifySyntax = options->MinifySyntax;
        result.optionsThatSupportStructuralEquality.minifyIdentifiers = options->MinifyIdentifiers;
        result.optionsThatSupportStructuralEquality.minifyWhitespace = options->MinifyWhitespace;
        result.optionsThatSupportStructuralEquality.omitRuntimeForTests = options->OmitRuntimeForTests;
        result.optionsThatSupportStructuralEquality.omitJSXRuntimeForTests = options->OmitJSXRuntimeForTests;
        result.optionsThatSupportStructuralEquality.ignoreDCEAnnotations = options->IgnoreDCEAnnotations;
        result.optionsThatSupportStructuralEquality.treeShaking = options->TreeShaking;
        result.optionsThatSupportStructuralEquality.dropDebugger = options->DropDebugger;
        result.optionsThatSupportStructuralEquality.mangleQuoted = options->MangleQuoted;
        result.optionsThatSupportStructuralEquality.logPathStyle = options->LogPathStyle;
        result.optionsThatSupportStructuralEquality.codePathStyle = options->CodePathStyle;

        return result;
    }

    // Deep equality comparison for the full Options struct. In addition to the
    // structurally-comparable subset, this also compares pointer-based fields by
    // value (tsAlwaysStrict contents, regex patterns, injected files, JSX config,
    // and defines). Used to determine if two parser invocations can share results.
    bool Options::equal(const Options &b) const {
        // Compare "optionsThatSupportStructuralEquality"
        if (!(this->optionsThatSupportStructuralEquality == b.optionsThatSupportStructuralEquality)) {
            return false;
        }

        // Compare "tsAlwaysStrict"
        if ((this->tsAlwaysStrict == nullptr && b.tsAlwaysStrict != nullptr) || (this->tsAlwaysStrict != nullptr && b.tsAlwaysStrict == nullptr) ||
            (this->tsAlwaysStrict != nullptr && b.tsAlwaysStrict != nullptr &&
                !(this->tsAlwaysStrict->Name == b.tsAlwaysStrict->Name &&
                    this->tsAlwaysStrict->SourceData.contents == b.tsAlwaysStrict->SourceData.contents &&
                    this->tsAlwaysStrict->RangeData.loc == b.tsAlwaysStrict->RangeData.loc &&
                    this->tsAlwaysStrict->RangeData.len == b.tsAlwaysStrict->RangeData.len &&
                    this->tsAlwaysStrict->Value == b.tsAlwaysStrict->Value))) {
            return false;
        }

        // Compare "mangleProps" and "reserveProps"
        if (!isSameRegexp(this->mangleProps, b.mangleProps) || !isSameRegexp(this->reserveProps, b.reserveProps)) {
            return false;
        }

        // Compare "dropLabels"
        if (!helpers::StringArraysEqual(this->dropLabels, b.dropLabels)) {
            return false;
        }

        // Compare "injectedFiles"
        if (this->injectedFiles.size() != b.injectedFiles.size()) {
            return false;
        }
        for (size_t i = 0; i < this->injectedFiles.size(); i++) {
            const config::InjectedFile &x = this->injectedFiles[i];
            const config::InjectedFile &y = b.injectedFiles[i];
            if (x.SourceData.contents != y.SourceData.contents || x.DefineName != y.DefineName || x.Exports.size() != y.Exports.size()) {
                return false;
            }
            for (size_t j = 0; j < x.Exports.size(); j++) {
                if (!(x.Exports[j].Alias == y.Exports[j].Alias && x.Exports[j].LocData.start == y.Exports[j].LocData.start)) {
                    return false;
                }
            }
        }

        // Compare "jsx"
        if (this->jsx.Parse != b.jsx.Parse || !jsxExprsEqual(this->jsx.Factory, b.jsx.Factory) || !jsxExprsEqual(this->jsx.Fragment, b.jsx.Fragment)) {
            return false;
        }

        // Do a cheap assert that the defines object hasn't changed
        if ((this->defines != nullptr || b.defines != nullptr) && (this->defines == nullptr || b.defines == nullptr ||
            this->defines->IdentifierDefines.size() != b.defines->IdentifierDefines.size() ||
            this->defines->DotDefines.size() != b.defines->DotDefines.size())) {
            throw std::runtime_error("Internal error");
        }

        return true;
    }

    // Compares two std::regex pointers for structural equality. Since std::regex
    // does not support ==, this always returns false for non-null pointers.
    // Returns true only when both are null.
    bool isSameRegexp(std::regex *a, std::regex *b) {
        if (a == nullptr) {
            return b == nullptr;
        }
        return b != nullptr && false;
    }

    // Compares two JSX factory/fragment expressions for structural equality.
    // Checks that both have the same dot-separated parts and (if constant)
    // the same constant value. Used by Options::equal to determine cacheability.
    bool jsxExprsEqual(config::DefineExpr a, config::DefineExpr b) {
        if (!helpers::StringArraysEqual(a.Parts, b.Parts)) {
            return false;
        }

        if (a.HasConstant()) {
            if (!b.HasConstant() || !ValuesLookTheSame(a.Constant, b.Constant)) {
                return false;
            }
        } else if (b.HasConstant()) {
            return false;
        }

        return true;
    }

    // Computes a deterministic hash for a switch case expression. Returns the hash
    // and a boolean indicating whether the expression is hashable (i.e., a pure
    // literal, identifier, or property access). Expressions with side effects or
    // runtime-dependent values return false.
    //
    // Input:  `case 42:`  → (hash, true)
    // Input:  `case foo:` → (hash, true)  (identifier hash)
    // Input:  `case a.b:` → (hash, true)  (recursive dot hash)
    // Input:  `case fn():`→ (0, false)    (not hashable)
    std::pair<uint32_t, bool> duplicateCaseHash(Expr expr) {
        if (const EInlinedEnum *e = Get<EInlinedEnum>(expr.data); e != nullptr) {
            return duplicateCaseHash(e->value);
        }

        if (const ENull *e = Get<ENull>(expr.data); e != nullptr) {
            return {0, true};
        }

        if (const EUndefined *e = Get<EUndefined>(expr.data); e != nullptr) {
            return {1, true};
        }

        if (const EBoolean *e = Get<EBoolean>(expr.data); e != nullptr) {
            if (e->value) {
                return {helpers::HashCombine(2, 1), true};
            }
            return {helpers::HashCombine(2, 0), true};
        }

        if (const ENumber *e = Get<ENumber>(expr.data); e != nullptr) {
            uint64_t bits = std::bit_cast<uint64_t>(e->value);
            return {helpers::HashCombine(helpers::HashCombine(3, static_cast<uint32_t>(bits)), static_cast<uint32_t>(bits >> 32)), true};
        }

        if (const EString *e = Get<EString>(expr.data); e != nullptr) {
            uint32_t hash = 4;
            for (char16_t c : e->value) {
                hash = helpers::HashCombine(hash, static_cast<uint32_t>(c));
            }
            return {hash, true};
        }

        if (const EBigInt *e = Get<EBigInt>(expr.data); e != nullptr) {
            uint32_t hash = 5;
            for (char c : e->value) {
                hash = helpers::HashCombine(hash, static_cast<uint32_t>(static_cast<uint8_t>(c)));
            }
            return {hash, true};
        }

        if (const EIdentifier *e = Get<EIdentifier>(expr.data); e != nullptr) {
            return {helpers::HashCombine(6, e->ref.inner_index), true};
        }

        if (const EDot *e = Get<EDot>(expr.data); e != nullptr) {
            uint32_t target;
            bool ok;
            std::tie(target, ok) = duplicateCaseHash(e->target);
            if (ok) {
                return {helpers::HashCombineString(helpers::HashCombine(7, target), e->name), true};
            }
        }

        if (const EIndex *e = Get<EIndex>(expr.data); e != nullptr) {
            uint32_t target;
            bool ok;
            std::tie(target, ok) = duplicateCaseHash(e->target);
            if (ok) {
                uint32_t index;
                std::tie(index, ok) = duplicateCaseHash(e->index);
                if (ok) {
                    return {helpers::HashCombine(helpers::HashCombine(8, target), index), true};
                }
            }
        }

        return {0, false};
    }

    // Performs exact equality comparison of two switch case expressions.
    // Returns a pair: (isEqual, hasSideEffects). The hasSideEffects flag indicates
    // whether the comparison traversed property accesses (dot/index), which means
    // the comparison could have observable side effects and shouldn't be collapsed.
    //
    // Input:  (42, 42)    → (true,  false)
    // Input:  (a.b, a.b)  → (true,  true)   // dot access has side effects
    // Input:  (foo, bar)  → (false, false)   // different identifiers
    std::pair<bool, bool> duplicateCaseEquals(Expr left, Expr right) {
        if (const EInlinedEnum *b = Get<EInlinedEnum>(right.data); b != nullptr) {
            return duplicateCaseEquals(left, b->value);
        }

        if (const EInlinedEnum *a = Get<EInlinedEnum>(left.data); a != nullptr) {
            return duplicateCaseEquals(a->value, right);
        }

        if (const ENull *a = Get<ENull>(left.data); a != nullptr) {
            return {Get<ENull>(right.data) != nullptr, false};
        }

        if (const EUndefined *a = Get<EUndefined>(left.data); a != nullptr) {
            return {Get<EUndefined>(right.data) != nullptr, false};
        }

        if (const EBoolean *a = Get<EBoolean>(left.data); a != nullptr) {
            const EBoolean *b = Get<EBoolean>(right.data);
            return {b != nullptr && a->value == b->value, false};
        }

        if (const ENumber *a = Get<ENumber>(left.data); a != nullptr) {
            const ENumber *b = Get<ENumber>(right.data);
            return {b != nullptr && a->value == b->value, false};
        }

        if (const EString *a = Get<EString>(left.data); a != nullptr) {
            const EString *b = Get<EString>(right.data);
            return {b != nullptr && helpers::UTF16EqualsUTF16(a->value, b->value), false};
        }

        if (const EBigInt *a = Get<EBigInt>(left.data); a != nullptr) {
            if (const EBigInt *b = Get<EBigInt>(right.data); b != nullptr) {
                std::optional<bool> equal = CheckEqualityBigInt(a->value, b->value);
                return {equal.has_value() && *equal, false};
            }
        }

        if (const EIdentifier *a = Get<EIdentifier>(left.data); a != nullptr) {
            const EIdentifier *b = Get<EIdentifier>(right.data);
            return {b != nullptr && a->ref == b->ref, false};
        }

        if (const EDot *a = Get<EDot>(left.data); a != nullptr) {
            const EDot *b = Get<EDot>(right.data);
            if (b != nullptr && a->optional_chain == b->optional_chain && a->name == b->name) {
                std::pair<bool, bool> result = duplicateCaseEquals(a->target, b->target);
                return {result.first, true};
            }
        }

        if (const EIndex *a = Get<EIndex>(left.data); a != nullptr) {
            const EIndex *b = Get<EIndex>(right.data);
            if (b != nullptr && a->optional_chain == b->optional_chain) {
                std::pair<bool, bool> result = duplicateCaseEquals(a->index, b->index);
                if (result.first) {
                    result = duplicateCaseEquals(a->target, b->target);
                    return {result.first, true};
                }
            }
        }

        return {false, false};
    }

    // Extracts and sorts all keys from an unordered_map<string, LocRef>.
    // Used to generate deterministic import statements from maps that have
    // no guaranteed iteration order.
    std::vector<std::string> sortedKeysOfMapStringLocRef(const std::unordered_map<std::string, compiler::LocRef> &in) {
        std::vector<std::string> keys;
        keys.reserve(in.size());
        for (const auto &entry : in) {
            keys.push_back(entry.first);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    // Selects the effective local variable kind, applying target-environment
    // downgrades (let/const → var) and minification optimizations.
    //
    // Rules:
    // 1. If target env doesn't support const/let, downgrade to var.
    // 2. If at top level and bundling or using-wrap, downgrade to var
    //    (so declarations can be moved into a try/catch).
    // 3. If minifying in bundle mode, downgrade const to let (shorter).
    LocalKind Parser::selectLocalKind(LocalKind kind) {
        // Use "var" instead of "let" and "const" if they aren't supported
        if ((kind == LocalKind::kLet || kind == LocalKind::kConst) &&
            compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kConstAndLet)) {
            return LocalKind::kVar;
        }

        // Use "var" instead of "let" and "const" if the variable declaration may
        // need to be separated from the initializer. This allows us to safely move
        // this declaration into a nested scope.
        if (this->currentScope->parent == nullptr && (kind == LocalKind::kLet || kind == LocalKind::kConst) &&
            (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle || this->willWrapModuleInTryCatchForUsing)) {
            return LocalKind::kVar;
        }

        // Optimization: use "let" instead of "const" because it's shorter. This is
        // only done when bundling because assigning to "const" is only an error when
        // bundling.
        if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle && kind == LocalKind::kConst &&
            this->options.optionsThatSupportStructuralEquality.minifySyntax) {
            return LocalKind::kLet;
        }

        return kind;
    }

    // Pushes a new scope during the parse pass. Creates a Scope node, links it
    // to the parent, and records it in scopesInOrder for later use by enum
    // lowering and scope-flattening operations.
    //
    // Special handling for kFunctionBody scopes: copies parent's arguments into
    // the body scope so re-declaration of arguments is properly detected.
    //
    // Returns the index into scopesInOrder that can be passed to popScope /
    // popAndDiscardScope / discardScopesUpTo.
    int Parser::pushScopeForParsePass(ScopeKind kind, logger::Loc loc) {
        Scope *parent = this->currentScope;
        Scope *scope = new Scope();
        scope->kind = kind;
        scope->parent = parent;
        scope->label = compiler::LocRef{logger::Loc{}, compiler::kInvalidRef};
        if (parent != nullptr) {
            parent->children.push_back(scope);
            scope->strict_mode = parent->strict_mode;
            scope->use_strict_loc = parent->use_strict_loc;
        }
        this->currentScope = scope;

        // Enforce that scope locations are strictly increasing to help catch bugs
        // where the pushed scopes are mismatched between the first and second passes
        if (!this->scopesInOrder.empty()) {
            int32_t prevStart = this->scopesInOrder[this->scopesInOrder.size() - 1].loc.start;
            if (prevStart >= loc.start) {
                throw LexerPanic();
            }
        }

        // Copy down function arguments into the function body scope. That way we get
        // errors if a statement in the function body tries to re-declare any of the
        // arguments.
        if (kind == ScopeKind::kFunctionBody) {
            if (scope->parent->kind != ScopeKind::kFunctionArgs) {
                throw std::runtime_error("Internal error");
            }
            for (auto &entry : scope->parent->members) {
                // Don't copy down the optional function expression name. Re-declaring
                // the name of a function expression is allowed.
                compiler::SymbolKind entry_kind = this->symbols[entry.second.ref.inner_index].kind;
                if (entry_kind != compiler::SymbolKind::kHoistedFunction) {
                    scope->members[entry.first] = entry.second;
                }
            }
        }

        // Remember the length in case we call popAndDiscardScope() later
        int scopeIndex = static_cast<int>(this->scopesInOrder.size());
        this->scopesInOrder.push_back(scopeOrder{/*scope=*/scope, /*loc=*/loc});
        return scopeIndex;
    }

    // Pops the current scope during the parse pass. Applies "ContainsDirectEval"
    // rules: if the scope contains a direct eval(), all symbols within it are
    // marked kMustNotBeRenamed (unless bundling at top-level in ESM, where the
    // constraint is relaxed because eval-referenced symbols can't be guaranteed
    // to match after scope hoisting).
    void Parser::popScope() {
        // We cannot rename anything inside a scope containing a direct eval() call
        if (this->currentScope->contains_direct_eval) {
            for (auto &entry : this->currentScope->members) {
                // Using direct eval when bundling is not a good idea in general because
                // guchho must assume that it can potentially reach anything in any of
                // the containing scopes. We try to make it work but this isn't possible
                // in some cases.
                //
                // For example, symbols imported using an ESM import are a live binding
                // to the underlying symbol in another file. This is emulated during
                // scope hoisting by erasing the ESM import and just referencing the
                // underlying symbol in the flattened bundle directly. However, that
                // symbol may have a different name which could break uses of direct
                // eval:
                //
                //   // Before bundling
                //   import { foo as bar } from './foo.js'
                //   console.log(eval('bar'))
                //
                //   // After bundling
                //   let foo = 123 // The contents of "foo.js"
                //   console.log(eval('bar'))
                //
                // There really isn't any way to fix this. You can't just rename "foo" to
                // "bar" in the example above because there may be a third bundled file
                // that also contains direct eval and imports the same symbol with a
                // different conflicting import alias. And there is no way to store a
                // live binding to the underlying symbol in a variable with the import's
                // name so that direct eval can access it:
                //
                //   // After bundling
                //   let foo = 123 // The contents of "foo.js"
                //   const bar = /* cannot express a live binding to "foo" here */
                //   console.log(eval('bar'))
                //
                // Technically a "with" statement could potentially make this work (with
                // a big hit to performance), but they are deprecated and are unavailable
                // in strict mode. This is a non-starter since all ESM code is strict mode.
                //
                // So while we still try to obey the requirement that all symbol names are
                // pinned when direct eval is present, we make an exception for top-level
                // symbols in an ESM file when bundling is enabled. We make no guarantee
                // that "eval" will be able to reach these symbols and we allow them to be
                // renamed or removed by tree shaking.
                if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kBundle && this->currentScope->parent == nullptr && this->isFileConsideredESM) {
                    continue;
                }

                this->symbols[entry.second.ref.inner_index].flags = this->symbols[entry.second.ref.inner_index].flags | compiler::SymbolFlags::kMustNotBeRenamed;
            }
        }

        this->currentScope = this->currentScope->parent;
    }

    // Pops and discards a scope that was created during a speculative parse.
    // Unwinds any child scopes that were created after the given index, removes
    // them from their parent's children list, and truncates scopesInOrder to
    // pretend they were never created.
    void Parser::popAndDiscardScope(int scopeIndex) {
        // Unwind any newly-added scopes in reverse order
        for (int i = static_cast<int>(this->scopesInOrder.size()) - 1; i >= scopeIndex; i--) {
            Scope *scope = this->scopesInOrder[static_cast<size_t>(i)].scope;
            Scope *parent = scope->parent;
            int last = static_cast<int>(parent->children.size()) - 1;
            if (parent->children[static_cast<size_t>(last)] != scope) {
                throw std::runtime_error("Internal error");
            }
            parent->children.pop_back();
        }

        // Move up to the parent scope
        this->currentScope = this->currentScope->parent;

        // Truncate the scope order where we started to pretend we never saw this scope
        this->scopesInOrder.resize(static_cast<size_t>(scopeIndex));
    }

    // Pops the current scope and flattens it into its parent. The scope itself
    // is removed from the scope tree, but its child scopes are reparented to the
    // parent scope. This is used when a scope (e.g., a single-statement if body)
    // doesn't need to exist as a separate scope node in the final AST.
    void Parser::popAndFlattenScope(int scopeIndex) {
        // Move up to the parent scope
        Scope *toFlatten = this->currentScope;
        Scope *parent = toFlatten->parent;
        this->currentScope = parent;

        // Erase this scope from the order. This will shift over the indices of all
        // the scopes that were created after us. However, we shouldn't have to
        // worry about other code with outstanding scope indices for these scopes.
        // These scopes were all created in between this scope's push and pop
        // operations, so they should all be child scopes and should all be popped
        // by the time we get here.
        std::copy(this->scopesInOrder.begin() + static_cast<long>(scopeIndex) + 1, this->scopesInOrder.end(), this->scopesInOrder.begin() + static_cast<long>(scopeIndex));
        this->scopesInOrder.pop_back();

        // Remove the last child from the parent scope
        int last = static_cast<int>(parent->children.size()) - 1;
        if (parent->children[static_cast<size_t>(last)] != toFlatten) {
            throw std::runtime_error("Internal error");
        }
        parent->children.pop_back();

        // Reparent our child scopes into our parent
        for (Scope *scope : toFlatten->children) {
            scope->parent = parent;
            parent->children.push_back(scope);
        }
    }

    // Discards all scopes pushed after the given index, cleaning up the scope
    // tree by removing direct child scopes from the current scope and truncating
    // scopesInOrder. Used to undo scopes during backtracking when the parser
    // speculatively parses and then decides to try an alternative path.
    //
    // Precondition: the scope stack must be at the same depth as when the
    // scope at scopeIndex was pushed.
    void Parser::discardScopesUpTo(int scopeIndex) {
        // Remove any direct children from their parent
        std::vector<Scope *> children = this->currentScope->children;
        for (size_t i = static_cast<size_t>(scopeIndex); i < this->scopesInOrder.size(); i++) {
            Scope *child = this->scopesInOrder[i].scope;
            if (child->parent == this->currentScope) {
                for (int j = static_cast<int>(children.size()) - 1; j >= 0; j--) {
                    if (children[static_cast<size_t>(j)] == child) {
                        children.erase(children.begin() + j);
                        break;
                    }
                }
            }
        }
        this->currentScope->children = children;

        // Truncate the scope order where we started to pretend we never saw this scope
        this->scopesInOrder.resize(static_cast<size_t>(scopeIndex));
    }

    // Allocates a new compiler symbol with the given kind and name. The symbol
    // is appended to the symbols vector and its ref is returned. The symbol is
    // not yet declared in any scope — that happens in declareSymbol().
    //
    // When TypeScript is enabled, a corresponding use-count entry is also
    // allocated for accurate type-only import elimination.
    compiler::Ref Parser::newSymbol(compiler::SymbolKind kind, const std::string &name) {
        compiler::Ref ref;
        ref.source_index = this->source.index;
        ref.inner_index = static_cast<uint32_t>(this->symbols.size());
        compiler::Symbol symbol;
        symbol.kind = kind;
        symbol.original_name = name;
        symbol.link = compiler::kInvalidRef;
        this->symbols.push_back(symbol);
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            this->tsUseCounts.push_back(0);
        }
        return ref;
    }

    // Merges two symbols that refer to the same logical declaration. This handles
    // cases where hoisting causes two symbol declarations to be linked (e.g., a
    // function declaration and a var with the same name). The old symbol is linked
    // to the new one, and the new symbol inherits the old symbol's contents.
    //
    // Input:  old = ref_to_foo_1, new = ref_to_foo_2
    // Output: foo_1.link = foo_2, foo_2 gets merged contents
    compiler::Ref Parser::mergeSymbols(compiler::Ref old, compiler::Ref new_) {
        if (old == new_) {
            return new_;
        }

        compiler::Symbol *oldSymbol = &this->symbols[old.inner_index];
        if (oldSymbol->link != compiler::kInvalidRef) {
            oldSymbol->link = this->mergeSymbols(oldSymbol->link, new_);
            return oldSymbol->link;
        }

        compiler::Symbol *newSymbol_ = &this->symbols[new_.inner_index];
        if (newSymbol_->link != compiler::kInvalidRef) {
            newSymbol_->link = this->mergeSymbols(old, newSymbol_->link);
            return newSymbol_->link;
        }

        oldSymbol->link = new_;
        newSymbol_->MergeContentsWith(*oldSymbol);
        return new_;
    }

    // Determines whether two symbol declarations can coexist in the same scope.
    // Returns a mergeResult indicating the resolution strategy:
    //   - mergeForbidden: emit an error (e.g., two conflicting let declarations)
    //   - mergeKeepExisting: discard the new declaration, keep the old one
    //   - mergeReplaceWithNew: replace the old with the new (e.g., var + function)
    //   - mergeBecomePrivateGetSetPair: merge getter and setter into a pair
    //   - mergeOverwriteWithNew: overwrite the old binding with the new
    //
    // This encodes the full set of JavaScript/TypeScript declaration merging rules,
    // including hoisted functions, var declarations, private get/set pairs,
    // catch identifiers, arguments, and TypeScript enum/namespace merging.
    mergeResult Parser::canMergeSymbols(Scope *scope, compiler::SymbolKind existing, compiler::SymbolKind new_) {
        if (existing == compiler::SymbolKind::kUnbound) {
            return mergeReplaceWithNew;
        }

        // In TypeScript, imports are allowed to silently collide with symbols within
        // the module. Presumably this is because the imports may be type-only:
        //
        //   import {Foo} from 'bar'
        //   class Foo {}
        //
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse && existing == compiler::SymbolKind::kImport) {
            return mergeReplaceWithNew;
        }

        // "enum Foo {} enum Foo {}"
        if (new_ == compiler::SymbolKind::kTSEnum && existing == compiler::SymbolKind::kTSEnum) {
            return mergeKeepExisting;
        }

        // "namespace Foo { ... } enum Foo {}"
        if (new_ == compiler::SymbolKind::kTSEnum && existing == compiler::SymbolKind::kTSNamespace) {
            return mergeReplaceWithNew;
        }

        // "namespace Foo { ... } namespace Foo { ... }"
        // "function Foo() {} namespace Foo { ... }"
        // "enum Foo {} namespace Foo { ... }"
        if (new_ == compiler::SymbolKind::kTSNamespace) {
            switch (existing) {
            case compiler::SymbolKind::kTSNamespace:
            case compiler::SymbolKind::kHoistedFunction:
            case compiler::SymbolKind::kGeneratorOrAsyncFunction:
            case compiler::SymbolKind::kTSEnum:
            case compiler::SymbolKind::kClass:
                return mergeKeepExisting;
            default:
                break;
            }
        }

        // "var foo; var foo;"
        // "var foo; function foo() {}"
        // "function foo() {} var foo;"
        // "function *foo() {} function *foo() {}" but not "{ function *foo() {} function *foo() {} }"
        if (compiler::SymbolKindIsHoistedOrFunction(new_) && compiler::SymbolKindIsHoistedOrFunction(existing) &&
            (scope->kind == ScopeKind::kEntry ||
                scope->kind == ScopeKind::kFunctionBody ||
                scope->kind == ScopeKind::kFunctionArgs ||
                (new_ == existing && compiler::SymbolKindIsHoisted(new_)))) {
            return mergeReplaceWithNew;
        }

        // "get #foo() {} set #foo() {}"
        // "set #foo() {} get #foo() {}"
        if ((existing == compiler::SymbolKind::kPrivateGet && new_ == compiler::SymbolKind::kPrivateSet) ||
            (existing == compiler::SymbolKind::kPrivateSet && new_ == compiler::SymbolKind::kPrivateGet)) {
            return mergeBecomePrivateGetSetPair;
        }
        if ((existing == compiler::SymbolKind::kPrivateStaticGet && new_ == compiler::SymbolKind::kPrivateStaticSet) ||
            (existing == compiler::SymbolKind::kPrivateStaticSet && new_ == compiler::SymbolKind::kPrivateStaticGet)) {
            return mergeBecomePrivateStaticGetSetPair;
        }

        // "try {} catch (e) { var e }"
        if (existing == compiler::SymbolKind::kCatchIdentifier && new_ == compiler::SymbolKind::kHoisted) {
            return mergeReplaceWithNew;
        }

        // "function() { var arguments }"
        if (existing == compiler::SymbolKind::kArguments && new_ == compiler::SymbolKind::kHoisted) {
            return mergeKeepExisting;
        }

        // "function() { let arguments }"
        if (existing == compiler::SymbolKind::kArguments && new_ != compiler::SymbolKind::kHoisted) {
            return mergeOverwriteWithNew;
        }

        return mergeForbidden;
    }

    // Emits a "symbol already declared" error with a note pointing to the
    // original declaration location. Called when canMergeSymbols returns
    // mergeForbidden.
    void Parser::addSymbolAlreadyDeclaredError(const std::string &name, logger::Loc newLoc, logger::Loc oldLoc) {
        this->log.AddErrorWithNotes(&this->tracker,
            RangeOfIdentifier(this->source, newLoc),
            logger::FormatMsg(logger::MsgCat::kJS_SymbolAlreadyDeclared_2, name),
            {this->tracker.MakeMsgData(
                RangeOfIdentifier(this->source, oldLoc),
                logger::FormatMsg(logger::MsgCat::kJS_SymbolOriginalDeclared_2, name))});
    }

    // Declares a symbol in the current scope, handling name collisions by
    // consulting canMergeSymbols(). If the name already exists, the resolution
    // depends on the merge result (keep, replace, merge, or error).
    //
    // When minifying and two function declarations with the same name are merged,
    // the overwritten declaration is flagged for removal.
    //
    // Returns a Ref that points to the surviving symbol.
    compiler::Ref Parser::declareSymbol(compiler::SymbolKind kind, logger::Loc loc, const std::string &name) {
        this->checkForUnrepresentableIdentifier(loc, name);

        // Allocate a new symbol
        compiler::Ref ref = this->newSymbol(kind, name);

        // Check for a collision in the declaring scope
        auto existingIt = this->currentScope->members.find(name);
        if (existingIt != this->currentScope->members.end()) {
            compiler::Symbol *symbol = &this->symbols[existingIt->second.ref.inner_index];

            switch (this->canMergeSymbols(this->currentScope, symbol->kind, kind)) {
            case mergeForbidden:
                this->addSymbolAlreadyDeclaredError(name, loc, existingIt->second.loc);
                return existingIt->second.ref;

            case mergeKeepExisting:
                ref = existingIt->second.ref;
                break;

            case mergeReplaceWithNew:
                symbol->link = ref;
                this->currentScope->replaced.push_back(existingIt->second);

                // If these are both functions, remove the overwritten declaration
                if (this->options.optionsThatSupportStructuralEquality.minifySyntax && compiler::SymbolKindIsFunction(kind) && compiler::SymbolKindIsFunction(symbol->kind)) {
                    symbol->flags = symbol->flags | compiler::SymbolFlags::kRemoveOverwrittenFunctionDeclaration;
                }
                break;

            case mergeBecomePrivateGetSetPair:
                ref = existingIt->second.ref;
                symbol->kind = compiler::SymbolKind::kPrivateGetSetPair;
                break;

            case mergeBecomePrivateStaticGetSetPair:
                ref = existingIt->second.ref;
                symbol->kind = compiler::SymbolKind::kPrivateStaticGetSetPair;
                break;

            case mergeOverwriteWithNew:
                break;
            }
        }

        // Overwrite this name in the declaring scope
        this->currentScope->members[name] = ScopeMember{/*ref=*/ref, /*loc=*/loc};
        return ref;
    }

    // Records a symbol usage reference. Increments use count estimates (skipped
    // in dead code regions for optimization purposes). Also maintains separate
    // TypeScript use counts that include dead code, since accurate counts are
    // needed for TypeScript-to-JavaScript conversion (e.g., removing type-only
    // imports).
    void Parser::recordUsage(compiler::Ref ref) {
        // The use count stored in the symbol is used for generating symbol names
        // during minification. These counts shouldn't include references inside dead
        // code regions since those will be culled.
        if (!this->isControlFlowDead) {
            this->symbols[ref.inner_index].use_count_estimate++;
            SymbolUse use = this->symbolUses[ref];
            use.count_estimate++;
            this->symbolUses[ref] = use;
        }

        // The correctness of TypeScript-to-JavaScript conversion relies on accurate
        // symbol use counts for the whole file, including dead code regions. This is
        // tracked separately in a parser-only data structure.
        if (this->options.optionsThatSupportStructuralEquality.ts.Parse) {
            this->tsUseCounts[ref.inner_index]++;
        }
    }

    // Walks the left-hand side of a dot/bracket chain to find and ignore the
    // base identifier. This is used when a property access like `obj.method` is
    // passed as a value — the `obj` reference is counted but `method` is not,
    // since it's a property name, not a variable reference.
    void Parser::ignoreUsageOfIdentifierInDotChain(Expr expr) {
        for (;;) {
            if (const EIdentifier *e = Get<EIdentifier>(expr.data); e != nullptr) {
                this->ignoreUsage(e->ref);
                return;
            }

            if (const EDot *e = Get<EDot>(expr.data); e != nullptr) {
                expr = e->target;
                continue;
            }

            if (const EIndex *e = Get<EIndex>(expr.data); e != nullptr) {
                if (Get<EString>(e->index.data) != nullptr) {
                    expr = e->target;
                    continue;
                }
            }

            return;
        }
    }

    // Generates an expression that references a runtime helper function via an
    // import statement. If the helper hasn't been imported yet, a new symbol and
    // import record are created. The generated import is later hoisted to the top
    // of the output file by toAST().
    //
    // Input:  name = "__objectSpread"
    // Output: EIdentifier referencing the runtime import symbol for "__objectSpread"
    Expr Parser::importFromRuntime(logger::Loc loc, const std::string &name) {
        auto it = this->runtimeImports.find(name);
        if (it == this->runtimeImports.end()) {
            compiler::LocRef locRef;
            locRef.loc = loc;
            locRef.ref = this->newSymbol(compiler::SymbolKind::kOther, name);
            this->moduleScope->generated.push_back(locRef.ref);
            this->runtimeImports[name] = locRef;
        }
        this->recordUsage(this->runtimeImports[name].ref);
        EIdentifier *identifier = new EIdentifier();
        identifier->ref = this->runtimeImports[name].ref;
        return Expr{std::shared_ptr<EIdentifier>(identifier), loc};
    }

    // Generates a function call expression to a runtime helper.
    // Shorthand for importFromRuntime() followed by creating an ECall node.
    //
    // Input:  name = "__export", args = [ref, getter]
    // Output: ECall{target: __export, args: [ref, getter]}
    Expr Parser::callRuntime(logger::Loc loc, const std::string &name, std::vector<Expr> &args) {
        ECall *call = new ECall();
        call->target = this->importFromRuntime(loc, name);
        call->args = args;
        return Expr{std::shared_ptr<ECall>(call), loc};
    }

    // Generates an import for a JSX runtime function (jsx, jsxs, Fragment,
    // createElement). Selects the appropriate import source based on the JSX
    // transform mode (automatic vs. legacy) and dev vs. prod.
    //
    // Input:  JSXImportJSX in automatic mode → imports "jsx" from "react/jsx-runtime"
    // Input:  JSXImportCreateElement         → imports "createElement" from "react"
    Expr Parser::importJSXSymbol(logger::Loc loc, JSXImport jsx) {
        std::unordered_map<std::string, compiler::LocRef> *symbol_map;
        std::string name;

        switch (jsx) {
        case JSXImportJSX:
            symbol_map = &this->jsxRuntimeImports;
            if (this->options.jsx.Development) {
                name = "jsxDEV";
            } else {
                name = "jsx";
            }
            break;

        case JSXImportJSXS:
            symbol_map = &this->jsxRuntimeImports;
            if (this->options.jsx.Development) {
                name = "jsxDEV";
            } else {
                name = "jsxs";
            }
            break;

        case JSXImportFragment:
            symbol_map = &this->jsxRuntimeImports;
            name = "Fragment";
            break;

        case JSXImportCreateElement:
            symbol_map = &this->jsxLegacyImports;
            name = "createElement";
            break;

        default:
            throw std::runtime_error("Internal error");
        }

        auto it = symbol_map->find(name);
        if (it == symbol_map->end()) {
            compiler::LocRef locRef;
            locRef.loc = loc;
            locRef.ref = this->newSymbol(compiler::SymbolKind::kOther, name);
            this->moduleScope->generated.push_back(locRef.ref);
            this->isImportItem[locRef.ref] = true;
            (*symbol_map)[name] = locRef;
        }

        this->recordUsage((*symbol_map)[name].ref);
        std::shared_ptr<EIdentifier> identifier = std::make_shared<EIdentifier>();
        identifier->ref = (*symbol_map)[name].ref;
        return this->handleIdentifier(loc, identifier, identifierOpts{/*assignTarget=*/AssignTarget::kNone, /*isCallTarget=*/false, /*isDeleteTarget=*/false, /*preferQuotedKey=*/false, /*wasOriginallyIdentifier=*/true, /*matchAgainstDefines=*/true});
    }

    // Returns the expression to substitute for a `require()` call. In bundle mode
    // for non-ESM output, this generates a call to the runtime __require helper.
    // Otherwise, it returns the raw `require` identifier for CommonJS compatibility.
    Expr Parser::valueToSubstituteForRequire(logger::Loc loc) {
        if (this->source.index != 0xFFFFFFFF && config::ShouldCallRuntimeRequire(this->options.optionsThatSupportStructuralEquality.mode, this->options.optionsThatSupportStructuralEquality.outputFormat)) {
            return this->importFromRuntime(loc, "__require");
        }

        this->recordUsage(this->requireRef);
        EIdentifier *identifier = new EIdentifier();
        identifier->ref = this->requireRef;
        return Expr{std::shared_ptr<EIdentifier>(identifier), loc};
    }

    // Lazily creates a reference to the global Promise constructor.
    // Used when lowering async/await to generate polyfill code.
    compiler::Ref Parser::makePromiseRef() {
        if (this->promiseRef == compiler::kInvalidRef) {
            this->promiseRef = this->newSymbol(compiler::SymbolKind::kUnbound, "Promise");
        }
        return this->promiseRef;
    }

    // Lazily creates a reference to the global RegExp constructor.
    // Used when lowering regex literals for target environments that need a polyfill.
    compiler::Ref Parser::makeRegExpRef() {
        if (this->regExpRef == compiler::kInvalidRef) {
            this->regExpRef = this->newSymbol(compiler::SymbolKind::kUnbound, "RegExp");
            this->moduleScope->generated.push_back(this->regExpRef);
        }
        return this->regExpRef;
    }

    // Lazily creates a reference to the global BigInt constructor.
    // Used when lowering BigInt literals for target environments that don't
    // support native BigInt.
    compiler::Ref Parser::makeBigIntRef() {
        if (this->bigIntRef == compiler::kInvalidRef) {
            this->bigIntRef = this->newSymbol(compiler::SymbolKind::kUnbound, "BigInt");
            this->moduleScope->generated.push_back(this->bigIntRef);
        }
        return this->bigIntRef;
    }

    // Stores a name string temporarily in a Ref for later retrieval. Two encoding
    // strategies are used depending on whether the name is a substring of the
    // source file:
    //
    // 1. Substring (common case): stored as (negative_source_index, start_offset).
    //    The negative source_index encodes the length. No allocation needed.
    //
    // 2. External string (rare): stored as (0x80000000, index_into_allocatedNames).
    //    The actual string is stored in the allocatedNames vector.
    //
    // These refs are recovered later by loadNameFromRef() during scope traversal.
    compiler::Ref Parser::storeNameInRef(MaybeSubstring name) {
        // Is the data in "name" a subset of the data in "p.source.Contents"?
        if (name.IsSubstring()) {
            // The name is a slice of the file contents, so we can just reference it by
            // length and don't have to allocate anything. This is the common case.
            //
            // It's stored as a negative value so we'll crash if we try to use it. That
            // way we'll catch cases where we've forgotten to call loadNameFromRef().
            // The length is the negative part because we know it's non-zero.
            compiler::Ref ref;
            ref.source_index = -static_cast<int32_t>(name.str.size());
            ref.inner_index = name.start;
            return ref;
        }
        // The name is some memory allocated elsewhere. This is either an inline
        // string constant in the parser or an identifier with escape sequences
        // in the source code, which is very unusual. Stash it away for later.
        // This uses allocations but it should hopefully be very uncommon.
        compiler::Ref ref;
        ref.source_index = 0x80000000;
        ref.inner_index = static_cast<uint32_t>(this->allocatedNames.size());
        this->allocatedNames.push_back(name.str);
        return ref;
    }

    // Inverse of storeNameInRef(). Recovers the original name string from a
    // Ref that was created by storeNameInRef(). See that function for the
    // encoding details.
    std::string Parser::loadNameFromRef(compiler::Ref ref) {
        if (ref.source_index == 0x80000000) {
            return this->allocatedNames[ref.inner_index];
        }
        if ((ref.source_index & 0x80000000) == 0) {
            throw std::runtime_error("Internal error: invalid symbol reference");
        }
        return this->source.contents.substr(
            ref.inner_index, static_cast<size_t>(static_cast<int32_t>(0) - static_cast<int32_t>(ref.source_index)));
    }

    // Logs deferred errors that were accumulated during expression parsing.
    // These include invalid default value syntax (=== in pattern), unexpected
    // tokens after the ternary ?, and unsupported array spread syntax.
    void Parser::logExprErrors(deferredErrors *errors) {
        if (errors->invalidExprDefaultValue.len > 0) {
            this->log.AddError(&this->tracker, errors->invalidExprDefaultValue, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedEquals));
        }

        if (errors->invalidExprAfterQuestion.len > 0) {
            logger::Range r = errors->invalidExprAfterQuestion;
            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_UnexpectedToken, this->source.contents.substr(static_cast<size_t>(r.loc.start), static_cast<size_t>(r.len))));
        }

        if (errors->arraySpreadFeature.len > 0) {
            this->markSyntaxFeature(compat::JSFeature::kArraySpread, errors->arraySpreadFeature);
        }
    }

    // Logs deferred errors from arrow function argument parsing. These are
    // parenthesized expressions that turned out to be invalid as bindings
    // (e.g., destructuring with invalid syntax). The errors are only logged
    // once the arrow function body is confirmed.
    void Parser::logDeferredArrowArgErrors(deferredErrors *errors) {
        for (logger::Range &paren : errors->invalidParens) {
            this->log.AddError(&this->tracker, paren, logger::FormatMsg(logger::MsgCat::kJS_InvalidBindingPattern));
        }
    }

    // Logs an error when the nullish coalescing operator (??) is mixed with
    // other logical operators (&&, ||, ??=) without explicit parenthesization.
    // The spec requires parentheses to avoid ambiguous precedence.
    void Parser::logNullishCoalescingErrorPrecedenceError(const std::string &op) {
        std::string prevOp = "??";
        if (this->lexer.token == T::kQuestionQuestion) {
            std::string tmp = op;
            const_cast<std::string&>(op) = prevOp;
            prevOp = tmp;
        }
        this->log.AddErrorWithNotes(&this->tracker, this->lexer.Range(),
            logger::FormatMsg(logger::MsgCat::kJS_OperatorsWithoutParens, op, prevOp),
            {logger::MsgData{nullptr, nullptr,
                logger::FormatMsg(logger::MsgCat::kJS_OperatorsWithoutParensNote, prevOp, op, prevOp, op, prevOp, op)}});
    }

    // Logs arrow function argument errors (await/yield in parameter defaults).
    // These are deferred until the arrow body is confirmed to avoid false
    // positives when the expression is not actually an arrow function.
    void Parser::logArrowArgErrors(deferredArrowArgErrors *errors) {
        if (errors->invalidExprAwait.len > 0) {
            this->log.AddError(&this->tracker, errors->invalidExprAwait, logger::FormatMsg(logger::MsgCat::kJS_AwaitExpressionHere));
        }

        if (errors->invalidExprYield.len > 0) {
            this->log.AddError(&this->tracker, errors->invalidExprYield, logger::FormatMsg(logger::MsgCat::kJS_YieldExpressionHere));
        }
    }

    // Marks a syntax feature as unsupported by the target environment and emits
    // a diagnostic. Returns true if an error was generated.
    //
    // For features that can be polyfilled (destructuring, for-of, etc.), emits
    // an error with a note explaining the target environment limitation. For
    // features that cannot be polyfilled (BigInt, import.meta), emits a warning
    // or debug message. Top-level await gets a format-specific error.
    bool Parser::markSyntaxFeature(compat::JSFeature feature, logger::Range r) {
        bool didGenerateError = true;

        if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, feature)) {
            if (feature == compat::JSFeature::kTopLevelAwait && !config::FormatAllowTopLevelAwait(this->options.optionsThatSupportStructuralEquality.outputFormat)) {
                this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_TopLevelAwaitNotSupported, std::string(config::FormatToString(this->options.optionsThatSupportStructuralEquality.outputFormat))));
                return didGenerateError;
            }

            didGenerateError = false;
            return didGenerateError;
        }

        std::string name;
        std::string where;
        std::vector<logger::MsgData> notes;
        std::tie(where, notes) = prettyPrintTargetEnvironment(feature, this->options.optionsThatSupportStructuralEquality);

        switch (feature) {
        case compat::JSFeature::kDefaultArgument:
            name = "default arguments";
            break;

        case compat::JSFeature::kRestArgument:
            name = "rest arguments";
            break;

        case compat::JSFeature::kArraySpread:
            name = "array spread";
            break;

        case compat::JSFeature::kForOf:
            name = "for-of loops";
            break;

        case compat::JSFeature::kObjectAccessors:
            name = "object accessors";
            break;

        case compat::JSFeature::kObjectExtensions:
            name = "object literal extensions";
            break;

        case compat::JSFeature::kDestructuring:
            name = "destructuring";
            break;

        case compat::JSFeature::kNewTarget:
            name = "new.target";
            break;

        case compat::JSFeature::kConstAndLet:
            name = this->source.TextForRange(r);
            break;

        case compat::JSFeature::kClass:
            name = "class syntax";
            break;

        case compat::JSFeature::kGenerator:
            name = "generator functions";
            break;

        case compat::JSFeature::kAsyncAwait:
            name = "async functions";
            break;

        case compat::JSFeature::kAsyncGenerator:
            name = "async generator functions";
            break;

        case compat::JSFeature::kForAwait:
            name = "for-await loops";
            break;

        case compat::JSFeature::kNestedRestBinding:
            name = "non-identifier array rest patterns";
            break;

        case compat::JSFeature::kDecorators:
            name = "JavaScript decorators";
            break;

        case compat::JSFeature::kImportAttributes:
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_ArbitraryImportSecondArg, where), notes);
            return didGenerateError;

        case compat::JSFeature::kTopLevelAwait:
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_TopLevelAwaitNotAvailable, where), notes);
            return didGenerateError;

        case compat::JSFeature::kImportDefer:
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_DeferredImportsNotAvailable, where), notes);
            return didGenerateError;

        case compat::JSFeature::kImportSource:
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_SourcePhaseImportsNotAvailable, where), notes);
            return didGenerateError;

        case compat::JSFeature::kArbitraryModuleNamespaceNames:
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_StringNamespaceIdentifier, where), notes);
            return didGenerateError;

        case compat::JSFeature::kBigint:
            // This can't be polyfilled
            {
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode || this->fnOrArrowDataVisit.tryBodyCount > 0) {
                    kind = logger::MsgKind::kDebug;
                }
                this->log.AddIDWithNotes(logger::MsgID::kJS_BigInt, kind, &this->tracker, r,
                    logger::FormatMsg(logger::MsgCat::kJS_BigIntNotAvailable, where), notes);
            }
            return didGenerateError;

        case compat::JSFeature::kImportMeta:
            // This can't be polyfilled
            {
                logger::MsgKind kind = logger::MsgKind::kWarning;
                if (this->suppressWarningsAboutWeirdCode || this->fnOrArrowDataVisit.tryBodyCount > 0) {
                    kind = logger::MsgKind::kDebug;
                }
                this->log.AddIDWithNotes(logger::MsgID::kJS_EmptyImportMeta, kind, &this->tracker, r,
                    logger::FormatMsg(logger::MsgCat::kJS_ImportMetaNotAvailable_2, where), notes);
            }
            return didGenerateError;

        default:
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_FeatureNotAvailable, where), notes);
            return didGenerateError;
        }

        this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_TransformingNotSupported, name, where), notes);
        return didGenerateError;
    }

    // Returns true if the current scope is in strict mode.
    // Strict mode is inherited from parent scopes and can be enabled by:
    // - "use strict" directive
    // - ES module context
    // - class body (implicit)
    // - TypeScript "alwaysStrict" setting
    // - JSX automatic runtime (implicit module)
    bool Parser::isStrictMode() {
        return this->currentScope->strict_mode != StrictModeKind::kSloppyMode;
    }

    // Reports a strict mode violation. In strict mode, emits an error. In
    // sloppy mode with ESM output, also emits an error since all ESM code is
    // implicitly strict. For features that can be transformed (like for-in var
    // initializers), the error is suppressed in sloppy mode.
    void Parser::markStrictModeFeature(strictModeFeature feature, logger::Range r, const std::string &detail) {
        std::string text;
        bool canBeTransformed = false;

        switch (feature) {
        case strictModeFeature::withStatement:
            text = "With statements";
            break;

        case strictModeFeature::deleteBareName:
            text = "Delete of a bare identifier";
            break;

        case strictModeFeature::forInVarInit:
            text = "Variable initializers inside for-in loops";
            canBeTransformed = true;
            break;

        case strictModeFeature::evalOrArguments:
            text = "Declarations with the name \"" + detail + "\"";
            break;

        case strictModeFeature::reservedWord:
            text = "\"" + detail + "\" is a reserved word and";
            break;

        case strictModeFeature::legacyOctalLiteral:
            text = "Legacy octal literals";
            break;

        case strictModeFeature::legacyOctalEscape:
            text = "Legacy octal escape sequences";
            break;

        case strictModeFeature::ifElseFunctionStmt:
            text = "Function declarations inside if statements";
            break;

        case strictModeFeature::labelFunctionStmt:
            text = "Function declarations inside labels";
            break;

        default:
            text = "This feature";
            break;
        }

        if (this->isStrictMode()) {
            std::string where;
            std::vector<logger::MsgData> notes;
            std::tie(where, notes) = this->whyStrictMode(this->currentScope);
            this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_FeatureCannotBeUsedWhere, text, where), notes);
        } else if (!canBeTransformed && this->options.optionsThatSupportStructuralEquality.outputFormat == config::Format::kESModule) {
            this->log.AddError(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_FeatureCannotBeUsedWithESM, text));
        }
    }

    // Explains why the given scope is in strict mode. Returns a human-readable
    // description and diagnostic notes pointing to the triggering source location.
    // This is used for error messages that need to explain why strict mode applies.
    std::pair<std::string, std::vector<logger::MsgData>> Parser::whyStrictMode(Scope *scope) {
        std::string where = "in strict mode";

        switch (scope->strict_mode) {
        case StrictModeKind::kImplicitStrictModeClass:
            return {where, std::vector<logger::MsgData>{this->tracker.MakeMsgData(this->enclosingClassKeyword,
                "All code inside a class is implicitly in strict mode")}};

        case StrictModeKind::kImplicitStrictModeTSAlwaysStrict:
            {
                config::TSAlwaysStrict *tsAlwaysStrict = this->options.tsAlwaysStrict;
                logger::LineColumnTracker t(&tsAlwaysStrict->SourceData);
                return {where, std::vector<logger::MsgData>{t.MakeMsgData(tsAlwaysStrict->RangeData,
                    "TypeScript's \"" + tsAlwaysStrict->Name + "\" setting was enabled here:")}};
            }

        case StrictModeKind::kImplicitStrictModeJSXAutomaticRuntime:
            return {where, std::vector<logger::MsgData>{
                this->tracker.MakeMsgData(logger::Range{logger::Loc{this->firstJSXElementLoc.start}, 1},
                    "This file is implicitly in strict mode due to the JSX element here:"),
                logger::MsgData{nullptr, nullptr,
                    "When React's \"automatic\" JSX transform is enabled, using a JSX element automatically inserts "
                    "an \"import\" statement at the top of the file for the corresponding the JSX helper function. "
                    "This means the file is considered an ECMAScript module, and all ECMAScript modules use strict mode."}}};

        case StrictModeKind::kExplicitStrictMode:
            return {where, std::vector<logger::MsgData>{this->tracker.MakeMsgData(this->source.RangeOfString(scope->use_strict_loc),
                "Strict mode is triggered by the \"use strict\" directive here:")}};

        case StrictModeKind::kImplicitStrictModeESM:
            {
                std::pair<whyESM, std::vector<logger::MsgData>> result = this->whyESModule();
                return {"in an ECMAScript module", result.second};
            }

        default:
            return {where, {}};
        }
    }

    // Marks an async function and checks whether async functions are supported
    // by the target environment. Async generators are only supported if the
    // target also supports generators (since async generators are implemented
    // in terms of generators).
    bool Parser::markAsyncFn(logger::Range asyncRange, bool isGenerator) {
        // Lowered async functions are implemented in terms of generators. So if
        // generators aren't supported, async functions aren't supported either.
        // But if generators are supported, then async functions are unconditionally
        // supported because we can use generators to implement them.
        if (!compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kGenerator)) {
            return false;
        }

        compat::JSFeature feature = compat::JSFeature::kAsyncAwait;
        if (isGenerator) {
            feature = compat::JSFeature::kAsyncGenerator;
        }
        return this->markSyntaxFeature(feature, asyncRange);
    }

    // Captures the `this` value into a lazily-created local variable `_this`.
    // Used when lowering arrow functions or async functions that reference `this`
    // from a nested non-arrow function context. The capture is only created once
    // per function and reused for all `this` references within it.
    compiler::Ref Parser::captureThis() {
        if (this->fnOnlyDataVisit.thisCaptureRef == nullptr) {
            compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kHoisted, "_this");
            this->fnOnlyDataVisit.thisCaptureRef = new compiler::Ref(ref);
        }

        compiler::Ref ref = *this->fnOnlyDataVisit.thisCaptureRef;
        this->recordUsage(ref);
        return ref;
    }

    // Captures the `arguments` object into a lazily-created local variable
    // `_arguments`. Used when lowering arrow functions that reference `arguments`
    // from their enclosing non-arrow function.
    compiler::Ref Parser::captureArguments() {
        if (this->fnOnlyDataVisit.argumentsCaptureRef == nullptr) {
            compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kHoisted, "_arguments");
            this->fnOnlyDataVisit.argumentsCaptureRef = new compiler::Ref(ref);
        }

        compiler::Ref ref = *this->fnOnlyDataVisit.argumentsCaptureRef;
        this->recordUsage(ref);
        return ref;
    }

    // Saves any comments that appeared before the current token as expression
    // comments. These are attached to the expression node at the current location
    // for source map and minification purposes. Returns the current location
    // for use as the expression's source location.
    logger::Loc Parser::saveExprCommentsHere() {
        logger::Loc loc = this->lexer.Loc();
        if (!this->lexer.comments_before_token.empty()) {
            std::vector<std::string> comments;
            comments.reserve(this->lexer.comments_before_token.size());
            for (logger::Range &comment : this->lexer.comments_before_token) {
                comments.push_back(this->source.CommentTextWithoutIndent(comment));
            }
            this->exprComments[loc] = comments;
            this->lexer.comments_before_token.clear();
        }
        return loc;
    }

    // Returns true if the current expression could be a binding pattern that
    // needs special parsing. This is used in for-loop initializers where the
    // same syntax can be an assignment or a declaration depending on what follows:
    //   - `=` → it's a destructuring assignment (e.g., `[a] = b`)
    //   - `in` (when not allowed) → it's a for-in declaration
    //   - `of` (when not allowed) → it's a for-of declaration
    bool Parser::willNeedBindingPattern() {
        switch (this->lexer.token) {
        case T::kEquals:
            // "[a] = b;"
            return true;

        case T::kIn:
            // "for ([a] in b) {}"
            return !this->allowIn;

        case T::kIdentifier:
            // "for ([a] of b) {}"
            return !this->allowIn && this->lexer.IsContextualKeyword("of");

        default:
            return false;
        }
    }

    // Validates that for-loop declarations follow the spec rules:
    // - For-in/for-of: at most one declaration, no initializer
    // - For-loop with var: allows initializers for simple identifiers
    // Emits appropriate errors for violations.
    void Parser::forbidInitializers(std::vector<Decl> decls, const std::string &loopType, bool isVar) {
        if (decls.size() > 1) {
            this->log.AddError(&this->tracker, logger::Range{logger::Loc{decls[0].binding.loc.start}},
                logger::FormatMsg(logger::MsgCat::kJS_ForLoopSingleDeclaration, loopType));
        } else if (decls.size() == 1 && !IsNil(decls[0].value_or_nil.data)) {
            if (isVar) {
                if (Get<BIdentifier>(decls[0].binding.data) != nullptr) {
                    // This is a weird special case. Initializers are allowed in "var"
                    // statements with identifier bindings.
                    return;
                }
            }
            this->log.AddError(&this->tracker, logger::Range{logger::Loc{decls[0].value_or_nil.loc.start}},
                logger::FormatMsg(logger::MsgCat::kJS_ForLoopNoInitializer, loopType));
        }
    }

    // Validates that a function name is legal in its context. Async functions
    // cannot be named "await" and generator function expressions cannot be
    // named "yield".
    void Parser::validateFunctionName(const Fn &fn, fnKind kind) {
        // Prevent the function name from being the same as a function-specific keyword
        if (fn.name != nullptr) {
            if (fn.is_async && this->symbols[fn.name->ref.inner_index].original_name == "await") {
                this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, fn.name->loc),
                    logger::FormatMsg(logger::MsgCat::kJS_AsyncFnNamedAwait));
            } else if (fn.is_generator && this->symbols[fn.name->ref.inner_index].original_name == "yield" && kind == fnKind::fnExpr) {
                this->log.AddError(&this->tracker, RangeOfIdentifier(this->source, fn.name->loc),
                    logger::FormatMsg(logger::MsgCat::kJS_GeneratorFnNamedYield));
            }
        }
    }

    // Validates that a declared symbol name is legal. In strict mode, rejects
    // reserved words and "eval"/"arguments" as binding names.
    void Parser::validateDeclaredSymbolName(logger::Loc loc, const std::string &name) {
        if (kStrictModeReservedWords.find(name) != kStrictModeReservedWords.end()) {
            this->markStrictModeFeature(strictModeFeature::reservedWord, RangeOfIdentifier(this->source, loc), name);
        } else if (isEvalOrArguments(name)) {
            this->markStrictModeFeature(strictModeFeature::evalOrArguments, RangeOfIdentifier(this->source, loc), name);
        }
    }

    // Warns when the deprecated "assert" keyword is used in import assertions.
    // Suggests replacing "assert" with "with" for the newer import attributes
    // syntax. Only emits a warning (not an error) when the target environment
    // supports import attributes but not the old assert syntax.
    void Parser::maybeWarnAboutAssertKeyword(logger::Loc loc) {
        if (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kImportAssertions) &&
            !compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kImportAttributes)) {
            std::string where = config::PrettyPrintTargetEnvironment(this->options.optionsThatSupportStructuralEquality.originalTargetEnv, this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatureOverridesMask);
            logger::Msg msg;
            msg.kind = logger::MsgKind::kWarning;
            msg.data = this->tracker.MakeMsgData(RangeOfIdentifier(this->source, loc), "The \"assert\" keyword is not supported in " + where);
            msg.notes = {logger::MsgData{nullptr, nullptr, "Did you mean to use \"with\" instead of \"assert\"?"}};
            msg.data.location->suggestion = "with";
            this->log.AddMsgID(logger::MsgID::kJS_AssertToWith, msg);
        }
    }

    // Emits an error when a lexical declaration (let, const, class) appears
    // directly inside an if/else/for/while without a block scope. The spec
    // forbids this to avoid the dangling-else ambiguity.
    void Parser::forbidLexicalDecl(logger::Loc loc) {
        logger::Range r = RangeOfIdentifier(this->source, loc);
        this->log.AddErrorWithNotes(&this->tracker, r, logger::FormatMsg(logger::MsgCat::kJS_DeclarationInSingleStatement),
            {{nullptr, nullptr, "Wrap this declaration in a block statement to use it here."}});
    }

    // Emits an error when using/await-using declarations appear inside a
    // switch case clause. They must be wrapped in a block statement.
    void Parser::forbidUsingInSwitch(logger::Loc loc) {
        this->log.AddErrorWithNotes(&this->tracker, RangeOfIdentifier(this->source, loc),
            logger::FormatMsg(logger::MsgCat::kJS_UsingInSwitchCase),
            {{nullptr, nullptr, "Wrap this declaration in a block statement to use it here."}});
    }

    // Returns true if the expression is a valid assignment target (left-hand
    // side of an assignment). Valid targets include:
    // - Identifiers (except eval/arguments in strict mode)
    // - Dot/bracket property access (excluding optional chaining)
    // - Object/array destructuring patterns (excluding parenthesized ones)
    bool Parser::isValidAssignmentTarget(Expr expr) {
        if (const EIdentifier *e = Get<EIdentifier>(expr.data); e != nullptr) {
            if (this->isStrictMode()) {
                if (std::string name = this->loadNameFromRef(e->ref); isEvalOrArguments(name)) {
                    return false;
                }
            }
            return true;
        }
        if (const EDot *e = Get<EDot>(expr.data); e != nullptr) {
            return e->optional_chain == OptionalChain::kNone;
        }
        if (const EIndex *e = Get<EIndex>(expr.data); e != nullptr) {
            return e->optional_chain == OptionalChain::kNone;
        }

        // Don't worry about recursive checking for objects and arrays. This will
        // already be handled naturally by passing down the assign target flag.
        if (const EObject *e = Get<EObject>(expr.data); e != nullptr) {
            return !e->is_parenthesized;
        }
        if (const EArray *e = Get<EArray>(expr.data); e != nullptr) {
            return !e->is_parenthesized;
        }
        return false;
    }

    // Generates a temporary variable reference for use during lowering passes.
    // The name is auto-generated based on a counter. The declare parameter
    // controls whether a variable declaration node should be created:
    //   - tempRefNeedsDeclare: creates a let/const declaration
    //   - tempRefNoDeclare: just creates the reference (declaration handled elsewhere)
    //   - tempRefNeedsDeclareMayBeCapturedInsideLoop: creates a let that may be
    //     hoisted out of a loop body
    //
    // The temporary is scoped to the nearest function/module boundary.
    compiler::Ref Parser::generateTempRef(generateTempRefArg declare, const std::string &optionalName) {
        Scope *scope = this->currentScope;

        if (declare != generateTempRefArg::tempRefNeedsDeclareMayBeCapturedInsideLoop) {
            while (!StopsHoisting(scope->kind)) {
                scope = scope->parent;
            }
        }

        std::string name = optionalName;
        if (name.empty()) {
            name = "_" + compiler::kDefaultNameMinifierJS.NumberToMinifiedName(this->tempRefCount);
            this->tempRefCount++;
        }
        compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kOther, name);

        if (declare == generateTempRefArg::tempRefNeedsDeclareMayBeCapturedInsideLoop && !StopsHoisting(scope->kind)) {
            this->tempLetsToDeclare.push_back(ref);
        } else if (declare != generateTempRefArg::tempRefNoDeclare) {
            this->tempRefsToDeclare.push_back(tempRef{/*valueOrNil=*/Expr{}, /*ref=*/ref});
        }

        scope->generated.push_back(ref);
        return ref;
    }

    // Generates a temporary reference scoped to the top-level module scope.
    // Unlike generateTempRef, this always creates a top-level declaration that
    // won't be affected by function scope boundaries.
    compiler::Ref Parser::generateTopLevelTempRef() {
        compiler::Ref ref = this->newSymbol(compiler::SymbolKind::kOther, "_" + compiler::kDefaultNameMinifierJS.NumberToMinifiedName(this->topLevelTempRefCount));
        this->topLevelTempRefsToDeclare.push_back(tempRef{/*valueOrNil=*/Expr{}, /*ref=*/ref});
        this->moduleScope->generated.push_back(ref);
        this->topLevelTempRefCount++;
        return ref;
    }

    // Records a symbol as declared in the current part for tree-shaking purposes.
    // Tracks whether the symbol is top-level (including hoisted symbols that were
    // moved up to module scope) so the linker can determine cross-part dependencies.
    void Parser::recordDeclaredSymbol(compiler::Ref ref) {
        bool is_top_level = this->currentScope == this->moduleScope;

        // Check whether this symbol was hoisted out of a nested scope into the module scope
        if (!is_top_level) {
            compiler::Symbol& symbol = this->symbols[ref.inner_index];
            if (compiler::SymbolKindIsHoisted(symbol.kind)) {
                if (auto it = this->moduleScope->members.find(symbol.original_name);
                    it != this->moduleScope->members.end() && it->second.ref == ref) {
                    is_top_level = true;
                }
            }
        }

        this->declaredSymbols.push_back(DeclaredSymbol{/*ref=*/ref, /*is_top_level=*/is_top_level});
    }

    // Adds an import record to the parser's import record list. Returns the index
    // of the new record. Import records track all import/require statements in the
    // file and are used by the linker to resolve dependencies.
    uint32_t Parser::addImportRecord(compiler::ImportKind kind, compiler::ImportPhase phase, logger::Range pathRange, const std::string &text, const std::shared_ptr<compiler::ImportAssertOrWith> &assertOrWith, compiler::ImportRecordFlags flags) {
        uint32_t index = static_cast<uint32_t>(this->importRecords.size());
        compiler::ImportRecord record;
        record.kind = kind;
        record.phase = phase;
        record.range = pathRange;
        record.path.text = text;
        record.assert_or_with = assertOrWith;
        record.flags = flags;
        this->importRecords.push_back(record);
        return index;
    }

    // Returns true if "using" or "await using" declarations in the given
    // statement list need to be lowered. This is determined by checking whether
    // the target environment supports the using declarations proposal natively,
    // or whether async support is required for await using.
    bool Parser::shouldLowerUsingDeclarations(std::vector<Stmt> stmts) {
        for (Stmt &stmt : stmts) {
            if (const SLocal *local = Get<SLocal>(stmt.data); local != nullptr &&
                ((local->kind == LocalKind::kUsing && compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kUsing)) ||
                    (local->kind == LocalKind::kAwaitUsing && (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kUsing) ||
                        compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncAwait) ||
                        (compat::Has(this->options.optionsThatSupportStructuralEquality.unsupportedJSFeatures, compat::JSFeature::kAsyncGenerator) && this->fnOrArrowDataVisit.isGenerator))))) {
                return true;
            }
        }
        return false;
    }

    // Checks whether a TypeScript arrow function expression is actually a JSX
    // element. In TypeScript + JSX mode, `<T>` can be either a type parameter
    // or a JSX element. This function peeks ahead to disambiguate by looking
    // for patterns like `<const T, ...>` (arrow fn) vs `<Component ...>` (JSX).
    bool Parser::isTSArrowFnJSX() {
        Lexer oldLexer = this->lexer;
        this->lexer.Next();

        // Look ahead to see if this should be an arrow function instead
        if (this->lexer.token == T::kConst) {
            this->lexer.Next();
        }
        if (this->lexer.token == T::kIdentifier) {
            this->lexer.Next();
            if (this->lexer.token == T::kComma || this->lexer.token == T::kEquals) {
                this->lexer = oldLexer;
                return true;
            }
            if (this->lexer.token == T::kExtends) {
                this->lexer.Next();
                bool isTSArrowFn = this->lexer.token != T::kEquals && this->lexer.token != T::kGreaterThan && this->lexer.token != T::kSlash;
                this->lexer = oldLexer;
                return isTSArrowFn;
            }
        }

        // Restore the lexer
        this->lexer = oldLexer;
        return false;
    }

    // Wraps an inlined enum value expression with a comment annotation. This
    // preserves the original enum member name in the output for debugging.
    // Skips wrapping if the comment contains "*/" to avoid breaking comment syntax.
    Expr Parser::wrapInlinedEnum(Expr value, const std::string &comment) {
        if (comment.find("*/") != std::string::npos) {
            // Don't wrap with a comment
            return value;
        }

        // Wrap with a comment
        EInlinedEnum *e = new EInlinedEnum();
        e->value = value;
        e->comment = comment;
        return Expr{std::shared_ptr<EInlinedEnum>(e), value.loc};
    }

    // Appends a completed part (a group of related statements) to the parts list.
    // This flushes all accumulated state for the current part: symbol uses,
    // import records, declared symbols, and scopes. Then it runs the visit pass
    // on the statements, handles relocated variable declarations, and determines
    // whether the part can be tree-shaken away if unused.
    void Parser::appendPart(std::vector<Part>& parts, std::vector<Stmt> stmts) {
        this->symbolUses.clear();
        this->importSymbolPropertyUses.clear();
        this->symbolCallUses.clear();
        this->declaredSymbols.clear();
        this->importRecordsForCurrentPart.clear();
        this->scopesForCurrentPart.clear();

        Part part;
        part.stmts = this->visitStmtsAndPrependTempRefs(stmts, prependTempRefsOpts{/*fnBodyLoc=*/nullptr, /*kind=*/stmtsKind::stmtsNormal});
        part.symbol_uses = this->symbolUses;

        // Insert any relocated variable statements now
        if (!this->relocatedTopLevelVars.empty()) {
            std::unordered_map<compiler::Ref, bool, RefHash> alreadyDeclared;
            for (compiler::LocRef local : this->relocatedTopLevelVars) {
                // Follow links because "var" declarations may be merged due to hoisting
                for (;;) {
                    compiler::Ref link = this->symbols[local.ref.inner_index].link;
                    if (link == compiler::kInvalidRef) {
                        break;
                    }
                    local.ref = link;
                }

                // Only declare a given relocated variable once
                if (!alreadyDeclared[local.ref]) {
                    alreadyDeclared[local.ref] = true;
                    SLocal *s = new SLocal();
                    Decl decl;
                    decl.binding = Binding{std::shared_ptr<BIdentifier>(new BIdentifier{local.ref}), local.loc};
                    s->decls.push_back(decl);
                    part.stmts.push_back(Stmt{std::shared_ptr<SLocal>(s), local.loc});
                }
            }
            this->relocatedTopLevelVars.clear();
        }

        if (!part.stmts.empty()) {
            StmtsCanBeRemovedIfUnusedFlags flags = StmtsCanBeRemovedIfUnusedFlags::kNone;
            if (this->options.optionsThatSupportStructuralEquality.mode == config::Mode::kPassThrough) {
                // Exports are tracked separately, so export clauses can normally always
                // be removed. Except we should keep them if we're not doing any format
                // conversion because exports are not re-emitted in that case.
                flags = flags | StmtsCanBeRemovedIfUnusedFlags::kKeepExportClauses;
            }

            part.can_be_removed_if_unused = this->astHelpers.StmtsCanBeRemovedIfUnused(part.stmts, flags);
        }

        part.import_record_indices = this->importRecordsForCurrentPart;
        part.declared_symbols = this->declaredSymbols;
        part.import_symbol_property_uses = this->importSymbolPropertyUses;
        part.symbol_call_uses = this->symbolCallUses;
        part.scopes = this->scopesForCurrentPart;

        parts.push_back(part);
    }

    // Computes character frequency across the source file for identifier
    // minification. The frequency is built from all source text, then comments,
    // import paths, and existing symbols are subtracted so that the minifier
    // generates names that are distinct from existing identifiers and comments.
    compiler::CharFreq *Parser::computeCharacterFrequency() {
        if (!this->options.optionsThatSupportStructuralEquality.minifyIdentifiers || this->source.index == kRuntimeSourceIndex) {
            return nullptr;
        }

        // Add everything in the file to the histogram
        compiler::CharFreq *charFreq = new compiler::CharFreq();
        charFreq->Scan(this->source.contents, 1);

        // Subtract out all comments
        for (logger::Range &commentRange : this->lexer.all_comments) {
            charFreq->Scan(this->source.TextForRange(commentRange), -1);
        }

        // Subtract out all import paths
        for (compiler::ImportRecord &record : this->importRecords) {
            if (!record.source_index.IsValid()) {
                charFreq->Scan(record.path.text, -1);
            }
        }

        // Subtract out all symbols that will be minified
        std::function<void(Scope *)> visit = [&](Scope *scope) {
            for (auto &entry : scope->members) {
                compiler::Symbol &symbol = this->symbols[entry.second.ref.inner_index];
                if (symbol.SlotNamespace() != compiler::SlotNamespace::kMustNotBeRenamed) {
                    charFreq->Scan(symbol.original_name, -static_cast<int32_t>(symbol.use_count_estimate));
                }
            }
            if (scope->label.ref != compiler::kInvalidRef) {
                compiler::Symbol &symbol = this->symbols[scope->label.ref.inner_index];
                if (symbol.SlotNamespace() != compiler::SlotNamespace::kMustNotBeRenamed) {
                    charFreq->Scan(symbol.original_name, -static_cast<int32_t>(symbol.use_count_estimate) - 1);
                }
            }
            for (Scope *child : scope->children) {
                visit(child);
            }
        };
        visit(this->moduleScope);

        // Subtract out all properties that will be mangled
        for (auto &entry : this->mangledProps) {
            compiler::Symbol &symbol = this->symbols[entry.second.inner_index];
            charFreq->Scan(symbol.original_name, -static_cast<int32_t>(symbol.use_count_estimate));
        }

        return charFreq;
    }

    // Generates an import statement for a dependency, creating the corresponding
    // import record, clause items, and named import mappings. The generated
    // statement is appended to the given parts list. Used for runtime imports,
    // JSX imports, injected file imports, and glob pattern imports.
    //
    // Returns the updated parts list and the import record index.
    std::pair<std::vector<Part>, uint32_t> Parser::generateImportStmt(const std::string &path, logger::Range pathRange, std::vector<std::string> imports, std::vector<Part> parts, std::unordered_map<std::string, compiler::LocRef> importSymbols, uint32_t *sourceIndex, uint32_t *copySourceIndex) {
        if (pathRange.len == 0) {
            bool isFirst = true;
            for (auto &it : importSymbols) {
                if (isFirst || it.second.loc.start < pathRange.loc.start) {
                    pathRange.loc = it.second.loc;
                }
                isFirst = false;
            }
        }

        compiler::Ref namespaceRef = this->newSymbol(compiler::SymbolKind::kOther, "import_" + GenerateNonUniqueNameFromPath(path));
        this->moduleScope->generated.push_back(namespaceRef);
        std::vector<DeclaredSymbol> localDeclaredSymbols;
        localDeclaredSymbols.reserve(1 + imports.size());
        std::vector<ClauseItem> clauseItems;
        clauseItems.reserve(imports.size());
        uint32_t importRecordIndex = this->addImportRecord(compiler::ImportKind::kStmt, compiler::ImportPhase::kEvaluation, pathRange, path, nullptr, compiler::ImportRecordFlags::kNone);
        if (sourceIndex != nullptr) {
            this->importRecords[importRecordIndex].source_index = compiler::Index32::Make(*sourceIndex);
        }
        if (copySourceIndex != nullptr) {
            this->importRecords[importRecordIndex].copy_source_index = compiler::Index32::Make(*copySourceIndex);
        }
        declaredSymbols.push_back(DeclaredSymbol{/*ref=*/namespaceRef, /*is_top_level=*/true});

        // Create per-import information
        for (size_t i = 0; i < imports.size(); i++) {
            const std::string &alias = imports[i];
            compiler::LocRef it = importSymbols[alias];
            declaredSymbols.push_back(DeclaredSymbol{/*ref=*/it.ref, /*is_top_level=*/true});
            ClauseItem item;
            item.alias = alias;
            item.alias_loc = it.loc;
            item.name = compiler::LocRef{/*loc=*/it.loc, /*ref=*/it.ref};
            clauseItems.push_back(item);
            this->isImportItem[it.ref] = true;
            NamedImport ni;
            ni.alias = alias;
            ni.alias_loc = it.loc;
            ni.namespace_ref = namespaceRef;
            ni.import_record_index = importRecordIndex;
            this->namedImports[it.ref] = ni;
        }

        // Append a single import to the end of the file (ES6 imports are hoisted
        // so we don't need to worry about where the import statement goes)
        SImport *import = new SImport();
        import->namespace_ref = namespaceRef;
        import->items = std::shared_ptr<std::vector<ClauseItem>>(new std::vector<ClauseItem>(clauseItems));
        import->import_record_index = importRecordIndex;
        import->is_single_line = true;

        Part part;
        part.declared_symbols = declaredSymbols;
        part.import_record_indices = std::vector<uint32_t>{importRecordIndex};
        part.stmts = std::vector<Stmt>{Stmt{std::shared_ptr<SImport>(import), pathRange.loc}};
        parts.push_back(part);
        return {parts, importRecordIndex};
    }

    // Assembles the final AST from the parsed and visited parts. This is the
    // last step of parsing and performs:
    //
    // 1. Insert runtime imports (helper function imports) at the top
    // 2. Insert JSX runtime imports (react/jsx-runtime)
    // 3. Insert legacy JSX imports (React.createElement)
    // 4. Insert glob pattern imports
    // 5. Prepend generated imports before user code (for buggy TS compatibility)
    // 6. Scan for import/export statements to populate metadata tables
    // 7. Remove unused TypeScript import-equals statements (may need multiple passes)
    // 8. Mark re-exported imports
    // 9. Analyze cross-part symbol dependencies for tree shaking
    // 10. Determine module type (ESM vs CommonJS)
    // 11. Package everything into the final AST struct
    AST Parser::toAST(std::vector<Part> before, std::vector<Part> parts, std::vector<Part> after, const std::string &hashbang, std::vector<std::string> directives) {
        // Insert an import statement for any runtime imports we generated
        if (!this->runtimeImports.empty()) {
            std::vector<std::string> keys = sortedKeysOfMapStringLocRef(this->runtimeImports);
            uint32_t sourceIndex = kRuntimeSourceIndex;
            before = this->generateImportStmt("<runtime>", logger::Range{}, keys, before, this->runtimeImports, &sourceIndex, nullptr).first;
        }

        // Insert an import statement for any jsx runtime imports we generated
        if (!this->jsxRuntimeImports.empty() && !this->options.optionsThatSupportStructuralEquality.omitJSXRuntimeForTests) {
            std::vector<std::string> keys = sortedKeysOfMapStringLocRef(this->jsxRuntimeImports);

            // Determine the runtime source and whether it's prod or dev
            std::string path = this->options.jsx.ImportSource;
            if (this->options.jsx.Development) {
                path = path + "/jsx-dev-runtime";
            } else {
                path = path + "/jsx-runtime";
            }

            before = this->generateImportStmt(path, logger::Range{}, keys, before, this->jsxRuntimeImports, nullptr, nullptr).first;
        }

        // Insert an import statement for any legacy jsx imports we generated (i.e., createElement)
        if (!this->jsxLegacyImports.empty() && !this->options.optionsThatSupportStructuralEquality.omitJSXRuntimeForTests) {
            std::vector<std::string> keys = sortedKeysOfMapStringLocRef(this->jsxLegacyImports);
            std::string path = this->options.jsx.ImportSource;
            before = this->generateImportStmt(path, logger::Range{}, keys, before, this->jsxLegacyImports, nullptr, nullptr).first;
        }

        // Insert imports for each glob pattern
        for (globPatternImport &glob : this->globPatternImports) {
            std::unordered_map<std::string, compiler::LocRef> globSymbols;
            globSymbols[glob.name] = compiler::LocRef{/*loc=*/glob.approximateRange.loc, /*ref=*/glob.ref};
            std::vector<Part> result;
            uint32_t importRecordIndex;
            std::tie(result, importRecordIndex) = this->generateImportStmt(helpers::GlobPatternToString(glob.parts), glob.approximateRange, std::vector<std::string>{glob.name}, before, globSymbols, nullptr, nullptr);
            before = result;
            compiler::ImportRecord *record = &this->importRecords[importRecordIndex];
            record->assert_or_with = glob.assertOrWith;
            record->phase = glob.phase;
            compiler::GlobPattern *pattern = new compiler::GlobPattern();
            pattern->parts = glob.parts;
            pattern->export_alias = glob.name;
            pattern->kind = glob.kind;
            record->glob_pattern = pattern;
        }

        // Generated imports are inserted before other code instead of appending them
        // to the end of the file. Appending them should work fine because JavaScript
        // import statements are "hoisted" to run before the importing file. However,
        // some buggy JavaScript toolchains such as the TypeScript compiler convert
        // ESM into CommonJS by replacing "import" statements inline without doing
        // any hoisting, which is incorrect. See the following issue for more info:
        // https://github.com/microsoft/TypeScript/issues/16166. Since JSX-related
        // imports are present in the generated code when bundling is disabled, and
        // could therefore be processed by these buggy tools, it's more robust to put
        // them at the top even though it means potentially reallocating almost the
        // entire array of parts.
        if (!before.empty()) {
            parts.insert(parts.begin(), before.begin(), before.end());
        }
        parts.insert(parts.end(), after.begin(), after.end());

        // Handle import paths after the whole file has been visited because we need
        // symbol usage counts to be able to remove unused type-only imports in
        // TypeScript code.
        bool keptImportEquals = false;
        bool removedImportEquals = false;
        size_t partsEnd = 0;
        for (size_t partIndex = 0; partIndex < parts.size(); partIndex++) {
            Part &part = parts[partIndex];
            this->importRecordsForCurrentPart.clear();
            this->declaredSymbols.clear();

            importsExportsScanResult result = this->scanForImportsAndExports(part.stmts);
            part.stmts = result.stmts;
            keptImportEquals = keptImportEquals || result.keptImportEquals;
            removedImportEquals = removedImportEquals || result.removedImportEquals;

            part.import_record_indices.insert(part.import_record_indices.end(), this->importRecordsForCurrentPart.begin(), this->importRecordsForCurrentPart.end());
            part.declared_symbols.insert(part.declared_symbols.end(), this->declaredSymbols.begin(), this->declaredSymbols.end());

            if (!part.stmts.empty() || partIndex == kNSExportPartIndex) {
                if (this->moduleScope->contains_direct_eval && !part.declared_symbols.empty()) {
                    // If this file contains a direct call to "eval()", all parts that
                    // declare top-level symbols must be kept since the eval'd code may
                    // reference those symbols.
                    part.can_be_removed_if_unused = false;
                }
                parts[partsEnd] = part;
                partsEnd++;
            }
        }
        parts.resize(partsEnd);

        // We need to iterate multiple times if an import-equals statement was
        // removed and there are more import-equals statements that may be removed.
        // In the example below, a/b/c should be kept but x/y/z should be removed
        // (and removal requires multiple passes):
        //
        //   import a = foo.a
        //   import b = a.b
        //   import c = b.c
        //
        //   import x = foo.x
        //   import y = x.y
        //   import z = y.z
        //
        //   export let bar = c
        //
        // This is a smaller version of the general import/export scanning loop above.
        // We only want to repeat the code that eliminates TypeScript import-equals
        // statements, not the other code in the loop above.
        while (keptImportEquals && removedImportEquals) {
            keptImportEquals = false;
            removedImportEquals = false;
            size_t end = 0;
            for (size_t partIndex = 0; partIndex < parts.size(); partIndex++) {
                Part part = parts[partIndex];
                importsExportsScanResult result = this->scanForUnusedTSImportEquals(part.stmts);
                part.stmts = result.stmts;
                keptImportEquals = keptImportEquals || result.keptImportEquals;
                removedImportEquals = removedImportEquals || result.removedImportEquals;
                if (!part.stmts.empty() || partIndex == kNSExportPartIndex) {
                    parts[end] = part;
                    end++;
                }
            }
            parts.resize(end);
        }

        // Do a second pass for exported items now that imported items are filled out
        for (Part &part : parts) {
            for (Stmt &stmt : part.stmts) {
                if (const SExportClause *s = Get<SExportClause>(stmt.data); s != nullptr) {
                    for (const ClauseItem &item : s->items) {
                        // Mark re-exported imports as such
                        auto namedImportIt = this->namedImports.find(item.name.ref);
                        if (namedImportIt != this->namedImports.end()) {
                            namedImportIt->second.is_exported = true;
                            this->namedImports[item.name.ref] = namedImportIt->second;
                        }
                    }
                }
            }
        }

        // Analyze cross-part dependencies for tree shaking and code splitting
        {
            // Map locals to parts
            this->topLevelSymbolToParts.clear();
            for (size_t partIndex = 0; partIndex < parts.size(); partIndex++) {
                Part &part = parts[partIndex];
                for (DeclaredSymbol &declared : part.declared_symbols) {
                    if (declared.is_top_level) {
                        // If this symbol was merged, use the symbol at the end of the
                        // linked list in the map. This is the case for multiple "var"
                        // declarations with the same name, for example.
                        compiler::Ref ref = declared.ref;
                        while (this->symbols[ref.inner_index].link != compiler::kInvalidRef) {
                            ref = this->symbols[ref.inner_index].link;
                        }
                        this->topLevelSymbolToParts[ref].push_back(static_cast<uint32_t>(partIndex));
                    }
                }
            }

            // Pulling in the exports of this module always pulls in the export part
            this->topLevelSymbolToParts[this->exportsRef].push_back(kNSExportPartIndex);
        }

        // Make a wrapper symbol in case we need to be wrapped in a closure
        compiler::Ref wrapperRef = this->newSymbol(compiler::SymbolKind::kOther, "require_" + this->source.identifier_name);

        ExportsKind exportsKind = ExportsKind::kNone;
        bool usesExportsRef = this->symbols[this->exportsRef.inner_index].use_count_estimate > 0;
        bool usesModuleRef = this->symbols[this->moduleRef.inner_index].use_count_estimate > 0;

        if (this->esmExportKeyword.len > 0 || this->esmImportMeta.len > 0 || this->topLevelAwaitKeyword.len > 0) {
            exportsKind = ExportsKind::kESM;
        } else if (usesExportsRef || usesModuleRef || this->hasTopLevelReturn) {
            exportsKind = ExportsKind::kCommonJS;
        } else {
            // If this module has no exports, try to determine what kind of module it
            // is by looking at node's "type" field in "package.json" and/or whether
            // the file extension is ".mjs"/".mts" or ".cjs"/".cts".
            if (IsCommonJS(this->options.optionsThatSupportStructuralEquality.moduleTypeData.type)) {
                // ".cjs" or ".cts" or ("type: commonjs" and (".js" or ".jsx" or ".ts" or ".tsx"))
                exportsKind = ExportsKind::kCommonJS;
            } else if (IsESM(this->options.optionsThatSupportStructuralEquality.moduleTypeData.type)) {
                // ".mjs" or ".mts" or ("type: module" and (".js" or ".jsx" or ".ts" or ".tsx"))
                exportsKind = ExportsKind::kESM;
            } else {
                // Treat unknown modules containing an import statement as ESM. Otherwise
                // the bundler will treat this file as CommonJS if it's imported and ESM
                // if it's not imported.
                if (this->esmImportStatementKeyword.len > 0) {
                    exportsKind = ExportsKind::kESM;
                }
            }
        }

        AST ast;
        ast.parts = parts;
        ast.module_type_data = this->options.optionsThatSupportStructuralEquality.moduleTypeData;
        ast.module_scope = this->moduleScope;
        ast.char_freq = std::shared_ptr<compiler::CharFreq>(this->computeCharacterFrequency());
        ast.symbols = this->symbols;
        ast.exports_ref = this->exportsRef;
        ast.module_ref = this->moduleRef;
        ast.uses_exports_ref = usesExportsRef;
        ast.uses_module_ref = usesModuleRef;
        ast.wrapper_ref = wrapperRef;
        ast.hashbang = hashbang;
        ast.directives = directives;
        ast.named_imports = this->namedImports;
        ast.named_exports = this->namedExports;
        ast.ts_enums = this->tsEnums;
        ast.const_values = this->constValues;
        ast.expr_comments = this->exprComments;
        ast.nested_scope_slot_counts = compiler::SlotCounts{};
        ast.top_level_symbol_to_parts_from_parser = this->topLevelSymbolToParts;
        ast.export_star_import_records = this->exportStarImportRecords;
        ast.import_records = this->importRecords;
        ast.approximate_line_count = this->lexer.approximate_newline_count + 1;
        ast.mangled_props = this->mangledProps;
        ast.reserved_props = this->reservedProps;
        ast.manifest_for_yarn_pnp = this->manifestForYarnPnP;
        ast.export_keyword = this->esmExportKeyword;
        ast.top_level_await_keyword = this->topLevelAwaitKeyword;
        ast.live_top_level_await_keyword = this->liveTopLevelAwaitKeyword;
        ast.exports_kind = exportsKind;
        return ast;
    }

    // Creates and initializes a new Parser instance. Sets up all parser state
    // including the lexer, options, symbol tables, scope, and helper context.
    // Also initializes lazy references (Promise, RegExp, BigInt, WeakMap, WeakSet)
    // and configures debug/minification flags.
    //
    // The entry scope is pushed here and popped in Parse() after the visit pass.
    Parser *newParser(logger::Log log, logger::Source source, Lexer lexer, Options *options) {
        if (options->defines == nullptr) {
            config::ProcessedDefines defaultDefines = config::ProcessDefines(std::vector<config::DefineData>{});
            options->defines = new config::ProcessedDefines(std::move(defaultDefines));
        }

        Parser *p = new Parser();
        p->log = log;
        p->source = source;
        p->tracker = logger::LineColumnTracker(&source);
        p->lexer = std::move(lexer);
        p->allowIn = true;
        p->options = *options;
        p->runtimeImports = {};
        p->promiseRef = compiler::kInvalidRef;
        p->regExpRef = compiler::kInvalidRef;
        p->bigIntRef = compiler::kInvalidRef;
        p->afterArrowBodyLoc = logger::Loc{-1};
        p->firstJSXElementLoc = logger::Loc{-1};
        p->importMetaRef = compiler::kInvalidRef;
        p->superCtorRef = compiler::kInvalidRef;

        // For lowering private methods
        p->weakMapRef = compiler::kInvalidRef;
        p->weakSetRef = compiler::kInvalidRef;
        p->privateGetters = {};
        p->privateSetters = {};

        // These are for TypeScript
        p->refToTSNamespaceMemberData = {};
        p->emittedNamespaceVars = {};
        p->isExportedInsideNamespace = {};
        p->localTypeNames = {};

        // These are for handling ES6 imports and exports
        p->importItemsForNamespace = {};
        p->isImportItem = {};
        p->namedImports = {};
        p->namedExports = {};

        // For JSX runtime imports
        p->jsxRuntimeImports = {};
        p->jsxLegacyImports = {};

        // Add "/* @__KEY__ */" comments when mangling properties to support
        // running guchho (or other tools like Terser) again on the output. This
        // checks both "--mangle-props" and "--reserve-props" so that you can turn
        // this on with just "--reserve-props=." if you want to.
        p->shouldAddKeyComment = ((options->mangleProps != nullptr) || (options->reserveProps != nullptr));

        p->suppressWarningsAboutWeirdCode = helpers::IsInsideNodeModules(source.key_path.text);

        if (!options->dropLabels.empty()) {
            p->dropLabelsMap = {};
            for (const std::string &name : options->dropLabels) {
                p->dropLabelsMap[name] = true;
            }
        }

        if (!options->optionsThatSupportStructuralEquality.minifyWhitespace) {
            p->exprComments = {};
        }

        p->astHelpers = MakeHelperContext([p](compiler::Ref ref) {
            return p->symbols[ref.inner_index].kind == compiler::SymbolKind::kUnbound;
        });

        p->pushScopeForParsePass(ScopeKind::kEntry, logger::Loc{locModuleScope});

        return p;
    }

    // Main entry point for parsing a JavaScript/TypeScript source file into an AST.
    //
    // Two-pass process:
    //   Pass 1 (parse): Tokenize and build the AST tree, declare symbols, create
    //                   scopes, and collect import/export metadata.
    //   Pass 2 (visit): Walk the AST for symbol resolution, error reporting,
    //                   constant folding, dead code elimination, and syntax lowering.
    //
    // Input:  source code string + parser options
    // Output: (AST, success_flag) — success is false if a LexerPanic was thrown
    //
    // Key steps:
    //   1. Consume hashbang, enable top-level await
    //   2. Parse all statements up to EOF
    //   3. Extract directive prologue ("use strict")
    //   4. Inject file imports, handle using declarations
    //   5. Visit all parts (symbol binding, lowering)
    //   6. Pop module scope, assemble final AST
    std::pair<AST, bool> Parse(logger::Log log, logger::Source source, Options options) {
        bool ok = true;

        // Default options for JSX elements
        if (options.jsx.Factory.Parts.empty()) {
            config::DefineExpr factory;
            factory.Parts = std::vector<std::string>{"React", "createElement"};
            options.jsx.Factory = factory;
        }
        if (options.jsx.Fragment.Parts.empty() && !options.jsx.Fragment.HasConstant()) {
            config::DefineExpr fragment;
            fragment.Parts = std::vector<std::string>{"React", "Fragment"};
            options.jsx.Fragment = fragment;
        }
        if (options.jsx.ImportSource.empty()) {
            options.jsx.ImportSource = "react";
        }

        Parser *p = newParser(log, source, Lexer(log, source, options.optionsThatSupportStructuralEquality.ts), &options);

        AST result;
        try {
            // Consume a leading hashbang comment
            std::string hashbang;
            if (p->lexer.token == T::kHashbang) {
                hashbang = p->lexer.identifier.str;
                p->lexer.Next();
            }

            // Allow top-level await
            p->fnOrArrowDataParse.await = allowExpr;
            p->fnOrArrowDataParse.isTopLevel = true;

            // Parse the file in the first pass, but do not bind symbols
            parseStmtOpts parseOpts;
            parseOpts.isModuleScope = true;
            parseOpts.allowDirectivePrologue = true;
            std::vector<Stmt> stmts = p->parseStmtsUpTo(T::kEndOfFile, parseOpts);
            p->prepareForVisitPass();

            // Insert a "use strict" directive if "alwaysStrict" is active
            std::vector<std::string> directives;
            if (config::TSAlwaysStrict *tsAlwaysStrict = p->options.tsAlwaysStrict; tsAlwaysStrict != nullptr && tsAlwaysStrict->Value) {
                directives.push_back("use strict");
            }

            // Strip off all leading directives
            {
                size_t totalCount = 0;
                size_t keptCount = 0;

                for (Stmt &stmt : stmts) {
                    if (const SComment *s = Get<SComment>(stmt.data); s != nullptr) {
                        stmts[keptCount] = stmt;
                        keptCount++;
                        totalCount++;
                        continue;
                    }

                    if (const SDirective *s = Get<SDirective>(stmt.data); s != nullptr) {
                        if (p->isStrictMode() && s->legacy_octal_loc.start > 0) {
                            p->markStrictModeFeature(strictModeFeature::legacyOctalEscape, p->source.RangeOfLegacyOctalEscape(s->legacy_octal_loc), "");
                        }
                        std::string directive = helpers::UTF16ToString(std::span<const char16_t>(s->value.data(), s->value.size()));

                        // Remove duplicate directives
                        bool found = false;
                        for (const std::string &existing : directives) {
                            if (existing == directive) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            directives.push_back(directive);
                        }

                        // Remove this directive from the statement list
                        totalCount++;
                        continue;
                    }

                    // Stop when the directive prologue ends
                    break;
                }

                if (keptCount < totalCount) {
                    std::vector<Stmt> kept(stmts.begin(), stmts.begin() + static_cast<std::ptrdiff_t>(keptCount));
                    kept.insert(kept.end(), stmts.begin() + static_cast<std::ptrdiff_t>(totalCount), stmts.end());
                    stmts = std::move(kept);
                }
            }

            // Add an empty part for the namespace export that we can fill in later
            Part nsExportPart;
            nsExportPart.symbol_uses = {};
            nsExportPart.can_be_removed_if_unused = true;

            std::vector<Part> before;
            before.push_back(nsExportPart);
            std::vector<Part> parts;
            std::vector<Part> after;

            // Insert any injected import statements now that symbols have been declared
            for (config::InjectedFile &file : p->options.injectedFiles) {
                std::vector<std::string> exportsNoConflict;
                exportsNoConflict.reserve(file.Exports.size());
            std::unordered_map<std::string, compiler::LocRef> symbols;

                if (!file.DefineName.empty()) {
                    compiler::Ref ref = p->newSymbol(compiler::SymbolKind::kOther, file.DefineName);
                    p->moduleScope->generated.push_back(ref);
                    symbols["default"] = compiler::LocRef{logger::Loc{}, ref};
                    exportsNoConflict.push_back("default");
                    p->injectedDefineSymbols.push_back(ref);
                } else {
                    for (const config::InjectableExport &export_ : file.Exports) {
                        // Skip injecting this symbol if it's already declared locally
                        // (i.e. it's not a reference to a global)
                        if (p->moduleScope->members.find(export_.Alias) != p->moduleScope->members.end()) {
                            continue;
                        }

                        // The key must be a dot-separated identifier list
                        std::vector<std::string> exportParts;
                        {
                            size_t partStart = 0;
                            while (partStart <= export_.Alias.size()) {
                                size_t dot = export_.Alias.find('.', partStart);
                                if (dot == std::string::npos) {
                                    exportParts.push_back(export_.Alias.substr(partStart));
                                    break;
                                }
                                exportParts.push_back(export_.Alias.substr(partStart, dot - partStart));
                                partStart = dot + 1;
                            }
                        }
                        bool isInvalidPart = false;
                        for (const std::string &part : exportParts) {
                            if (!IsIdentifier(part)) {
                                isInvalidPart = true;
                                break;
                            }
                        }
                        if (isInvalidPart) {
                            continue;
                        }

                        compiler::Ref ref = p->newSymbol(compiler::SymbolKind::kInjected, export_.Alias);
                        symbols[export_.Alias] = compiler::LocRef{export_.LocData, ref};
                        if (exportParts.size() == 1) {
                            // Handle the identifier case by generating an injected symbol directly
                            p->moduleScope->members[export_.Alias] = ScopeMember{ref, export_.LocData};
                        } else {
                            // Handle the dot case using a map. This map is similar to the
                            // map "options.defines.DotDefines" but is kept separate instead
                            // of being implemented using the same mechanism because we allow
                            // you to use "define" to rewrite something to an injected symbol
                            // (i.e. we allow two levels of mappings).
                            const std::string &tail = exportParts[exportParts.size() - 1];
                            p->injectedDotNames[tail].push_back(injectedDotName{exportParts, static_cast<uint32_t>(p->injectedDefineSymbols.size())});
                            p->injectedDefineSymbols.push_back(ref);
                        }
                        exportsNoConflict.push_back(export_.Alias);
                        p->injectedSymbolSources[ref] = injectedSymbolSource{file.SourceData, export_.LocData};
                    }
                }

                if (file.IsCopyLoader) {
                    before = p->generateImportStmt(file.SourceData.key_path.text, logger::Range{}, exportsNoConflict, before, symbols, nullptr, &file.SourceData.index).first;
                } else {
                    before = p->generateImportStmt(file.SourceData.key_path.text, logger::Range{}, exportsNoConflict, before, symbols, &file.SourceData.index, nullptr).first;
                }
            }

            // When "using" declarations appear at the top level, we change all TDZ
            // variables in the top-level scope into "var" so that they aren't harmed
            // when they are moved into the try/catch statement that lowering will
            // generate.
            p->willWrapModuleInTryCatchForUsing = p->shouldLowerUsingDeclarations(stmts);

            // Bind symbols in a second pass over the AST. We do this in a separate
            // pass after parsing because it is impossible to correctly handle arrow
            // functions with a single pass due to the grammar ambiguities.
            if (!p->options.optionsThatSupportStructuralEquality.treeShaking || p->willWrapModuleInTryCatchForUsing) {
                // When tree shaking is disabled, everything comes in a single part
                p->appendPart(parts, stmts);
            } else {
                std::unordered_map<int, std::vector<Part>> preprocessedEnums;
                if (!p->scopesInOrderForEnum.empty()) {
                    // Preprocess TypeScript enums to improve code generation. Otherwise
                    // uses of an enum before that enum has been declared won't be inlined.
                    for (size_t i = 0; i < stmts.size(); i++) {
                        Stmt &stmt = stmts[i];
                        if (Get<SEnum>(stmt.data) != nullptr) {
                            std::vector<scopeOrder> oldScopesInOrder = std::move(p->scopesInOrder);
                            auto enumScopesIt = p->scopesInOrderForEnum.find(stmt.loc);
                            if (enumScopesIt != p->scopesInOrderForEnum.end()) {
                                p->scopesInOrder = enumScopesIt->second;
                            }
                            std::vector<Part> enumParts;
                            p->appendPart(enumParts, std::vector<Stmt>{stmt});
                            preprocessedEnums[static_cast<int>(i)] = std::move(enumParts);
                            p->scopesInOrder = std::move(oldScopesInOrder);
                        }
                    }
                }

                // When tree shaking is enabled, each top-level statement is
                // potentially a separate part
                for (size_t i = 0; i < stmts.size(); i++) {
                    Stmt &stmt = stmts[i];
                    if (const SLocal *s = Get<SLocal>(stmt.data); s != nullptr) {
                        // Split up top-level multi-declaration variable statements
                        for (const Decl &decl : s->decls) {
                            SLocal clone = *s;
                            clone.decls = std::vector<Decl>{decl};
                            p->appendPart(parts, std::vector<Stmt>{Stmt{std::make_shared<SLocal>(clone), stmt.loc}});
                        }
                    } else if (Get<SImport>(stmt.data) != nullptr || Get<SExportFrom>(stmt.data) != nullptr || Get<SExportStar>(stmt.data) != nullptr) {
                        if (p->options.optionsThatSupportStructuralEquality.mode != config::Mode::kPassThrough) {
                            // Move imports (and import-like exports) to the top of the
                            // file to ensure that if they are converted to a require()
                            // call, the effects will take place before any other
                            // statements are evaluated.
                            p->appendPart(before, std::vector<Stmt>{stmt});
                        } else {
                            // If we aren't doing any format conversion, just keep these
                            // statements inline where they were.
                            p->appendPart(parts, std::vector<Stmt>{stmt});
                        }
                    } else if (Get<SExportEquals>(stmt.data) != nullptr) {
                        // TypeScript "export = value;" becomes "module.exports = value;".
                        // This must happen at the end after everything is parsed because
                        // TypeScript moves this statement to the end when it generates code.
                        p->appendPart(after, std::vector<Stmt>{stmt});
                    } else if (Get<SEnum>(stmt.data) != nullptr) {
                        auto enumPartsIt = preprocessedEnums.find(static_cast<int>(i));
                        if (enumPartsIt != preprocessedEnums.end()) {
                            parts.insert(parts.end(), enumPartsIt->second.begin(), enumPartsIt->second.end());
                        }
                        auto enumScopesIt = p->scopesInOrderForEnum.find(stmt.loc);
                        if (enumScopesIt != p->scopesInOrderForEnum.end() && p->scopesInOrder.size() >= enumScopesIt->second.size()) {
                            p->scopesInOrder.erase(p->scopesInOrder.begin(), p->scopesInOrder.begin() + static_cast<std::ptrdiff_t>(enumScopesIt->second.size()));
                        }
                    } else {
                        p->appendPart(parts, std::vector<Stmt>{stmt});
                    }
                }
            }

            // Insert a variable for "import.meta" at the top of the file if it was used.
            if (p->importMetaRef != compiler::kInvalidRef) {
                std::shared_ptr<SLocal> importMetaStmt = std::make_shared<SLocal>();
                importMetaStmt->kind = p->selectLocalKind(LocalKind::kConst);
                Decl decl;
                decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{p->importMetaRef}), logger::Loc{}};
                decl.value_or_nil = Expr{std::make_shared<EObject>(EObject{}), logger::Loc{}};
                importMetaStmt->decls.push_back(decl);
                Part importMetaPart;
                importMetaPart.stmts = std::vector<Stmt>{Stmt{importMetaStmt, logger::Loc{}}};
                importMetaPart.symbol_uses = {};
                importMetaPart.declared_symbols = std::vector<DeclaredSymbol>{DeclaredSymbol{p->importMetaRef, true}};
                importMetaPart.can_be_removed_if_unused = true;
                before.push_back(importMetaPart);
            }

            // Pop the module scope to apply the "ContainsDirectEval" rules
            p->popScope();

            result = p->toAST(before, parts, after, hashbang, directives);
            result.source_map_comment = p->lexer.source_mapping_url;
        } catch (const LexerPanic &) {
            ok = false;
        }

        return {std::move(result), ok};
    }

    // Creates an AST for a lazy export expression. Lazy exports are evaluated
    // at link time rather than parse time, enabling deferred initialization.
    // Optionally wraps the expression in a runtime helper call (either a global
    // function chain like `a.b.c(expr)` or a runtime function like `__helper(expr)`).
    AST LazyExportAST(logger::Log log, logger::Source source, Options options, Expr expr, HelperCall *helperCall) {
        // Don't create a new lexer using the "Lexer" constructor here since that
        // will actually attempt to parse the first token, which might cause a
        // syntax error when calling "LazyExportAST" for a lazy export.
        Parser *p = newParser(log, source, Lexer{}, &options);
        p->prepareForVisitPass();

        // Defer the actual code generation until linking
        Part part;
        part.symbol_uses = {};

        // Optionally call a runtime API function to transform the expression
        if (helperCall != nullptr) {
            if (!helperCall->Global.empty()) {
                compiler::Ref ref = p->newSymbol(compiler::SymbolKind::kUnbound, helperCall->Global[0]);
                p->recordUsage(ref);
                Expr target = Expr{std::make_shared<EIdentifier>(EIdentifier{ref}), expr.loc};
                CallKind kind = CallKind::kNormal;
                for (size_t i = 1; i < helperCall->Global.size(); i++) {
                    std::shared_ptr<EDot> dot = std::make_shared<EDot>();
                    dot->target = target;
                    dot->name = helperCall->Global[i];
                    target = Expr{dot, expr.loc};
                    kind = CallKind::kTargetWasOriginallyPropertyAccess;
                }
                std::shared_ptr<ECall> call = std::make_shared<ECall>();
                call->target = target;
                call->args = std::vector<Expr>{expr};
                call->kind = kind;
                expr = Expr{call, expr.loc};
            } else {
                std::vector<Expr> args = {expr};
                expr = p->callRuntime(expr.loc, helperCall->Runtime, args);
            }
        }

        part.stmts.push_back(Stmt{std::make_shared<SLazyExport>(SLazyExport{expr}), expr.loc});
        part.symbol_uses = std::move(p->symbolUses);
        p->symbolUses = {};

        // Add an empty part for the namespace export that we can fill in later
        Part nsExportPart;
        nsExportPart.symbol_uses = {};
        nsExportPart.can_be_removed_if_unused = true;

        AST ast = p->toAST({nsExportPart}, {part}, {}, "", {});
        ast.has_lazy_export = true;
        return ast;
    }

    // Creates an AST for a glob pattern import resolution. Generates an export
    // that calls the runtime __glob helper with the resolved import records and
    // an object mapping file paths to their import specifiers.
    AST GlobResolveAST(logger::Log log, logger::Source source, Options options, std::vector<compiler::ImportRecord> importRecords, std::shared_ptr<EObject> object, const std::string &name) {
        // Don't create a new lexer using the "Lexer" constructor here since that
        // will actually attempt to parse the first token.
        Parser *p = newParser(log, source, Lexer{}, &options);
        p->prepareForVisitPass();

        // Add an empty part for the namespace export that we can fill in later
        Part nsExportPart;
        nsExportPart.symbol_uses = {};
        nsExportPart.can_be_removed_if_unused = true;

        if (!p->importRecords.empty()) {
            throw std::runtime_error("Internal error");
        }
        p->importRecords = std::move(importRecords);

        std::vector<uint32_t> importRecordIndices;
        importRecordIndices.reserve(p->importRecords.size());
        for (size_t i = 0; i < p->importRecords.size(); i++) {
            importRecordIndices.push_back(static_cast<uint32_t>(i));
        }

        p->symbolUses = {};
        compiler::Ref ref = p->newSymbol(compiler::SymbolKind::kOther, name);
        p->moduleScope->generated.push_back(ref);

        Part part;
        part.import_record_indices = std::move(importRecordIndices);
        part.symbol_uses = {};

        std::shared_ptr<SLocal> local = std::make_shared<SLocal>();
        local->is_export = true;
        Decl decl;
        decl.binding = Binding{std::make_shared<BIdentifier>(BIdentifier{ref}), logger::Loc{}};
        std::vector<Expr> globArgs;
        globArgs.push_back(Expr{std::move(object), logger::Loc{}});
        decl.value_or_nil = p->callRuntime(logger::Loc{}, "__glob", globArgs);
        local->decls.push_back(decl);
        part.stmts.push_back(Stmt{local, logger::Loc{}});
        part.symbol_uses = std::move(p->symbolUses);
        p->symbolUses = {};

        p->esmExportKeyword.len = 1;
        return p->toAST({nsExportPart}, {part}, {}, "", {});
    }

    // Parses a define expression from a string. Tries two strategies:
    //
    // 1. Property chain: splits on '.' and validates each part is an identifier.
    //    Input:  "process.env.NODE_ENV"
    //    Output: DefineExpr{Parts: ["process", "env", "NODE_ENV"]}
    //
    // 2. JSON value: parses as JSON to get a primitive literal (null, boolean,
    //    string, number, BigInt) or a compound value to be injected out-of-line.
    //    Input:  `"production"` → DefineExpr{Constant: "production"}
    //    Input:  `[1,2,3]`      → (empty DefineExpr, EArray ptr)
    std::pair<config::DefineExpr, std::shared_ptr<E>> ParseDefineExpr(const std::string &text) {
        if (text.empty()) {
            return {config::DefineExpr{}, nullptr};
        }

        // Try a property chain
        std::vector<std::string> parts;
        {
            size_t partStart = 0;
            while (partStart <= text.size()) {
                size_t dot = text.find('.', partStart);
                if (dot == std::string::npos) {
                    parts.push_back(text.substr(partStart));
                    break;
                }
                parts.push_back(text.substr(partStart, dot - partStart));
                partStart = dot + 1;
            }
        }
        bool partsAreValid = true;
        for (size_t i = 0; i < parts.size(); i++) {
            if (!IsIdentifier(parts[i])) {
                partsAreValid = false;
                parts.clear();
                break;
            }

            // Don't allow most keywords as the identifier
            if (i == 0) {
                auto keywordIt = kKeywords.find(parts[i]);
                if (keywordIt != kKeywords.end()) {
                    T token = keywordIt->second;
                    if (token != T::kNull && token != T::kThis && (token != T::kImport || parts.size() < 2 || parts[1] != "meta")) {
                        partsAreValid = false;
                        parts.clear();
                        break;
                    }
                }
            }
        }
        if (partsAreValid) {
            config::DefineExpr defineExpr;
            defineExpr.Parts = parts;
            return {defineExpr, nullptr};
        }

        // Try parsing a value
        logger::Log deferLog = logger::NewDeferLog(logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
        logger::Source jsonSource;
        jsonSource.contents = text;
        JSONOptions jsonOptions;
        jsonOptions.is_for_define = true;
        std::pair<Expr, bool> parsed = Parser::ParseJSON(deferLog, jsonSource, jsonOptions);
        if (!parsed.second) {
            return {config::DefineExpr{}, nullptr};
        }
        Expr &expr = parsed.first;

        // Only primitive literals are inlined directly
        if (Get<ENull>(expr.data) || Get<EBoolean>(expr.data) || Get<EString>(expr.data) || Get<ENumber>(expr.data) || Get<EBigInt>(expr.data)) {
            config::DefineExpr defineExpr;
            defineExpr.Constant = expr.data;
            return {defineExpr, nullptr};
        }

        // If it's not a primitive, return the whole compound JSON value to be
        // injected out-of-line
        return {config::DefineExpr{}, std::make_shared<E>(expr.data)};
    }

    // Validates that an expression is a valid JSON value. JSON values can be:
    // - Atoms: string, number, boolean, null
    // - Arrays: where every element is also valid JSON
    // - Objects: where every key is a string, every value is valid JSON,
    //   and no properties use computed keys or non-field kinds
    //
    // Used to validate define replacement values and JSON import assertions.
    bool Parser::IsValidJSON(const Expr& value) {
        if (std::holds_alternative<std::shared_ptr<EString>>(value.data) ||
            std::holds_alternative<std::shared_ptr<ENumber>>(value.data) ||
            std::holds_alternative<std::shared_ptr<EBoolean>>(value.data) ||
            std::holds_alternative<std::shared_ptr<ENull>>(value.data)) {
            // This is an atom
        } else if (auto *array = std::get_if<std::shared_ptr<EArray>>(&value.data); array != nullptr) {
            for (const Expr &item : (*array)->items) {
                if (!this->IsValidJSON(item)) {
                    return false;
                }
            }
        } else if (auto *object = std::get_if<std::shared_ptr<EObject>>(&value.data); object != nullptr) {
            for (const Property &property : (*object)->properties) {
                if (property.kind != PropertyKind::kField || Has(property.flags, PropertyFlags::kIsComputed)) {
                    return false;
                }
                if (!std::holds_alternative<std::shared_ptr<EString>>(property.key.data)) {
                    return false;
                }
                if (!this->IsValidJSON(property.value_or_nil)) {
                    return false;
                }
            }
        } else {
            return false;
        }

        return true;
    }

}

