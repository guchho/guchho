#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"

namespace guchho::css {

    namespace {

        // Creates or retrieves a symbol for a composes identifier name. This
        // is used when processing CSS Modules composes declarations to track
        // which symbols are referenced across files.
        //
        // The function maintains a scope (local or global) that maps identifier
        // names to symbol references. If the name already exists in the scope,
        // the existing reference is returned. Otherwise, a new symbol is created.
        //
        // The scope is determined by context.make_local_symbols:
        //   - true: symbols are scoped to the current block (kLocalCSS)
        //   - false: symbols are global (kGlobalCSS)
        //
        // Input:  name = "button", context with empty scope
        // Output: New LocRef pointing to created symbol, added to scope
        //
        // Input:  name = "button", context with existing entry
        // Output: Existing LocRef for that name
        //
        // Edge cases:
        //   - The use_count_estimate is incremented on each access.
        //   - Local symbols are also added to local_symbols vector.
        guchho::compiler::LocRef SymbolForName(
            guchho::logger::Loc loc, 
            const std::string& name,
            ComposesSymbolContext& context
        ) {
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

    }

    // Processes a CSS Modules composes! pragma declaration. The composes
    // directive allows one class to inherit styles from another class,
    // potentially from a different file.
    //
    // Syntax forms:
    //   1. composes: button primary;
    //      - Composes from local classes "button" and "primary"
    //
    //   2. composes: button from "./styles.css";
    //      - Composes from external file "./styles.css"
    //
    //   3. composes: button from global;
    //      - Composes from global scope (not local)
    //
    // The function parses the token list and:
    //   1. Collects class names until a "from" clause is found
    //   2. If "from" is followed by a string or URL, creates import records
    //   3. If "from" is followed by "global", uses global scope
    //   4. Otherwise, creates local symbol references
    //
    // Input:  tokens = ["button", "primary"], context with parent .btn
    // Output: composes for .btn references symbols "button" and "primary"
    //
    // Input:  tokens = ["button", "from", "./button.css"]
    // Output: composes for .btn references imported "button" from file
    //
    // Input:  tokens = ["button", "from", "global"]
    // Output: composes for .btn references global "button" symbol
    //
    // Edge cases:
    //   - Reports warnings for invalid tokens or unrecognized "from" targets.
    //   - Multiple parent refs are handled (shared selectors).
    //   - The "from" clause must be at the end of the token list.
    void HandleComposesPragma(const ComposesContext& context, const std::vector<Token>& tokens,
                            ComposesSymbolContext& symbol_context) {
        struct NameWithLoc {
            guchho::logger::Loc loc;
            std::string text;
        };
        std::vector<NameWithLoc> names;
        bool from_global = false;

        for (size_t i = 0; i < tokens.size(); i++) {
            const Token& t = tokens[i];
            if (t.kind == TokenType::kIdent) {
                // Check for a "from" clause at the end
                if (helpers::EqualFoldASCII(t.text, "from") && i + 2 == tokens.size()) {
                    const Token& last = tokens[i + 1];

                    // A string or a URL is an external file
                    if (last.kind == TokenType::kString || last.kind == TokenType::kUrl) {
                        uint32_t import_record_index;
                        if (last.kind == TokenType::kString) {
                            import_record_index = static_cast<uint32_t>(symbol_context.import_records->size());
                            guchho::compiler::ImportRecord record;
                            record.kind = guchho::compiler::ImportKind::kComposesFrom;
                            record.path.text = last.text;
                            record.range = symbol_context.source->RangeOfString(last.loc);
                            symbol_context.import_records->push_back(std::move(record));
                        } else {
                            import_record_index = last.payload_index;
                            (*symbol_context.import_records)[import_record_index].kind =
                                guchho::compiler::ImportKind::kComposesFrom;
                        }
                        for (const guchho::compiler::Ref& parent_ref : context.parent_refs) {
                            std::shared_ptr<Composes>& composes = (*symbol_context.composes)[parent_ref];
                            if (!composes) {
                                composes = std::make_shared<Composes>();
                            }
                            for (const NameWithLoc& name : names) {
                                composes->imported_names.push_back(ImportedComposesName{
                                    name.text,
                                    name.loc,
                                    import_record_index,
                                });
                            }
                        }
                        return;
                    }

                    // An identifier must be "global"
                    if (last.kind == TokenType::kIdent) {
                        if (helpers::EqualFoldASCII(last.text, "global")) {
                            from_global = true;
                            break;
                        }

                        symbol_context.log->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError,
                            guchho::logger::MsgKind::kWarning, symbol_context.tracker,
                            RangeOfIdentifier(*symbol_context.source, last.loc),
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ComposesInvalidLocation, last.text));
                        if (symbol_context.prev_error) {
                            *symbol_context.prev_error = t.loc;
                        }
                        return;
                    }
                }

                names.push_back(NameWithLoc{t.loc, t.text});
                continue;
            }

            // Any unexpected tokens are a syntax error
            std::string text;
            switch (t.kind) {
            case TokenType::kUrl:
            case TokenType::kBadUrl:
            case TokenType::kString:
            case TokenType::kUnterminatedString:
                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnexpectedToken, lexer::ToString(t.kind));
                break;
            default:
                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnexpectedToken, t.text);
                break;
            }
            symbol_context.log->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError,
                guchho::logger::MsgKind::kWarning, symbol_context.tracker,
                guchho::logger::Range{t.loc, 0}, text);
            if (symbol_context.prev_error) {
                *symbol_context.prev_error = t.loc;
            }
            return;
        }

        // If we get here, all of these names are not references to another file
        bool old = symbol_context.make_local_symbols;
        if (from_global) {
            symbol_context.make_local_symbols = false;
        }
        for (const guchho::compiler::Ref& parent_ref : context.parent_refs) {
            std::shared_ptr<Composes>& composes = (*symbol_context.composes)[parent_ref];
            if (!composes) {
                composes = std::make_shared<Composes>();
            }
            for (const NameWithLoc& name : names) {
                composes->names.push_back(SymbolForName(name.loc, name.text, symbol_context));
            }
        }
        symbol_context.make_local_symbols = old;
    }

}
