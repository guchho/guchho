#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"

#include <unordered_set>

namespace guchho::css {

    namespace {

        // Returns a static set of CSS-wide and reserved keywords that are illegal as
        // container names. These keywords have special semantics in CSS (e.g. "initial"
        // sets a property to its initial value, "inherit" pulls from the parent). All
        // comparisons are case-insensitive because CSS keywords are defined to be
        // case-insensitive. The set is returned by const reference so callers never
        // pay for repeated construction.
        //
        // Usage:
        //   CssWideAndReservedKeywords().count("inherit")  =>  1 (found)
        //   CssWideAndReservedKeywords().count("my-box")   =>  0 (not found)
        const std::unordered_set<std::string>& CssWideAndReservedKeywords() {
            static const std::unordered_set<std::string> keywords = {
                "initial",
                "inherit",
                "unset",
                "default",
                "revert",
                "revert-layer",
            };
            return keywords;
        }

        // Determines whether a candidate text string is forbidden as a container name.
        // A name is invalid when it is the literal "none" (reserved by the CSS
        // Containment spec for "container-name: none") or when it matches any
        // CSS-wide / reserved keyword. Matching is case-insensitive because CSS
        // keywords themselves are case-insensitive.
        //
        // Input:  "none"       => true
        // Input:  "Inherit"    => true
        // Input:  "my-layout"  => false
        //
        // Edge case: empty string is not explicitly handled here; an empty identifier
        // should never reach this function because the lexer will not produce a
        // kIdent token with zero-length text.
        bool IsInvalidContainerName(std::string_view text) {
            std::string lower = helpers::ToLowerASCII(text);
            return lower == "none" || CssWideAndReservedKeywords().count(lower) != 0;
        }

        // Looks up (or creates) a compiler symbol for a container name and returns a
        // LocRef that points to it. When the symbol already exists in the current
        // scope the existing LocRef is returned; otherwise a brand-new Symbol entry is
        // appended to the symbols list and registered in the appropriate scope map.
        //
        // The function chooses between local and global scope based on
        // context.make_local_symbols. Local symbols are used when the container
        // declaration is inside a @scope rule or similar block-scoped construct;
        // global symbols are the default for top-level declarations.
        //
        // Side effects:
        //   - Appends to context.symbols when a new name is encountered.
        //   - Registers the new name in context.local_scope or global_scope.
        //   - For local symbols, also appends the LocRef to context.local_symbols.
        //   - Increments use_count_estimate on every call so downstream passes can
        //     estimate how heavily a symbol is referenced.
        //
        // Input:  name="sidebar", context.make_local_symbols=false  =>  LocRef to global symbol
        // Input:  name="sidebar", context.make_local_symbols=true   =>  LocRef to local symbol
        // Edge case: If the symbols vector is empty the first push_back creates index 0,
        //            which is valid. If source_index is out of range for Ref the symbol
        //            will be created but may cause issues later — callers must ensure
        //            source_index is valid.
        guchho::compiler::LocRef SymbolForName(guchho::logger::Loc loc, const std::string& name,
                                        ContainerSymbolContext& context) {
            guchho::compiler::SymbolKind kind;
            std::unordered_map<std::string, guchho::compiler::LocRef>* scope;

            if (context.make_local_symbols) {
                kind = guchho::compiler::SymbolKind::kLocalCSS;
                scope = context.local_scope;
            } else {
                kind = guchho::compiler::SymbolKind::kGlobalCSS;
                scope = context.global_scope;
            }

            guchho::compiler::LocRef entry;
            auto it = scope->find(name);
            if (it == scope->end()) {
                entry = guchho::compiler::LocRef{
                    loc,
                    guchho::compiler::Ref{
                        context.source_index,
                        static_cast<uint32_t>(context.symbols->size()),
                    },
                };
                (*scope)[name] = entry;

                context.symbols->push_back(guchho::compiler::Symbol{});
                guchho::compiler::Symbol& symbol = context.symbols->back();
                symbol.kind = kind;
                symbol.original_name = name;
                symbol.link = guchho::compiler::kInvalidRef;

                if (kind == guchho::compiler::SymbolKind::kLocalCSS) {
                    context.local_symbols->push_back(entry);
                }
            } else {
                entry = it->second;
            }

            context.symbols->at(entry.ref.inner_index).use_count_estimate++;
            return entry;
        }

        // Processes a single container-name token. If the token text is an invalid
        // container name (e.g. "none", "initial") the token is left untouched so the
        // downstream error reporter can emit a diagnostic. Otherwise the token is
        // promoted from a plain identifier (kIdent) to a symbol reference (kSymbol)
        // whose payload_index points into the symbols table.
        //
        // Input:  Token{text="main", kind=kIdent}  =>  kind=kSymbol, payload_index set
        // Input:  Token{text="none", kind=kIdent}  =>  unchanged (invalid name)
        void HandleSingleContainerName(Token* token, ContainerSymbolContext& context) {
            if (IsInvalidContainerName(token->text)) {
                return;
            }

            token->kind = TokenType::kSymbol;
            token->payload_index = SymbolForName(token->loc, token->text, context).ref.inner_index;
        }

    }

    // Processes the `container` shorthand property, which accepts a list of
    // container names followed by an optional size query introduced by a slash.
    //
    // The valid shorthand syntax is:
    //   container-name: <ident>+ [/ <size-query>]?
    //
    // The first loop validates that every token before the optional slash is an
    // identifier. If any non-identifier (other than the slash separator) appears,
    // or the slash appears in an invalid position (not followed by exactly one
    // more token), the token vector is returned unchanged so the normal
    // declaration-error path can handle the problem.
    //
    // The second loop converts each leading identifier into a symbol reference,
    // stopping as soon as the slash (or a non-ident) is reached.
    //
    // Input:  [kIdent("sidebar"), kIdent("panel")]         => both become kSymbol
    // Input:  [kIdent("main"), kDelimSlash, kIdent("size")] => "main" becomes kSymbol, slash and size left alone
    // Input:  [kIdent("a"), kDelimSlash]                   => unchanged (slash not followed by ident)
    // Edge case: empty token vector — the for-loop body never executes, nothing changes.
    void ProcessContainerShorthand(std::vector<Token>& tokens, ContainerSymbolContext& context) {
        for (size_t i = 0; i < tokens.size(); i++) {
            Token& t = tokens[i];
            if (t.kind == TokenType::kIdent) {
                continue;
            }
            if (t.kind == TokenType::kDelimSlash && i + 2 == tokens.size() &&
                tokens[i + 1].kind == TokenType::kIdent) {
                break;
            }
            return;
        }

        for (size_t i = 0; i < tokens.size(); i++) {
            Token& t = tokens[i];
            if (t.kind != TokenType::kIdent) {
                break;
            }
            HandleSingleContainerName(&tokens[i], context);
        }
    }

    // Processes the `container-name` longhand property, which is a plain
    // space-separated list of custom-ident names (no slash or size query).
    //
    // The validation loop ensures every token is a kIdent; if any token is not
    // an identifier the vector is left unchanged for the error reporter.
    // The conversion loop then promotes every identifier to a kSymbol token.
    //
    // Input:  [kIdent("sidebar"), kIdent("panel")]  => both become kSymbol
    // Input:  [kIdent("box"), kDelimComma]          => unchanged (comma is not an ident)
    // Edge case: empty token vector — loops are skipped, nothing changes.
    void ProcessContainerName(std::vector<Token>& tokens, ContainerSymbolContext& context) {
        for (size_t i = 0; i < tokens.size(); i++) {
            Token& t = tokens[i];
            if (t.kind != TokenType::kIdent) {
                return;
            }
        }

        for (size_t i = 0; i < tokens.size(); i++) {
            HandleSingleContainerName(&tokens[i], context);
        }
    }

}