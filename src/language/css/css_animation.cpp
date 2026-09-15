#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>
#include <unordered_set>

namespace guchho::css {

    namespace {

        // CSS-wide and reserved keywords that have special meaning in CSS
        // property values. These cannot be used as animation names because
        // they would be interpreted as keyword values rather than identifiers.
        //
        // Example:
        //   animation-name: none;  // "none" is a keyword, not a name
        //   animation-name: 'none'; // 'none' is a string, so it's a valid name
        const std::unordered_set<std::string>& CssWideAndReservedKeywords()
        {
            static const std::unordered_set<std::string> keywords = {
                // CSS-wide keywords: apply to all properties
                "initial",
                "inherit",
                "unset",

                // CSS reserved keyword: browser default
                "default",

                // Cascade-dependent keywords: revert cascade changes
                "revert",
                "revert-layer",
            };

            return keywords;
        }

        // Creates or retrieves a CSS symbol for the given animation name.
        // If the name already exists in the current scope, the existing
        // symbol is reused and its use count is incremented. Otherwise,
        // a new symbol is created and added to the appropriate scope.
        //
        // Example:
        //   SymbolForName(loc, "slide-in", context)
        //   // Creates a new symbol if "slide-in" doesn't exist, or
        //   // returns the existing one if it does.
        guchho::compiler::LocRef SymbolForName(
            guchho::logger::Loc loc, 
            const std::string& name,
            AnimationSymbolContext& context
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

        // Converts an animation name token into a symbol token so it can
        // be renamed during minification. CSS keywords like "none" are
        // skipped because they have special meaning and cannot be used
        // as animation names.
        //
        // For global symbols (make_local_symbols=false), string tokens
        // containing CSS keywords are also skipped because global symbols
        // are output directly and might be printed as keywords. Local
        // symbols are always renamed, so converting strings to symbols
        // is safe.
        //
        // Example:
        //   HandleSingleAnimationName(Token("slide-in"), context)
        //   // Token becomes {kind: kSymbol, payload_index: <ref>}
        //
        // Edge cases:
        //   - Token "none" is skipped (CSS keyword).
        //   - Token "initial" is skipped (CSS-wide keyword).
        //   - Quoted string "'none'" in global mode is skipped.
        //   - Quoted string "'none'" in local mode becomes a symbol.
        void HandleSingleAnimationName(Token* token, AnimationSymbolContext& context)
        {
            // Skip CSS keywords that have special meaning.
            // "animation-name: none" doesn't set a name called "none",
            // it removes the animation name. But "animation-name: 'none'"
            // sets an animation named "none".
            //
            // Global symbols are not converted if they contain keywords
            // because they appear directly in output and could be printed
            // as keywords. Local symbols are always renamed, so it's safe
            // to convert them.
            if ((token->kind == TokenType::kIdent ||
                (token->kind == TokenType::kString && !context.make_local_symbols)) &&
                IsInvalidAnimationName(token->text)) {
                return;
            }

            // Convert the animation name token to a symbol token.
            token->kind = TokenType::kSymbol;

            // Store the symbol's reference index in the token's payload.
            token->payload_index =
                SymbolForName(token->loc, token->text, context).ref.inner_index;
        }
    }

    // Parses the "animation" shorthand property and converts animation
    // names to symbols. The shorthand can contain multiple animations
    // separated by commas, each with sub-values in any order:
    // duration, timing-function, delay, iteration-count, direction,
    // fill-mode, play-state, and name.
    //
    // This function uses a greedy matching approach: it tries to match
    // each token against known keywords in order, and any remaining
    // ident or string token that doesn't match a keyword is treated as
    // the animation name.
    //
    // Example:
    //   ProcessAnimationShorthand("slide-in 1s ease-in-out", context)
    //   // "slide-in" is converted to a symbol, "1s" is duration,
    //   // "ease-in-out" is timing function.
    //
    //   ProcessAnimationShorthand("1s ease slide-in, 2s linear fade-out", context)
    //   // Two animations: first has name "slide-in", second has name "fade-out".
    //
    // Edge cases:
    //   - Commas reset all found flags, starting fresh for the next animation.
    //   - The "infinite" keyword is recognized as iteration-count, not name.
    //   - "none" is a valid fill-mode, not an animation name.
    //   - If multiple unrecognizable idents appear, only the first is treated
    //     as the name; subsequent ones fall through to default handling.
    void ProcessAnimationShorthand(std::vector<Token>& tokens, AnimationSymbolContext& context) {
        struct FoundFlags {
            bool timing_function{};
            bool iteration_count{};
            bool direction{};
            bool fill_mode{};
            bool play_state{};
            bool name{};
        };

        FoundFlags found;

        for (size_t i = 0; i < tokens.size(); i++) {
            Token& t = tokens[i];
            switch (t.kind) {
            case TokenType::kComma:
                // Reset flags when encountering a comma separator.
                // Each comma-separated animation is parsed independently.
                found = FoundFlags{};
                break;

            case TokenType::kNumber:
                if (!found.iteration_count) {
                    found.iteration_count = true;
                    continue;
                }
                break;

            case TokenType::kIdent: {
                std::string lowered = helpers::ToLowerASCII(t.text);
                if (!found.timing_function) {
                    if (lowered == "linear" || lowered == "ease" || lowered == "ease-in" ||
                        lowered == "ease-out" || lowered == "ease-in-out" || lowered == "step-start" ||
                        lowered == "step-end") {
                        found.timing_function = true;
                        continue;
                    }
                }

                if (!found.iteration_count && lowered == "infinite") {
                    found.iteration_count = true;
                    continue;
                }

                if (!found.direction) {
                    if (lowered == "normal" || lowered == "reverse" || lowered == "alternate" ||
                        lowered == "alternate-reverse") {
                        found.direction = true;
                        continue;
                    }
                }

                if (!found.fill_mode) {
                    if (lowered == "none" || lowered == "forwards" || lowered == "backwards" ||
                        lowered == "both") {
                        found.fill_mode = true;
                        continue;
                    }
                }

                if (!found.play_state) {
                    if (lowered == "running" || lowered == "paused") {
                        found.play_state = true;
                        continue;
                    }
                }

                if (!found.name) {
                    HandleSingleAnimationName(&tokens[i], context);
                    found.name = true;
                    continue;
                }
                break;
            }

            case TokenType::kString:
                if (!found.name) {
                    HandleSingleAnimationName(&tokens[i], context);
                    found.name = true;
                    continue;
                }
                break;

            default:
                break;
            }
        }
    }

    // Processes the "animation-name" property, converting each ident or
    // string token to a symbol. Unlike the shorthand, this property only
    // contains animation names (comma-separated).
    //
    // Example:
    //   ProcessAnimationName("slide-in, fade-out", context)
    //   // Both "slide-in" and "fade-out" become symbols.
    //
    // Edge cases:
    //   - CSS keywords like "none" are skipped by HandleSingleAnimationName.
    //   - Multiple comma-separated names are all processed.
    void ProcessAnimationName(std::vector<Token>& tokens, AnimationSymbolContext& context) {
        for (size_t i = 0; i < tokens.size(); i++) {
            Token& t = tokens[i];
            if (t.kind == TokenType::kIdent || t.kind == TokenType::kString) {
                HandleSingleAnimationName(&tokens[i], context);
            }
        }
    }

    // Returns true if the given text is not a valid animation name.
    // CSS keywords and reserved words cannot be used as animation names
    // because they have special meaning in CSS property values.
    //
    // Example:
    //   IsInvalidAnimationName("none")     -> true  (CSS keyword)
    //   IsInvalidAnimationName("initial")  -> true  (CSS-wide keyword)
    //   IsInvalidAnimationName("slide-in") -> false (valid name)
    //   IsInvalidAnimationName("my-animation") -> false (valid name)
    //
    // Edge cases:
    //   - Case-insensitive comparison: "None", "NONE" are also invalid.
    //   - "revert" and "revert-layer" are cascade-dependent keywords.
    bool IsInvalidAnimationName(std::string_view text) {
        std::string lower = helpers::ToLowerASCII(text);
        return lower == "none" || CssWideAndReservedKeywords().count(lower) != 0;
    }
}
