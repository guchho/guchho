#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/helpers.hpp"


#include <unordered_set>

namespace guchho::css {

    namespace {

        // Returns the set of CSS-wide and reserved keywords that cannot be
        // used as custom identifier names for list-style-type.
        //
        // These keywords are defined in CSS Values and Units Level 3 and
        // CSS Cascading and Inheritance Level 5.
        //
        // The set includes:
        //   - CSS-wide keywords: initial, inherit, unset, default
        //   - Cascade-dependent keywords: revert, revert-layer
        //
        // Input:  (none - accessor function)
        // Output: Reference to static set with 6 entries
        const std::unordered_set<std::string>& CssWideAndReservedKeywords() {
            static const std::unordered_set<std::string> keywords = {
                // CSS Values and Units Level 3: https://drafts.csswg.org/css-values-3/#common-keywords
                "initial", // CSS-wide keyword
                "inherit", // CSS-wide keyword
                "unset",   // CSS-wide keyword
                "default", // CSS reserved keyword

                // CSS Cascading and Inheritance Level 5: https://drafts.csswg.org/css-cascade-5/#defaulting-keywords
                "revert",       // Cascade-dependent keyword
                "revert-layer", // Cascade-dependent keyword
            };
            return keywords;
        }

        // Returns the set of predefined CSS counter style names from the
        // CSS Counter Styles Level 3 specification.
        //
        // These style names cannot be used as custom identifier names for
        // list-style-type because they are reserved for predefined counters.
        //
        // The styles are organized by category:
        //   - Numeric: decimal, upper-roman, lower-roman, etc.
        //   - Alphabetic: lower-alpha, upper-alpha, lower-greek, etc.
        //   - Symbolic: disc, circle, square, etc.
        //   - Fixed: cjk-earthly-branch, cjk-heavenly-stem
        //   - Japanese/Korean/Chinese formal/informal styles
        //   - Ethiopic numeric
        //
        // Input:  (none - accessor function)
        // Output: Reference to static set with 67 entries
        const std::unordered_set<std::string>& PredefinedCounterStyles() {
            static const std::unordered_set<std::string> styles = {
                // 6.1. Numeric:
                "arabic-indic",
                "armenian",
                "bengali",
                "cambodian",
                "cjk-decimal",
                "decimal-leading-zero",
                "decimal",
                "devanagari",
                "georgian",
                "gujarati",
                "gurmukhi",
                "hebrew",
                "kannada",
                "khmer",
                "lao",
                "lower-armenian",
                "lower-roman",
                "malayalam",
                "mongolian",
                "myanmar",
                "oriya",
                "persian",
                "tamil",
                "telugu",
                "thai",
                "tibetan",
                "upper-armenian",
                "upper-roman",

                // 6.2. Alphabetic:
                "hiragana-iroha",
                "hiragana",
                "katakana-iroha",
                "katakana",
                "lower-alpha",
                "lower-greek",
                "lower-latin",
                "upper-alpha",
                "upper-latin",

                // 6.3. Symbolic:
                "circle",
                "disc",
                "disclosure-closed",
                "disclosure-open",
                "square",

                // 6.4. Fixed:
                "cjk-earthly-branch",
                "cjk-heavenly-stem",

                // 7.1.1. Japanese:
                "japanese-formal",
                "japanese-informal",

                // 7.1.2. Korean:
                "korean-hangul-formal",
                "korean-hanja-formal",
                "korean-hanja-informal",

                // 7.1.3. Chinese:
                "simp-chinese-formal",
                "simp-chinese-informal",
                "trad-chinese-formal",
                "trad-chinese-informal",

                // 7.2. Ethiopic Numeric Counter Style:
                "ethiopic-numeric",
            };
            return styles;
        }


    // Creates or retrieves a symbol for a custom identifier name. This is
    // used when converting list-style-type custom identifiers to symbols
    // that can be resolved at runtime.
    //
    // The function maintains a scope (local or global) that maps identifier
    // names to symbol references. If the name already exists in the scope,
    // the existing reference is returned. Otherwise, a new symbol is created.
    //
    // The scope is determined by context.make_local_symbols:
    //   - true: symbols are scoped to the current block (kLocalCSS)
    //   - false: symbols are global (kGlobalCSS)
    //
    // Input:  name = "my-bullet", context with empty scope
    // Output: New LocRef pointing to created symbol, added to scope
    //
    // Input:  name = "my-bullet", context with existing entry
    // Output: Existing LocRef for that name
    //
    // Edge cases:
    //   - The use_count_estimate is incremented on each access.
    //   - Local symbols are also added to local_symbols vector.
    guchho::compiler::LocRef SymbolForName(
        guchho::logger::Loc loc, 
        const std::string& name,
        ListStyleSymbolContext& context
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

    // Processes the list-style shorthand property, which combines up to
    // three values: list-style-type, list-style-position, and list-style-image.
    //
    // The shorthand syntax is:
    //   list-style: <type> <position> <image>;
    //
    // Where:
    //   - <type>: a counter style name, custom identifier, or "none"
    //   - <position>: "inside" or "outside"
    //   - <image>: url(), gradient function, or "none"
    //
    // The function parses the token list and:
    //   1. Identifies image tokens (url, gradient functions)
    //   2. Identifies position tokens (inside, outside)
    //   3. Identifies the type token (custom identifier)
    //   4. Resolves "none" ambiguity (applies to image or type)
    //   5. Converts custom identifiers to symbol tokens
    //
    // Input:  tokens = ["disc", "inside", url("bullet.png")]
    // Output: tokens = [Symbol(disc), "inside", url("bullet.png")]
    //
    // Input:  tokens = ["none", "outside"]
    // Output: tokens = ["none", "outside"] (none applies to image)
    //
    // Input:  tokens = ["custom-bullet"]
    // Output: tokens = [Symbol(custom-bullet)]
    //
    // Edge cases:
    //   - Returns early if token count is outside 1-3.
    //   - Returns early if a string token is found (not a valid type).
    //   - Returns early if css-wide keywords or predefined styles are found.
    //   - "none" is ambiguous and resolved at the end.
    //   - Only one custom identifier is allowed as the type.
    void ProcessListStyleShorthand(std::vector<Token>& tokens, ListStyleSymbolContext& context) {
        if (tokens.size() < 1 || tokens.size() > 3) {
            return;
        }

        bool found_image = false;
        bool found_position = false;
        int type_index = -1;
        int none_count = 0;

        for (size_t i = 0; i < tokens.size(); i++) {
            const Token& t = tokens[i];
            switch (t.kind) {
            case TokenType::kString:
                // "list-style-type" is definitely not a <custom-ident>
                return;

            case TokenType::kUrl:
                if (!found_image) {
                    found_image = true;
                    continue;
                }
                break;

            case TokenType::kFunction:
                if (!found_image) {
                    std::string lower = helpers::ToLowerASCII(t.text);
                    if (lower == "src" || lower == "linear-gradient" ||
                        lower == "repeating-linear-gradient" || lower == "radial-gradient" ||
                        lower == "repeating-radial-gradient") {
                        found_image = true;
                        continue;
                    }
                }
                break;

            case TokenType::kIdent: {
                std::string lower = helpers::ToLowerASCII(t.text);

                // Note: If "none" is present, it's ambiguous whether it applies to
                // "list-style-image" or "list-style-type". To resolve ambiguity it's
                // applied at the end to whichever property isn't otherwise set.
                if (lower == "none") {
                    none_count++;
                    continue;
                }

                if (!found_position && (lower == "inside" || lower == "outside")) {
                    found_position = true;
                    continue;
                }

                if (type_index == -1) {
                    if (CssWideAndReservedKeywords().count(lower) != 0 ||
                        PredefinedCounterStyles().count(lower) != 0) {
                        // "list-style-type" is definitely not a <custom-ident>
                        return;
                    }
                    type_index = static_cast<int>(i);
                    continue;
                }
                break;
            }

            default:
                return;
            }

            // Bail if we hit an unexpected token
            return;
        }

        if (type_index != -1) {
            // The first "none" applies to "list-style-image" if it's missing
            if (!found_image && none_count > 0) {
                none_count--;
            }

            if (none_count > 0) {
                // "list-style-type" is "none", not a <custom-ident>
                return;
            }

            Token& t = tokens[static_cast<size_t>(type_index)];
            if (t.kind == TokenType::kIdent) {
                t.kind = TokenType::kSymbol;
                t.payload_index = SymbolForName(t.loc, t.text, context).ref.inner_index;
            }
        }
    }

    // Processes a standalone list-style-type declaration. If the token
    // is a custom identifier (not "none", not a css-wide keyword, and
    // not a predefined counter style), it is converted to a symbol token.
    //
    // Input:  token = "disc"
    // Output: token unchanged (predefined style)
    //
    // Input:  token = "none"
    // Output: token unchanged (reserved keyword)
    //
    // Input:  token = "my-counter"
    // Output: token = Symbol(my-counter)
    //
    // Input:  token = "inherit"
    // Output: token unchanged (css-wide keyword)
    //
    // Edge cases:
    //   - Only kIdent tokens are processed; other token types pass through.
    //   - The original token text is preserved; only kind and payload_index change.
    void ProcessListStyleType(Token* token, ListStyleSymbolContext& context) {
        if (token->kind == TokenType::kIdent) {
            std::string lower = helpers::ToLowerASCII(token->text);
            if (lower != "none" && CssWideAndReservedKeywords().count(lower) == 0 &&
                PredefinedCounterStyles().count(lower) == 0) {
                token->kind = TokenType::kSymbol;
                token->payload_index = SymbolForName(token->loc, token->text, context).ref.inner_index;
            }
        }
    }

}
