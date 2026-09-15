#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_properties.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"

namespace guchho::css {

    namespace {

        // Expands a CSS shorthand value of 1-4 tokens into a full quad
        // (top, right, bottom, left) following the standard CSS shorthand
        // expansion rules. If the input contains unexpected tokens (like
        // var() functions or custom ident values), expansion is refused.
        //
        // CSS shorthand expansion rules:
        //   1 token  -> all four sides get the same value
        //   2 tokens -> top/bottom get token 0, left/right get token 1
        //   3 tokens -> top gets token 0, left/right get token 1, bottom gets token 2
        //   4 tokens -> top=0, right=1, bottom=2, left=3
        //
        // Example:
        //   ExpandTokenQuad(["10px"], "")          -> ["10px", "10px", "10px", "10px"]
        //   ExpandTokenQuad(["10px", "20px"], "")  -> ["10px", "20px", "10px", "20px"]
        //   ExpandTokenQuad(["1px", "2px", "3px", "4px"], "") -> ["1px","2px","3px","4px"]
        //
        // Edge cases:
        //   - Empty input or more than 4 tokens returns false.
        //   - If allowed_ident is non-empty, ident tokens matching that
        //     string are treated as valid (used for keywords like "auto").
        //   - var() functions or other function tokens always cause failure.
        std::pair<std::array<Token, 4>, bool> ExpandTokenQuad(
            std::span<const Token> tokens,
            std::string_view allowed_ident
        ) {
            std::array<Token, 4> result;
            size_t n = tokens.size();
            if (n < 1 || n > 4) {
                return {result, false};
            }

            // Don't do this if we encounter any unexpected tokens such as "var()"
            for (size_t i = 0; i < n; i++) {
                const Token& t = tokens[i];
                if (!IsNumeric(t.kind) &&
                    (t.kind != TokenType::kIdent || allowed_ident.empty() || t.text != allowed_ident)) {
                    return {result, false};
                }
            }

            result[0] = tokens[0];
            if (n > 1) {
                result[1] = tokens[1];
            } else {
                result[1] = result[0];
            }
            if (n > 2) {
                result[2] = tokens[2];
            } else {
                result[2] = result[0];
            }
            if (n > 3) {
                result[3] = tokens[3];
            } else {
                result[3] = result[1];
            }
            return {result, true};
        }

        // Lowers the CSS `inset` shorthand property into four individual
        // directional declarations (top, right, bottom, left). The `inset`
        // property is a shorthand for positioning offsets, similar to how
        // `margin` and `padding` work.
        //
        // Returns a pair of (vector of rules, success flag). On failure
        // (e.g., var() in the value), returns empty vector and false.
        //
        // Example:
        //   Input:  inset: 10px 20px
        //   Output: top: 10px; right: 20px; bottom: 10px; left: 20px
        //
        //   Input:  inset: 1px 2px 3px 4px
        //   Output: top: 1px; right: 2px; bottom: 3px; left: 4px
        //
        // Edge cases:
        //   - Whitespace between tokens is preserved or stripped based on
        //     the minify_whitespace flag.
        //   - The !important flag is propagated to all four declarations.
        //   - Returns false if the value can't be expanded (var(), calc(),
        //     or other non-numeric/non-ident tokens).
        std::pair<std::vector<Rule>, bool> LowerInset(
            guchho::logger::Loc loc, 
            const RDeclaration& decl,
            bool minify_whitespace
        ) {
            auto [tokens, ok] = ExpandTokenQuad(decl.value, "");
            if (!ok) {
                return {{}, false};
            }

            WhitespaceFlags mask = static_cast<WhitespaceFlags>(
                ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
            if (minify_whitespace) {
                mask = WhitespaceFlags::kNone;
            }
            for (Token& t : tokens) {
                t.whitespace =
                    static_cast<WhitespaceFlags>(static_cast<uint8_t>(t.whitespace) & static_cast<uint8_t>(mask));
            }

            static constexpr std::string_view kSideTexts[] = {"top", "right", "bottom", "left"};
            static constexpr Declarations kSideKeys[] = {kDTop, kDRight, kDBottom, kDLeft};

            std::vector<Rule> result;
            result.reserve(4);
            for (int i = 0; i < 4; i++) {
                auto d = std::make_shared<RDeclaration>();
                d->key_text = std::string(kSideTexts[i]);
                d->key_range = decl.key_range;
                d->key = kSideKeys[i];
                d->value = std::vector<Token>{tokens[static_cast<size_t>(i)]};
                d->important = decl.important;
                result.push_back(Rule{std::move(d), loc});
            }
            return {std::move(result), true};
        }


        // Inserts a vendor-prefixed copy of a CSS declaration into the rule
        // list, positioned just before the original declaration. This function
        // handles several special cases where the prefix applies to the value
        // rather than the property name, or where only specific values need
        // the prefix.
        //
        // The function first checks whether a prefixed version of this
        // property already exists in the rule list. If so, it skips insertion
        // to avoid duplicates.
        //
        // Special cases handled:
        //   - background-clip: only prefixed for "text" value
        //   - position: only prefixed for "sticky" value, prefix goes on value
        //   - width/height/min-*/max-*: only prefixed for "stretch" value
        //   - user-select: -moz-none replaces "none" under -moz- prefix
        //   - mask-composite: WebKit uses different value names (add->source-over, etc.)
        //
        // Example:
        //   Input:  display: flex  with prefix "-webkit-"
        //   Output: -webkit-display: flex; display: flex;
        //
        //   Input:  position: sticky  with prefix "-webkit-"
        //   Output: position: -webkit-sticky; position: sticky;
        //
        // Edge cases:
        //   - If declaration_keys already contains the prefixed key, no
        //     insertion occurs (prevents duplicates).
        //   - When the key_text doesn't change (prefix on value), a linear
        //     scan checks for duplicate rules with identical values.
        //   - The original declaration is always re-appended after the
        //     prefixed one to maintain cascade order.
        std::vector<Rule> InsertPrefixedDeclaration(
            std::vector<Rule> rules, std::string_view prefix,
            guchho::logger::Loc loc,
            const std::shared_ptr<RDeclaration>& decl,
            const std::unordered_map<std::string, bool>& declaration_keys
        ) {
            std::string key_text = std::string(prefix) + decl->key_text;

            // Don't insert a prefixed declaration if there already is one
            if (declaration_keys.count(key_text) != 0) {
                // We found a previous declaration with a matching prefixed property.
                // The value is ignored, which matches the behavior of "autoprefixer".
                return rules;
            }

            // Additional special cases for when the prefix applies
            switch (decl->key) {
            case kDBackgroundClip:
                // The prefix is only needed for "background-clip: text"
                if (decl->value.size() != 1 || decl->value[0].kind != TokenType::kIdent ||
                    !helpers::EqualFoldASCII(decl->value[0].text, "text")) {
                    return rules;
                }
                break;

            case kDPosition:
                // The prefix is only needed for "position: sticky"
                if (decl->value.size() != 1 || decl->value[0].kind != TokenType::kIdent ||
                    !helpers::EqualFoldASCII(decl->value[0].text, "sticky")) {
                    return rules;
                }
                break;

            case kDWidth:
            case kDMinWidth:
            case kDMaxWidth:
            case kDHeight:
            case kDMinHeight:
            case kDMaxHeight:
                // The prefix is only needed for "width: stretch"
                if (decl->value.size() != 1 || decl->value[0].kind != TokenType::kIdent ||
                    !helpers::EqualFoldASCII(decl->value[0].text, "stretch")) {
                    return rules;
                }
                break;

            default:
                break;
            }

            std::vector<Token> value = CloneTokensWithoutImportRecords(decl->value);

            // Additional special cases for how to transform the contents
            switch (decl->key) {
            case kDPosition:
                // The prefix applies to the value, not the property
                key_text = decl->key_text;
                value[0].text = "-webkit-sticky";
                break;

            case kDWidth:
            case kDMinWidth:
            case kDMaxWidth:
            case kDHeight:
            case kDMinHeight:
            case kDMaxHeight:
                // The prefix applies to the value, not the property
                key_text = decl->key_text;

                // This currently only applies to "stretch" (already checked above)
                if (prefix == "-webkit-") {
                    value[0].text = "-webkit-fill-available";
                } else if (prefix == "-moz-") {
                    value[0].text = "-moz-available";
                }
                break;

            case kDUserSelect:
                // The prefix applies to the value as well as the property
                if (prefix == "-moz-" && value.size() == 1 && value[0].kind == TokenType::kIdent &&
                    helpers::EqualFoldASCII(value[0].text, "none")) {
                    value[0].text = "-moz-none";
                }
                break;

            case kDMaskComposite:
                // WebKit uses different names for these values
                if (prefix == "-webkit-") {
                    for (Token& token : value) {
                        if (token.kind == TokenType::kIdent) {
                            if (token.text == "add") {
                                token.text = "source-over";
                            } else if (token.text == "subtract") {
                                token.text = "source-out";
                            } else if (token.text == "intersect") {
                                token.text = "source-in";
                            } else if (token.text == "exclude") {
                                token.text = "xor";
                            }
                        }
                    }
                }
                break;

            default:
                break;
            }

            // If we didn't change the key, manually search for a previous duplicate rule
            if (key_text == decl->key_text) {
                for (const Rule& rule : rules) {
                    if (auto* prev_decl = dynamic_cast<const RDeclaration*>(rule.data.get())) {
                        if (prev_decl->key_text == key_text && TokensEqual(prev_decl->value, value, nullptr)) {
                            return rules;
                        }
                    }
                }
            }

            // Overwrite the latest declaration with the prefixed declaration
            auto prefixed = std::make_shared<RDeclaration>();
            prefixed->key_text = key_text;
            prefixed->key_range = decl->key_range;
            prefixed->value = std::move(value);
            prefixed->important = decl->important;
            rules[rules.size() - 1] = Rule{std::move(prefixed), loc};

            // Re-add the latest declaration after the inserted declaration
            rules.push_back(Rule{decl, loc});
            return rules;
        }

    }

    // Main entry point for processing CSS declarations. This function takes
    // a list of CSS rules and applies a series of transformations:
    //
    //   1. Shorthand lowering (inset -> top/right/bottom/left)
    //   2. Color lowering and minification
    //   3. Gradient lowering
    //   4. Transform, box-shadow, and font mangling
    //   5. Box model property mangling (margin, padding, inset, border-radius)
    //   6. CSS vendor prefix insertion
    //   7. Composes directive processing (for CSS Modules)
    //   8. Container/animation/list-style symbol tracking
    //   9. Color gamut clipping (for unsupported color functions)
    //
    // The function modifies rules in-place and returns the rewritten list.
    // Removed rules (like composes directives) are compacted at the end
    // when minify_syntax is enabled.
    //
    // Example:
    //   Input:  { inset: 10px 20px; color: oklch(50% 0.1 200); }
    //   Output: { top: 10px; right: 20px; bottom: 10px; left: 20px;
    //             color: rgb(128 48 180); }  (with color lowered)
    //
    // Edge cases:
    //   - var() and other dynamic values prevent shorthand expansion;
    //     the original declaration is kept as-is.
    //   - The inset property is only lowered if the target doesn't support
    //     CSSFeature::kInsetProperty.
    //   - Color clipping only occurs for the first declaration of a given
    //     property; subsequent declarations with the same key are not
    //     clipped to avoid overwriting user-specified fallbacks.
    //   - The composes_context parameter being non-null indicates we're in
    //     CSS Modules mode, where composes directives are processed and
    //     removed rather than kept as regular declarations.
    //   - declaration_keys is lazily built on first use for vendor prefix
    //     deduplication, avoiding unnecessary work when no prefixes are needed.
    std::vector<Rule> ProcessDeclarations(
        std::vector<Rule> rules,
        const DeclarationsOptions& options,
        const ComposesContext* composes_context,
        DeclarationsSymbolContext& symbol_context,
        const DeclarationsLogContext& log_context
    ) {
        BoxTracker margin;
        margin.key = kDMargin;
        margin.key_text = "margin";
        margin.allow_auto = true;
        BoxTracker padding;
        padding.key = kDPadding;
        padding.key_text = "padding";
        padding.allow_auto = false;
        BoxTracker inset;
        inset.key = kDInset;
        inset.key_text = "inset";
        inset.allow_auto = true;
        BorderRadiusTracker border_radius;

        std::vector<Rule> rewritten_rules;
        rewritten_rules.reserve(rules.size());
        bool did_warn_about_composes = false;
        bool would_clip_color_flag = false;
        std::unordered_map<std::string, bool> declaration_keys;
        bool declaration_keys_built = false;

        // Don't automatically generate the "inset" property if it's not supported
        if (Has(options.unsupported_css_features, guchho::compat::CSSFeature::kInsetProperty)) {
            inset.key = kDUnknown;
            inset.key_text.clear();
        }

        // If this is a local class selector, track which CSS properties it declares.
        // This is used to warn when CSS "composes" is used incorrectly.
        if (composes_context != nullptr) {
            for (const guchho::compiler::Ref& ref : composes_context->parent_refs) {
                std::shared_ptr<Composes>& composes = (*symbol_context.composes)[ref];
                if (!composes) {
                    composes = std::make_shared<Composes>();
                }
                for (const Rule& rule : rules) {
                    if (auto* decl = dynamic_cast<const RDeclaration*>(rule.data.get())) {
                        if (decl->key != kDComposes) {
                            composes->properties[decl->key_text] = decl->key_range.loc;
                        }
                    }
                }
            }
        }

        // These contexts are used to create CSS symbols for the relevant properties
        ContainerSymbolContext container_context;
        container_context.symbols = symbol_context.symbols;
        container_context.local_symbols = symbol_context.local_symbols;
        container_context.local_scope = symbol_context.local_scope;
        container_context.global_scope = symbol_context.global_scope;
        container_context.source_index = symbol_context.source_index;
        container_context.make_local_symbols = symbol_context.make_local_symbols;

        AnimationSymbolContext animation_context;
        animation_context.symbols = symbol_context.symbols;
        animation_context.local_symbols = symbol_context.local_symbols;
        animation_context.local_scope = symbol_context.local_scope;
        animation_context.global_scope = symbol_context.global_scope;
        animation_context.source_index = symbol_context.source_index;
        animation_context.make_local_symbols = symbol_context.make_local_symbols;

        ListStyleSymbolContext list_style_context;
        list_style_context.symbols = symbol_context.symbols;
        list_style_context.local_symbols = symbol_context.local_symbols;
        list_style_context.local_scope = symbol_context.local_scope;
        list_style_context.global_scope = symbol_context.global_scope;
        list_style_context.source_index = symbol_context.source_index;
        list_style_context.make_local_symbols = symbol_context.make_local_symbols;

        ColorDeclOptions color_options{options.unsupported_css_features, options.minify_syntax,
                                    options.minify_whitespace};

        for (size_t i = 0; i < rules.size(); i++) {
            Rule rule = rules[i];
            rewritten_rules.push_back(rule);
            std::shared_ptr<RDeclaration> decl_ptr =
                std::dynamic_pointer_cast<RDeclaration>(rule.data);
            if (!decl_ptr) {
                continue;
            }
            RDeclaration* decl = decl_ptr.get();

            // If the previous loop iteration would have clipped a color, we will
            // duplicate it and insert the clipped copy before the unclipped copy
            bool* would_clip_color = nullptr;
            if (would_clip_color_flag) {
                would_clip_color_flag = false;
                RDeclaration clone = *decl;
                clone.value = CloneTokensWithoutImportRecords(clone.value);
                auto clone_ptr = std::make_shared<RDeclaration>(std::move(clone));
                decl = clone_ptr.get();
                decl_ptr = clone_ptr;
                rule.data = clone_ptr;
                size_t n = rewritten_rules.size() - 2;
                rewritten_rules.pop_back();
                rewritten_rules.insert(rewritten_rules.begin() + static_cast<ptrdiff_t>(n), rule);
            } else {
                would_clip_color = &would_clip_color_flag;
            }

            switch (decl->key) {
            case kDComposes:
                // Only process "composes" directives if we're in "local-css" or
                // "global-css" mode. In these cases, "composes" directives will always
                // be removed (because they are being processed) even if they contain
                // errors. Otherwise we leave "composes" directives there untouched and
                // don't check them for errors.
                if (options.symbol_mode_enabled) {
                    if (composes_context == nullptr) {
                        if (!did_warn_about_composes) {
                            did_warn_about_composes = true;
                            log_context.log->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError,
                                                guchho::logger::MsgKind::kWarning, log_context.tracker,
                                                decl->key_range,
                                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ComposesNotValidHere));
                        }
                    } else if (composes_context->problem_range.len > 0) {
                        if (!did_warn_about_composes) {
                            did_warn_about_composes = true;
                            log_context.log->AddIDWithNotes(
                                guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                                log_context.tracker, decl->key_range,
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ComposesOnlySingleClass),
                                std::vector<guchho::logger::MsgData>{
                                    log_context.tracker->MakeMsgData(
                                        composes_context->problem_range,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ComposesOnlySingleClassNote))});
                        }
                    } else {
                        ComposesSymbolContext composes_symbol_context;
                        composes_symbol_context.composes = symbol_context.composes;
                        composes_symbol_context.import_records = symbol_context.import_records;
                        composes_symbol_context.symbols = symbol_context.symbols;
                        composes_symbol_context.local_symbols = symbol_context.local_symbols;
                        composes_symbol_context.local_scope = symbol_context.local_scope;
                        composes_symbol_context.global_scope = symbol_context.global_scope;
                        composes_symbol_context.source_index = symbol_context.source_index;
                        composes_symbol_context.make_local_symbols = symbol_context.make_local_symbols;
                        composes_symbol_context.source = log_context.source;
                        composes_symbol_context.log = log_context.log;
                        composes_symbol_context.tracker = log_context.tracker;
                        composes_symbol_context.prev_error = log_context.prev_error;
                        HandleComposesPragma(*composes_context, decl->value, composes_symbol_context);
                    }
                    rewritten_rules.pop_back();
                }
                break;

            case kDBackground:
                for (Token& t : decl->value) {
                    t = LowerAndMinifyColor(t, color_options, would_clip_color);
                    t = LowerAndMinifyGradient(t, color_options, would_clip_color);
                }
                break;

            case kDBackgroundImage:
            case kDBorderImage:
            case kDMaskImage:
                for (Token& t : decl->value) {
                    t = LowerAndMinifyGradient(t, color_options, would_clip_color);
                }
                break;

            case kDBackgroundColor:
            case kDBorderBlockEndColor:
            case kDBorderBlockStartColor:
            case kDBorderBottomColor:
            case kDBorderColor:
            case kDBorderInlineEndColor:
            case kDBorderInlineStartColor:
            case kDBorderLeftColor:
            case kDBorderRightColor:
            case kDBorderTopColor:
            case kDCaretColor:
            case kDColor:
            case kDColumnRuleColor:
            case kDFill:
            case kDFloodColor:
            case kDLightingColor:
            case kDOutlineColor:
            case kDStopColor:
            case kDStroke:
            case kDTextDecorationColor:
            case kDTextEmphasisColor:
                if (decl->value.size() == 1) {
                    decl->value[0] = LowerAndMinifyColor(decl->value[0], color_options, would_clip_color);
                }
                break;

            case kDTransform:
                if (options.minify_syntax) {
                    decl->value = MangleTransforms(std::move(decl->value));
                }
                break;

            case kDBoxShadow:
                decl->value = LowerAndMangleBoxShadows(std::move(decl->value), color_options,
                                                    options.minify_whitespace, would_clip_color);
                break;

            // Container name
            case kDContainer:
                ProcessContainerShorthand(decl->value, container_context);
                break;
            case kDContainerName:
                ProcessContainerName(decl->value, container_context);
                break;

            // Animation name
            case kDAnimation:
                ProcessAnimationShorthand(decl->value, animation_context);
                break;
            case kDAnimationName:
                ProcessAnimationName(decl->value, animation_context);
                break;

            // List style
            case kDListStyle:
                ProcessListStyleShorthand(decl->value, list_style_context);
                break;
            case kDListStyleType:
                if (decl->value.size() == 1) {
                    ProcessListStyleType(&decl->value[0], list_style_context);
                }
                break;

            // Font
            case kDFont:
                if (options.minify_syntax) {
                    decl->value = MangleFont(decl->value, options.minify_whitespace);
                }
                break;
            case kDFontFamily:
                if (options.minify_syntax) {
                    std::vector<Token> value;
                    if (MangleFontFamily(value, decl->value, options.minify_whitespace)) {
                        decl->value = std::move(value);
                    }
                }
                break;
            case kDFontWeight:
                if (decl->value.size() == 1 && options.minify_syntax) {
                    decl->value[0] = MangleFontWeight(decl->value[0]);
                }
                break;

            // Margin
            case kDMargin:
                if (options.minify_syntax) {
                    margin.MangleSides(rewritten_rules, decl, options.minify_whitespace);
                }
                break;
            case kDMarginTop:
                if (options.minify_syntax) {
                    margin.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxTop);
                }
                break;
            case kDMarginRight:
                if (options.minify_syntax) {
                    margin.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxRight);
                }
                break;
            case kDMarginBottom:
                if (options.minify_syntax) {
                    margin.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxBottom);
                }
                break;
            case kDMarginLeft:
                if (options.minify_syntax) {
                    margin.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxLeft);
                }
                break;

            // Padding
            case kDPadding:
                if (options.minify_syntax) {
                    padding.MangleSides(rewritten_rules, decl, options.minify_whitespace);
                }
                break;
            case kDPaddingTop:
                if (options.minify_syntax) {
                    padding.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxTop);
                }
                break;
            case kDPaddingRight:
                if (options.minify_syntax) {
                    padding.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxRight);
                }
                break;
            case kDPaddingBottom:
                if (options.minify_syntax) {
                    padding.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxBottom);
                }
                break;
            case kDPaddingLeft:
                if (options.minify_syntax) {
                    padding.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxLeft);
                }
                break;

            // Inset
            case kDInset:
                if (Has(options.unsupported_css_features, guchho::compat::CSSFeature::kInsetProperty)) {
                    if (auto [decls, ok] = LowerInset(rule.loc, *decl, options.minify_whitespace); ok) {
                        rewritten_rules.pop_back();
                        for (size_t j = 0; j < decls.size(); j++) {
                            rewritten_rules.push_back(decls[j]);
                            if (options.minify_syntax) {
                                auto* sub_decl =
                                    dynamic_cast<RDeclaration*>(decls[j].data.get());
                                inset.MangleSide(rewritten_rules, sub_decl, options.minify_whitespace,
                                                static_cast<int>(j));
                            }
                        }
                        break;
                    }
                }
                if (options.minify_syntax) {
                    inset.MangleSides(rewritten_rules, decl, options.minify_whitespace);
                }
                break;
            case kDTop:
                if (options.minify_syntax) {
                    inset.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxTop);
                }
                break;
            case kDRight:
                if (options.minify_syntax) {
                    inset.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxRight);
                }
                break;
            case kDBottom:
                if (options.minify_syntax) {
                    inset.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxBottom);
                }
                break;
            case kDLeft:
                if (options.minify_syntax) {
                    inset.MangleSide(rewritten_rules, decl, options.minify_whitespace, kBoxLeft);
                }
                break;

            // Border radius
            case kDBorderRadius:
                if (options.minify_syntax) {
                    border_radius.MangleCorners(rewritten_rules, decl, options.minify_whitespace);
                }
                break;
            case kDBorderTopLeftRadius:
                if (options.minify_syntax) {
                    border_radius.MangleCorner(rewritten_rules, decl, options.minify_whitespace,
                                            kBorderRadiusTopLeft);
                }
                break;
            case kDBorderTopRightRadius:
                if (options.minify_syntax) {
                    border_radius.MangleCorner(rewritten_rules, decl, options.minify_whitespace,
                                            kBorderRadiusTopRight);
                }
                break;
            case kDBorderBottomRightRadius:
                if (options.minify_syntax) {
                    border_radius.MangleCorner(rewritten_rules, decl, options.minify_whitespace,
                                            kBorderRadiusBottomRight);
                }
                break;
            case kDBorderBottomLeftRadius:
                if (options.minify_syntax) {
                    border_radius.MangleCorner(rewritten_rules, decl, options.minify_whitespace,
                                            kBorderRadiusBottomLeft);
                }
                break;

            default:
                break;
            }

            if (options.css_prefix_data != nullptr) {
                auto it = options.css_prefix_data->find(decl->key);
                if (it != options.css_prefix_data->end()) {
                    // Only generate this map if it's needed
                    if (!declaration_keys_built) {
                        declaration_keys_built = true;
                        for (const Rule& r : rules) {
                            if (auto* d = dynamic_cast<const RDeclaration*>(r.data.get())) {
                                declaration_keys[d->key_text] = true;
                            }
                        }
                    }
                    guchho::compat::CSSPrefix prefixes = it->second;
                    if (Has(prefixes, guchho::compat::CSSPrefix::kWebkitPrefix)) {
                        rewritten_rules = InsertPrefixedDeclaration(std::move(rewritten_rules), "-webkit-",
                                                                    rule.loc, decl_ptr, declaration_keys);
                    }
                    if (Has(prefixes, guchho::compat::CSSPrefix::kKhtmlPrefix)) {
                        rewritten_rules = InsertPrefixedDeclaration(std::move(rewritten_rules), "-khtml-",
                                                                    rule.loc, decl_ptr, declaration_keys);
                    }
                    if (Has(prefixes, guchho::compat::CSSPrefix::kMozPrefix)) {
                        rewritten_rules = InsertPrefixedDeclaration(std::move(rewritten_rules), "-moz-",
                                                                    rule.loc, decl_ptr, declaration_keys);
                    }
                    if (Has(prefixes, guchho::compat::CSSPrefix::kMsPrefix)) {
                        rewritten_rules = InsertPrefixedDeclaration(std::move(rewritten_rules), "-ms-",
                                                                    rule.loc, decl_ptr, declaration_keys);
                    }
                    if (Has(prefixes, guchho::compat::CSSPrefix::kOPrefix)) {
                        rewritten_rules = InsertPrefixedDeclaration(std::move(rewritten_rules), "-o-",
                                                                    rule.loc, decl_ptr, declaration_keys);
                    }
                }
            }


            if (would_clip_color_flag) {
                if (Has(options.unsupported_css_features, guchho::compat::CSSFeature::kColorFunctions)) {
                    // Only do this if there was no previous instance of that property so
                    // we avoid overwriting any manually-specified fallback values
                    for (size_t j = rewritten_rules.size(); j >= 2; j--) {
                        if (auto* prev = dynamic_cast<const RDeclaration*>(rewritten_rules[j - 2].data.get())) {
                            if (prev->key == decl->key) {
                                would_clip_color_flag = false;
                                break;
                            }
                        }
                    }
                    if (would_clip_color_flag) {
                        // If the code above would have clipped a color outside of the sRGB
                        // gamut, process this rule again so we can generate the clipped
                        // version next time
                        i -= 1;
                        continue;
                    }
                }
                would_clip_color_flag = false;
            }
        }

        // Compact removed rules
        if (options.minify_syntax) {
            size_t end = 0;
            for (const Rule& rule : rewritten_rules) {
                if (rule.data != nullptr) {
                    rewritten_rules[end++] = rule;
                }
            }
            rewritten_rules.resize(end);
        }

        return rewritten_rules;
    }

} 
