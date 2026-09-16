#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/css/css_parser.hpp"
#include "guchho/logger.hpp"


namespace guchho::css {

    namespace {

        // MergeCompoundSelectors
        // ----------------------
        // Merges a source compound selector into a target compound
        // selector.  This is used when flattening :local() and :global()
        // pseudo-class selectors that contain inner compound selectors.
        //
        // The merge follows these rules:
        //
        //   1. Nesting selectors (&) from the source are copied only if
        //      the target has none.
        //
        //   2. If the source has a type selector (e.g. "div") and the
        //      target does not, the type selector is moved to the target.
        //
        //   3. If both have type selectors, the source's type selector is
        //      wrapped in an :is() pseudo-class and appended as a subclass
        //      selector.  This avoids the obviously-wrong concatenation
        //      that other implementations produce.
        //
        //   4. All subclass selectors from the source are appended to the
        //      target.
        //
        // Input:  target = ".foo", source = {type_selector="div"}
        // Output: target = "div.foo"  (type moved to front)
        //
        // Input:  target = {type_selector="div"}, source = {type_selector="span"}
        // Output: target = "div:is(span)"  (wrapped in :is())
        //
        // Input:  target = ".foo", source = ".bar"
        // Output: target = ".foo.bar"  (subclass selectors concatenated)
        void MergeCompoundSelectors(CompoundSelector& target, const CompoundSelector& source) {
            if (!source.nesting_selector_locs.empty() && target.nesting_selector_locs.empty()) {
                target.nesting_selector_locs = source.nesting_selector_locs;
            }

            if (source.type_selector != nullptr) {
                if (target.type_selector == nullptr) {
                    target.type_selector = source.type_selector;
                } else {
                    SubclassSelector ss;
                    ss.range = source.type_selector->Range();
                    auto data = std::make_shared<SSPseudoClassWithSelectorList>();
                    data->kind = PseudoClassKind::kPseudoClassIs;
                    CompoundSelector inner;
                    inner.type_selector = source.type_selector;
                    ComplexSelector complex;
                    complex.selectors.push_back(std::move(inner));
                    data->selectors.push_back(std::move(complex));
                    ss.data = std::move(data);
                    target.subclass_selectors.push_back(std::move(ss));
                }
            }

            target.subclass_selectors.insert(target.subclass_selectors.end(), source.subclass_selectors.begin(),
                                            source.subclass_selectors.end());
        }

        // ContainsLocalOrGlobalSelector
        // -----------------------------
        // Returns true when the complex selector contains a :local or
        // :global pseudo-class at any nesting depth.  This check is used
        // to decide whether the selector needs to be flattened.
        //
        // The function inspects both bare pseudo-classes (":local",
        // ":global") and pseudo-classes with selector lists
        // (":local(.foo)", ":global(.bar)").
        //
        // Input:  sel = ComplexSelector containing ":local(.foo)"
        // Output: true
        //
        // Input:  sel = ComplexSelector containing ":hover"
        // Output: false
        bool ContainsLocalOrGlobalSelector(const ComplexSelector& sel) {
            for (const CompoundSelector& s : sel.selectors) {
                for (const SubclassSelector& ss : s.subclass_selectors) {
                    if (auto* pseudo = dynamic_cast<SSPseudoClass*>(ss.data.get()); pseudo) {
                        if (pseudo->name == "global" || pseudo->name == "local") {
                            return true;
                        }
                    } else if (auto* pseudo2 = dynamic_cast<SSPseudoClassWithSelectorList*>(ss.data.get()); pseudo2) {
                        if (pseudo2->kind == PseudoClassKind::kPseudoClassGlobal ||
                            pseudo2->kind == PseudoClassKind::kPseudoClassLocal) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        // AnalyzeLeadingAmpersand
        // -----------------------
        // Determines whether the leading "&" nesting selector in a
        // complex selector can be safely removed during minification.
        // The analysis distinguishes three cases:
        //
        //   kCanAlwaysRemove    The ampersand is the first compound and
        //                       is always redundant (e.g. "& + div",
        //                       "& div", "& :hover").
        //
        //   kCanRemoveIfNotFirst The ampersand is redundant only if it
        //                       appears in a non-first position in the
        //                       selector list (e.g. ".bar, & div" can
        //                       become ".bar, div" but "& div" alone
        //                       cannot be reduced to just "div" in a
        //                       declaration context).
        //
        //   kCannotRemove       The ampersand is required (e.g. "& &.bar",
        //                       bare "& {}", or inside a pseudo-class
        //                       like :has()).
        //
        // The `is_declaration_context` flag indicates whether the selector
        // appears in a rule's selector list (true) or inside a pseudo-
        // class argument (false).  In pseudo-class contexts the ampersand
        // cannot be removed.
        //
        // Input:  sel = "& + div", is_declaration_context = true
        // Output: kCanAlwaysRemove
        //
        // Input:  sel = "& &.bar", is_declaration_context = true
        // Output: kCannotRemove
        //
        // Input:  sel = "& div", is_declaration_context = true
        // Output: kCanRemoveIfNotFirst
        LeadingAmpersand AnalyzeLeadingAmpersand(const ComplexSelector& sel, bool is_declaration_context) {
            if (sel.selectors.size() > 1) {
                const CompoundSelector& first = sel.selectors[0];
                if (first.IsSingleAmpersand()) {
                    const CompoundSelector& second = sel.selectors[1];
                    if (second.combinator.byte_ == 0 && !second.nesting_selector_locs.empty()) {
                        // ".foo { & &.bar {} }" => ".foo { & &.bar {} }"
                    } else if (second.combinator.byte_ != 0 || second.type_selector == nullptr || !is_declaration_context) {
                        return LeadingAmpersand::kCanAlwaysRemove;
                    } else {
                        return LeadingAmpersand::kCanRemoveIfNotFirst;
                    }
                }
            } else {
                // "& {}" => "& {}"
            }
            return LeadingAmpersand::kCannotRemove;
        }

    }

    // ParseSelectorList
    // -----------------
    // Parses a comma-separated list of complex selectors.  This is the
    // entry point for parsing the selector portion of a CSS rule or a
    // pseudo-class argument like :is() or :has().
    //
    // The function handles three concerns:
    //
    //   1. Parsing each complex selector via ParseComplexSelector and
    //      collecting them into a vector.
    //
    //   2. Flattening :local/:global pseudo-classes via
    //      FlattenLocalAndGlobalSelectors.
    //
    //   3. Removing redundant leading "&" nesting selectors when
    //      minifying.
    //
    // When `opts.only_one_complex_selector` is true (used for :local
    // and :global), a trailing comma is treated as an error rather than
    // a separator.
    //
    // When `opts.is_forgiving_selector_list` is true (used for :is()
    // and :where()), an empty selector list is accepted.
    //
    // Duplicate selectors are omitted when minify_syntax is enabled.
    //
    // Input:  "a, b, c"   =>  3-element list of ComplexSelectors
    // Input:  ":is(a, b)" =>  2-element list inside :is()
    // Input:  ""          =>  empty list (forgiving mode)
    //
    // Edge case: when only_one_complex_selector is true and a comma is
    // found, a warning is emitted and parsing stops.
    std::pair<std::vector<ComplexSelector>, bool> Parser::ParseSelectorList(const ParseSelectorOpts& opts) {
        std::vector<ComplexSelector> list;

        // Potentially parse an empty list for ":is()" and ":where()"
        if (opts.is_forgiving_selector_list && opts.stop_on_close_paren && Peek(TokenType::kCloseParen)) {
            return {list, true};
        }

        // Parse the first selector
        ParseComplexSelectorOpts first_opts;
        first_opts.parse_selector_opts = opts;
        first_opts.is_first = true;
        auto [sel, good] = ParseComplexSelector(first_opts);
        if (!good) {
            return {list, false};
        }
        list = FlattenLocalAndGlobalSelectors(std::move(list), sel);

        // Parse the remaining selectors
        if (opts.only_one_complex_selector) {
            if (Current().kind == TokenType::kComma) {
                lexer::Token t = Current();
                prev_error_ = t.range.loc;
                std::string kind = ":" + std::string(ToString(opts.pseudo_class_kind)) + "(...)";
                std::vector<guchho::logger::MsgData> notes;
                guchho::logger::MsgData note;
                note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnexpectedCommaInsideNote, kind);
                notes.push_back(std::move(note));
                log_->AddIDWithNotes(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning,
                                    &tracker_, t.range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnexpectedCommaInside, kind),
                                    notes);
                return {list, false};
            }
        } else {
            while (true) {
                Eat(TokenType::kWhitespace);
                if (!Eat(TokenType::kComma)) {
                    break;
                }
                Eat(TokenType::kWhitespace);

                ParseComplexSelectorOpts sel_opts;
                sel_opts.parse_selector_opts = opts;
                auto [sel2, good2] = ParseComplexSelector(sel_opts);
                if (!good2) {
                    return {list, false};
                }

                // Omit duplicate selectors
                if (options_.minify_syntax) {
                    bool duplicate = false;
                    for (const ComplexSelector& existing : list) {
                        if (sel2.Equal(existing, nullptr)) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        continue;
                    }
                }

                list = FlattenLocalAndGlobalSelectors(std::move(list), sel2);
            }
        }

        // Remove the leading ampersand when minifying and it can be implied.
        // The rules for removal depend on whether the selector is first in
        // the list and whether it appears in a declaration context.
        if (options_.minify_syntax && !opts.stop_on_close_paren) {
            for (size_t i = 1; i < list.size(); ++i) {
                if (AnalyzeLeadingAmpersand(list[i], opts.is_declaration_context) != LeadingAmpersand::kCannotRemove) {
                    list[i].selectors.erase(list[i].selectors.begin());
                }
            }

            switch (AnalyzeLeadingAmpersand(list[0], opts.is_declaration_context)) {
            case LeadingAmpersand::kCanAlwaysRemove:
                list[0].selectors.erase(list[0].selectors.begin());
                break;

            case LeadingAmpersand::kCanRemoveIfNotFirst:
                for (size_t i = 1; i < list.size(); ++i) {
                    const CompoundSelector& first = list[i].selectors[0];
                    if (first.nesting_selector_locs.empty() &&
                        (first.combinator.byte_ != 0 || first.type_selector == nullptr)) {
                        list[0].selectors.erase(list[0].selectors.begin());
                        std::swap(list[0], list[i]);
                        break;
                    }
                }
                break;

            default:
                break;
            }
        }

        return {std::move(list), true};
    }

    // FlattenLocalAndGlobalSelectors
    // ------------------------------
    // Handles the :local() and :global() CSS Modules annotations by
    // expanding them into plain selectors.  When symbol_mode is
    // disabled the function is a no-op.
    //
    // The flattening process:
    //
    //   1. Walk every compound selector in the complex selector.
    //
    //   2. For each subclass selector, check if it is a :local or
    //      :global pseudo-class.
    //
    //   3. Bare :local/:global (without arguments) are simply removed.
    //
    //   4. :local(.foo .bar) is expanded: the first inner compound
    //      selector is merged with the compound before it, the last
    //      inner compound is merged with the compound after it, and
    //      any middle compounds become new entries in the list.
    //
    //   5. If the entire complex selector consisted only of bare
    //      :local/:global, a synthetic "&" nesting selector is
    //      inserted so the selector remains valid.
    //
    // Input:  ".foo:local(.bar .baz):hover" (symbol_mode enabled)
    // Output: ".foo.bar .baz:hover"
    //
    // Input:  ":local" (symbol_mode enabled)
    // Output: "&"  (synthetic nesting selector)
    //
    // Edge case: compounds that become empty after stripping :local/:global
    // are silently dropped from the output.
    std::vector<ComplexSelector> Parser::FlattenLocalAndGlobalSelectors(std::vector<ComplexSelector> list,
                                                                        const ComplexSelector& sel_in) {
        ComplexSelector sel = sel_in;

        // Only do the work to flatten the whole list if there's a ":local" or a ":global"
        if (options_.symbol_mode != SymbolMode::kDisabled && ContainsLocalOrGlobalSelector(sel)) {
            std::vector<CompoundSelector> selectors;

            for (const CompoundSelector& s_in : sel.selectors) {
                CompoundSelector s = s_in;
                std::vector<SubclassSelector> old_subclass_selectors = std::move(s.subclass_selectors);
                s.subclass_selectors.clear();
                s.subclass_selectors.reserve(old_subclass_selectors.size());

                for (SubclassSelector& ss : old_subclass_selectors) {
                    bool is_local_or_global = false;
                    if (auto* pseudo = dynamic_cast<SSPseudoClass*>(ss.data.get()); pseudo) {
                        if (pseudo->name == "global" || pseudo->name == "local") {
                            // Remove bare ":global" and ":local" pseudo-classes
                            is_local_or_global = true;
                        }
                    } else if (auto* pseudo2 = dynamic_cast<SSPseudoClassWithSelectorList*>(ss.data.get()); pseudo2) {
                        if (pseudo2->kind == PseudoClassKind::kPseudoClassGlobal ||
                            pseudo2->kind == PseudoClassKind::kPseudoClassLocal) {
                            is_local_or_global = true;
                            std::vector<CompoundSelector> inner = pseudo2->selectors[0].selectors;

                            // Replace this pseudo-class with all inner compound selectors.
                            // The first inner compound selector is merged with the compound
                            // selector before it and the last inner compound selector is
                            // merged with the compound selector after it:
                            //
                            // "div:local(.a .b):hover" => "div.a b:hover"
                            //
                            // This behavior is really strange since this is not how anything
                            // involving pseudo-classes in real CSS works at all. However, all
                            // other implementations (Lightning CSS, PostCSS, and Webpack) are
                            // consistent with this strange behavior, so we do it too.
                            if (inner[0].combinator.byte_ == 0) {
                                MergeCompoundSelectors(s, inner[0]);
                                inner.erase(inner.begin());
                            } else {
                                // "div:local(+ .foo):hover" => "div + .foo:hover"
                            }
                            if (!inner.empty()) {
                                if (!s.IsInvalidBecauseEmpty()) {
                                    // Don't add this selector if it consisted only of a bare ":global" or ":local"
                                    selectors.push_back(s);
                                }
                                selectors.insert(selectors.end(), inner.begin(), inner.end() - 1);
                                s = std::move(inner.back());
                            }
                        }
                    }

                    if (!is_local_or_global) {
                        s.subclass_selectors.push_back(std::move(ss));
                    }
                }

                if (!s.IsInvalidBecauseEmpty()) {
                    // Don't add this selector if it consisted only of a bare ":global" or ":local"
                    selectors.push_back(s);
                }
            }

            if (selectors.empty()) {
                // Treat a bare ":global" or ":local" as a bare "&" nesting selector
                CompoundSelector compound;
                compound.nesting_selector_locs.push_back(sel.selectors[0].Range().loc);
                compound.was_empty_from_local_or_global = true;
                selectors.push_back(std::move(compound));

                // Make sure we report that nesting is present so that it can be lowered
                nesting_is_present_ = true;
            }

            sel.selectors = std::move(selectors);
        }

        list.push_back(std::move(sel));
        return list;
    }

    // ParseComplexSelector
    // --------------------
    // Parses a complex selector, which is a chain of compound selectors
    // connected by combinators (descendant, child, sibling).  The CSS
    // nesting spec allows a leading combinator (e.g. "& + div") which
    // is parsed when no_leading_combinator is false.
    //
    // The function first optionally parses a leading combinator, then
    // loops to parse each subsequent compound selector and its
    // preceding combinator.  Parsing stops at EOF, comma, or the
    // closing delimiter (brace or paren).
    //
    // Input:  "a > b + c"
    // Output: ComplexSelector with three compounds and two combinators
    //
    // Input:  "& .foo"  (nesting context)
    // Output: ComplexSelector starting with ampersand compound
    //
    // Edge case: an empty input produces a selector with no compounds
    // and the function returns true (the caller decides what to do).
    std::pair<ComplexSelector, bool> Parser::ParseComplexSelector(const ParseComplexSelectorOpts& opts) {
        // This is an extension: https://drafts.csswg.org/css-nesting-1/
        ComplexSelector result;
        Combinator combinator;
        if (!opts.parse_selector_opts.no_leading_combinator) {
            combinator = ParseCombinator();
            if (combinator.byte_ != 0) {
                nesting_is_present_ = true;
                Eat(TokenType::kWhitespace);
            }
        }

        // Parent
        ParseComplexSelectorOpts parent_opts;
        parent_opts.parse_selector_opts = opts.parse_selector_opts;
        parent_opts.is_first = opts.is_first;
        auto [sel, good] = ParseCompoundSelector(parent_opts);
        if (!good) {
            return {result, false};
        }
        sel.combinator = combinator;
        result.selectors.push_back(std::move(sel));

        TokenType stop = TokenType::kOpenBrace;
        if (opts.parse_selector_opts.stop_on_close_paren) {
            stop = TokenType::kCloseParen;
        }
        for (;;) {
            Eat(TokenType::kWhitespace);
            if (Peek(TokenType::kEndOfFile) || Peek(TokenType::kComma) || Peek(stop)) {
                break;
            }

            // Optional combinator
            combinator = ParseCombinator();
            if (combinator.byte_ != 0) {
                Eat(TokenType::kWhitespace);
            }

            // Child
            ParseComplexSelectorOpts child_opts;
            child_opts.parse_selector_opts = opts.parse_selector_opts;
            auto [sel2, good2] = ParseCompoundSelector(child_opts);
            if (!good2) {
                return {result, false};
            }
            sel2.combinator = combinator;
            result.selectors.push_back(std::move(sel2));
        }

        return {std::move(result), true};
    }

    // TokenName
    // ---------
    // Extracts the decoded text and metadata from the current token
    // and returns it as a NameToken.  This is used when parsing
    // type selectors, attribute names, and other identifier-like
    // tokens that may need namespace resolution.
    NameToken Parser::TokenName() {
        lexer::Token t = Current();
        NameToken result;
        result.kind = t.kind;
        result.range = t.range;
        result.text = Decoded();
        return result;
    }

    // ParseCompoundSelector
    // ---------------------
    // Parses a single compound selector, which consists of an optional
    // type selector followed by zero or more subclass selectors (IDs,
    // classes, attributes, pseudo-classes, pseudo-elements, and &
    // nesting selectors).
    //
    // The parsing order is:
    //   1. Optional leading "&" nesting selector.
    //   2. Optional type selector (element name, optionally namespaced).
    //   3. Subclass selectors in a loop: #id, .class, [attr], :pseudo,
    //      & nesting.
    //
    // After parsing, two validation checks are performed:
    //   - The compound must not be empty.
    //   - A type selector must not follow a nesting selector (&div is
    //     invalid per the CSS WG resolution).
    //
    // Input:  "div.foo#bar"
    // Output: CompoundSelector with type="div", class=".foo", id="#bar"
    //
    // Input:  "&:hover"
    // Output: CompoundSelector with nesting selector and :hover
    //
    // Edge case: "div:local(span)" with a conflicting type selector
    // wraps the second type in :is() rather than concatenating.
    std::pair<CompoundSelector, bool> Parser::ParseCompoundSelector(const ParseComplexSelectorOpts& opts) {
        guchho::logger::Loc start_loc = Current().range.loc;
        CompoundSelector sel;

        // This is an extension: https://drafts.csswg.org/css-nesting-1/
        bool has_leading_nesting_selector = Peek(TokenType::kDelimAmpersand);
        if (has_leading_nesting_selector) {
            nesting_is_present_ = true;
            sel.nesting_selector_locs.push_back(start_loc);
            Advance();
        }

        // Parse the type selector
        guchho::logger::Loc type_selector_loc = Current().range.loc;
        switch (Current().kind) {
        case TokenType::kDelimBar:
        case TokenType::kIdent:
        case TokenType::kDelimAsterisk: {
            auto ns_name = std::make_shared<NamespacedName>();
            if (!Peek(TokenType::kDelimBar)) {
                ns_name->name = TokenName();
                Advance();
            } else {
                // Hack: Create an empty "identifier" to represent this
                ns_name->name.kind = TokenType::kIdent;
            }
            if (Eat(TokenType::kDelimBar)) {
                if (!Peek(TokenType::kIdent) && !Peek(TokenType::kDelimAsterisk)) {
                    Expect(TokenType::kIdent);
                    return {sel, false};
                }
                auto prefix = ns_name->name;
                ns_name->namespace_prefix = std::make_shared<NameToken>(std::move(prefix));
                ns_name->name = TokenName();
                Advance();
            }
            sel.type_selector = std::move(ns_name);
            break;
        }

        default:
            break;
        }

        // Parse the subclass selectors
        for (;;) {
            lexer::Token subclass_token = Current();

            switch (subclass_token.kind) {
            case TokenType::kHash:
                if ((subclass_token.flags & kIsID) == 0) {
                    goto subclass_selectors_done;
                }
                {
                    guchho::logger::Loc name_loc{guchho::logger::Loc{subclass_token.range.loc.start + 1}};
                    std::string name = Decoded();
                    SubclassSelector ss;
                    ss.range = subclass_token.range;
                    auto data = std::make_shared<SSHash>();
                    data->name = SymbolForName(name_loc, name);
                    ss.data = std::move(data);
                    sel.subclass_selectors.push_back(std::move(ss));
                }
                Advance();
                break;

            case TokenType::kDelimDot:
                Advance();
                {
                    guchho::logger::Range name_range = Current().range;
                    std::string name = Decoded();
                    SubclassSelector ss;
                    ss.range = guchho::logger::Range{guchho::logger::Loc{subclass_token.range.loc.start},
                                                name_range.End() - subclass_token.range.loc.start};
                    auto data = std::make_shared<SSClass>();
                    data->name = SymbolForName(name_range.loc, name);
                    ss.data = std::move(data);
                    sel.subclass_selectors.push_back(std::move(ss));
                }
                if (!Expect(TokenType::kIdent)) {
                    return {sel, false};
                }
                break;

            case TokenType::kOpenBracket: {
                auto [attr, r] = ParseAttributeSelector();
                if (r.len == 0) {
                    return {sel, false};
                }
                SubclassSelector ss;
                ss.range = r;
                ss.data = std::make_shared<SSAttribute>(attr);
                sel.subclass_selectors.push_back(std::move(ss));
                break;
            }

            case TokenType::kColon:
                if (Next().kind == TokenType::kColon) {
                    // Special-case the start of the pseudo-element selector section
                    while (Peek(TokenType::kColon)) {
                        guchho::logger::Loc first_colon_loc = Current().range.loc;
                        bool is_element = Next().kind == TokenType::kColon;
                        if (is_element) {
                            Advance();
                        }
                        std::shared_ptr<SS> pseudo;
                        guchho::logger::Range r;
                        std::tie(pseudo, r) = ParsePseudoClassSelector(first_colon_loc, is_element);

                        // https://www.w3.org/TR/selectors-4/#single-colon-pseudos
                        // The four Level 2 pseudo-elements (::before, ::after, ::first-line,
                        // and ::first-letter) may, for legacy reasons, be represented using
                        // the <pseudo-class-selector> grammar, with only a single ":"
                        // character at their start.
                        if (options_.minify_syntax && is_element) {
                            if (auto* p = dynamic_cast<SSPseudoClass*>(pseudo.get()); p != nullptr && p->args == nullptr) {
                                if (p->name == "before" || p->name == "after" || p->name == "first-line" ||
                                    p->name == "first-letter") {
                                    p->is_element = false;
                                }
                            }
                        }

                        SubclassSelector ss;
                        ss.range = r;
                        ss.data = std::move(pseudo);
                        sel.subclass_selectors.push_back(std::move(ss));
                    }
                    goto subclass_selectors_done;
                }

                {
                    std::shared_ptr<SS> pseudo;
                    guchho::logger::Range r;
                    std::tie(pseudo, r) = ParsePseudoClassSelector(subclass_token.range.loc, false);
                    SubclassSelector ss;
                    ss.range = r;
                    ss.data = std::move(pseudo);
                    sel.subclass_selectors.push_back(std::move(ss));
                }
                break;

            case TokenType::kDelimAmpersand:
                // This is an extension: https://drafts.csswg.org/css-nesting-1/
                nesting_is_present_ = true;
                sel.nesting_selector_locs.push_back(subclass_token.range.loc);
                Advance();
                break;

            default:
                goto subclass_selectors_done;
            }
        }

    subclass_selectors_done:

        // The compound selector must be non-empty
        if (sel.IsInvalidBecauseEmpty()) {
            Unexpected();
            return {sel, false};
        }

        // Note: "&div {}" was originally valid, but is now an invalid selector:
        // https://github.com/w3c/csswg-drafts/issues/8662#issuecomment-1514977935.
        // This is because SASS already uses that syntax to mean something very
        // different, so that syntax has been removed to avoid mistakes.
        if (has_leading_nesting_selector && sel.type_selector != nullptr) {
            guchho::logger::Range r{guchho::logger::Loc{type_selector_loc.start},
                                At(index_ - 1).range.End() - type_selector_loc.start};
            std::string text = sel.type_selector->name.text;
            if (sel.type_selector->namespace_prefix != nullptr) {
                text = sel.type_selector->namespace_prefix->text + "|" + text;
            }
            std::string how_to_fix;
            std::string suggestion = source_->TextForRange(r);
            if (opts.is_first) {
                suggestion = ":is(" + suggestion + ")";
                how_to_fix = "You can wrap this selector in \":is(...)\" as a workaround. ";
            } else {
                r = guchho::logger::Range{guchho::logger::Loc{start_loc.start}, r.End() - start_loc.start};
                suggestion += "&";
                how_to_fix = "You can move the \"&\" to the end of this selector as a workaround. ";
            }
            guchho::logger::Msg msg;
            msg.kind = guchho::logger::MsgKind::kWarning;
            msg.data = tracker_.MakeMsgData(r,
                                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_TypeSelectorAfterNesting, text));
            guchho::logger::MsgData note;
            note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_TypeSelectorAfterNestingNotes);
            msg.notes.push_back(std::move(note));
            msg.data.location->suggestion = suggestion;
            log_->AddMsgID(guchho::logger::MsgID::kCSS_CSSSyntaxError, msg);
            return {sel, false};
        }

        // The type selector must always come first
        switch (Current().kind) {
        case TokenType::kDelimBar:
        case TokenType::kIdent:
        case TokenType::kDelimAsterisk:
            Unexpected();
            return {sel, false};
        default:
            break;
        }

        return {sel, true};
    }

    // ParseAttributeSelector
    // ----------------------
    // Parses an attribute selector in square brackets, e.g.
    // "[href]", "[type='text']", "[class~='active']".
    //
    // The function handles:
    //   1. Namespaced attribute names (e.g. "[xml|lang='en']").
    //   2. The optional matcher operator (=, ~=, |=, ^=, $=, *=).
    //   3. The optional matcher value (string or identifier).
    //   4. The optional case-sensitivity modifier (i or s).
    //
    // Input:  "[type='text']"
    // Output: SSAttribute{name="type", op="=", value="text"}
    //
    // Input:  "[class~='active' i]"
    // Output: SSAttribute{name="class", op="~=", value="active", modifier='i'}
    //
    // Input:  "[href]"
    // Output: SSAttribute{name="href"}  (presence check)
    //
    // Edge case: "[|attr]" is equivalent to "[attr]" per the spec —
    // default namespaces do not apply to attributes.
    std::pair<SSAttribute, guchho::logger::Range> Parser::ParseAttributeSelector() {
        guchho::logger::Loc matching_loc = Current().range.loc;
        Advance();
        SSAttribute attr;

        // Parse the namespaced name
        switch (Current().kind) {
        case TokenType::kDelimBar:
        case TokenType::kDelimAsterisk:
            // "[|x]"
            // "[*|x]"
            if (Peek(TokenType::kDelimAsterisk)) {
                NameToken prefix = TokenName();
                Advance();
                attr.namespaced_name.namespace_prefix = std::make_shared<NameToken>(std::move(prefix));
            } else {
                // "[|attr]" is equivalent to "[attr]". From the specification:
                // "In keeping with the Namespaces in the XML recommendation, default
                // namespaces do not apply to attributes, therefore attribute selectors
                // without a namespace component apply only to attributes that have no
                // namespace (equivalent to |attr)."
            }
            if (!Expect(TokenType::kDelimBar)) {
                return {attr, guchho::logger::Range{}};
            }
            attr.namespaced_name.name = TokenName();
            if (!Expect(TokenType::kIdent)) {
                return {attr, guchho::logger::Range{}};
            }
            break;

        default:
            // "[x]"
            // "[x|y]"
            attr.namespaced_name.name = TokenName();
            if (!Expect(TokenType::kIdent)) {
                return {attr, guchho::logger::Range{}};
            }
            if (Next().kind != TokenType::kDelimEquals && Eat(TokenType::kDelimBar)) {
                auto prefix = attr.namespaced_name.name;
                attr.namespaced_name.namespace_prefix = std::make_shared<NameToken>(std::move(prefix));
                attr.namespaced_name.name = TokenName();
                if (!Expect(TokenType::kIdent)) {
                    return {attr, guchho::logger::Range{}};
                }
            }
            break;
        }

        // Parse the optional matcher operator
        Eat(TokenType::kWhitespace);
        if (Eat(TokenType::kDelimEquals)) {
            attr.matcher_op = "=";
        } else {
            switch (Current().kind) {
            case TokenType::kDelimTilde:
                attr.matcher_op = "~=";
                break;
            case TokenType::kDelimBar:
                attr.matcher_op = "|=";
                break;
            case TokenType::kDelimCaret:
                attr.matcher_op = "^=";
                break;
            case TokenType::kDelimDollar:
                attr.matcher_op = "$=";
                break;
            case TokenType::kDelimAsterisk:
                attr.matcher_op = "*=";
                break;
            default:
                break;
            }
            if (!attr.matcher_op.empty()) {
                Advance();
                if (!Expect(TokenType::kDelimEquals)) {
                    return {attr, guchho::logger::Range{}};
                }
            }
        }

        // Parse the optional matcher value
        if (!attr.matcher_op.empty()) {
            Eat(TokenType::kWhitespace);
            if (!Peek(TokenType::kString) && !Peek(TokenType::kIdent)) {
                Unexpected();
            }
            attr.matcher_value = Decoded();
            Advance();
            Eat(TokenType::kWhitespace);
            if (Peek(TokenType::kIdent)) {
                std::string modifier = Decoded();
                if (modifier.size() == 1) {
                    char c = modifier[0];
                    if (c == 'i' || c == 'I' || c == 's' || c == 'S') {
                        attr.matcher_modifier = static_cast<uint8_t>(c);
                        Advance();
                    }
                }
            }
        }

        guchho::logger::Range close_range = Current().range;
        if (!ExpectWithMatchingLoc(TokenType::kCloseBracket, matching_loc)) {
            close_range.len = 0;
        }
        guchho::logger::Range r{guchho::logger::Loc{matching_loc.start}, close_range.End() - matching_loc.start};
        return {attr, r};
    }

    // ParsePseudoClassSelector
    // ------------------------
    // Parses a pseudo-class or pseudo-element selector.  This function
    // handles both:
    //
    //   - Simple pseudo-classes: :hover, :focus, :local, :global
    //   - Functional pseudo-classes: :is(...), :has(...), :not(...),
    //     :nth-child(2n+1), :local(.foo), :global(.bar)
    //   - Pseudo-elements: ::before, ::after, ::slotted(...)
    //
    // For functional pseudo-classes that take a selector list (:is,
    // :where, :has, :not, :local, :global), the function recursively
    // calls ParseSelectorList.  For :nth-child and :nth-last-child
    // with the "of S" syntax, it parses the An+B microsyntax and then
    // the selector list.
    //
    // The `is_element` flag distinguishes pseudo-elements (::) from
    // pseudo-classes (:).  During minification, the four Level 2
    // pseudo-elements that have single-colon aliases (::before,
    // ::after, ::first-line, ::first-letter) are converted to their
    // single-colon form.
    //
    // Input:  ":hover"         => SSPseudoClass{name="hover"}
    // Input:  ":is(a, b)"      => SSPseudoClassWithSelectorList with 2 selectors
    // Input:  "::before"       => SSPseudoClass{name="before", is_element=true}
    // Input:  ":nth-child(2n)" => SSPseudoClassWithSelectorList with index
    //
    // Edge case: :local and :global toggle the make_local_symbols_
    // flag, which affects how identifiers are resolved in the
    // selector list that follows.
    std::pair<std::shared_ptr<SS>, guchho::logger::Range> Parser::ParsePseudoClassSelector(guchho::logger::Loc loc,
                                                                                        bool is_element) {
        Advance();

        if (Peek(TokenType::kFunction)) {
            std::string text = Decoded();
            guchho::logger::Loc matching_loc{guchho::logger::Loc{Current().range.End() - 1}};
            Advance();

            // Potentially parse a pseudo-class with a selector list
            if (!is_element) {
                PseudoClassKind kind{};
                bool local = make_local_symbols_;
                bool has_kind = true;
                if (text == "global") {
                    kind = PseudoClassKind::kPseudoClassGlobal;
                    if (options_.symbol_mode != SymbolMode::kDisabled) {
                        local = false;
                    }
                } else if (text == "has") {
                    kind = PseudoClassKind::kPseudoClassHas;
                } else if (text == "is") {
                    kind = PseudoClassKind::kPseudoClassIs;
                } else if (text == "local") {
                    kind = PseudoClassKind::kPseudoClassLocal;
                    if (options_.symbol_mode != SymbolMode::kDisabled) {
                        local = true;
                    }
                } else if (text == "not") {
                    kind = PseudoClassKind::kPseudoClassNot;
                } else if (text == "nth-child") {
                    kind = PseudoClassKind::kPseudoClassNthChild;
                } else if (text == "nth-last-child") {
                    kind = PseudoClassKind::kPseudoClassNthLastChild;
                } else if (text == "nth-of-type") {
                    kind = PseudoClassKind::kPseudoClassNthOfType;
                } else if (text == "nth-last-of-type") {
                    kind = PseudoClassKind::kPseudoClassNthLastOfType;
                } else if (text == "where") {
                    kind = PseudoClassKind::kPseudoClassWhere;
                } else {
                    has_kind = false;
                }
                if (has_kind) {
                    size_t old = index_;
                    if (HasNthIndex(kind)) {
                        Eat(TokenType::kWhitespace);

                        // Parse the "An+B" syntax
                        auto [index, index_ok] = ParseNthIndex();
                        if (index_ok) {
                            std::vector<ComplexSelector> selectors;
                            bool selectors_ok = true;

                            // Parse the optional "of" clause
                            if ((kind == PseudoClassKind::kPseudoClassNthChild ||
                                kind == PseudoClassKind::kPseudoClassNthLastChild) &&
                                Peek(TokenType::kIdent) && helpers::EqualFoldASCII(Decoded(), "of")) {
                                Advance();
                                Eat(TokenType::kWhitespace);

                                // Contain the effects of ":local" and ":global"
                                bool old_local = make_local_symbols_;
                                ParseSelectorOpts sopts;
                                sopts.stop_on_close_paren = true;
                                sopts.no_leading_combinator = true;
                                std::tie(selectors, selectors_ok) = ParseSelectorList(sopts);
                                make_local_symbols_ = old_local;
                            }

                            // "2n+0" => "2n"
                            if (options_.minify_syntax) {
                                index.Minify();
                            }

                            // Match the closing ")"
                            if (selectors_ok) {
                                guchho::logger::Range close_range = Current().range;
                                if (!ExpectWithMatchingLoc(TokenType::kCloseParen, matching_loc)) {
                                    close_range.len = 0;
                                }
                                auto data = std::make_shared<SSPseudoClassWithSelectorList>();
                                data->kind = kind;
                                data->selectors = std::move(selectors);
                                data->index = index;
                                return {data, guchho::logger::Range{guchho::logger::Loc{loc.start},
                                                                close_range.End() - loc.start}};
                            }
                        }
                    } else {
                        Eat(TokenType::kWhitespace);

                        // ":local" forces local names and ":global" forces global names
                        bool old_local = make_local_symbols_;
                        make_local_symbols_ = local;
                        ParseSelectorOpts sopts;
                        sopts.pseudo_class_kind = kind;
                        sopts.stop_on_close_paren = true;
                        sopts.only_one_complex_selector =
                            kind == PseudoClassKind::kPseudoClassGlobal || kind == PseudoClassKind::kPseudoClassLocal;
                        sopts.is_forgiving_selector_list =
                            kind == PseudoClassKind::kPseudoClassIs || kind == PseudoClassKind::kPseudoClassWhere;
                        auto [selectors, selectors_ok] = ParseSelectorList(sopts);
                        make_local_symbols_ = old_local;

                        // Match the closing ")"
                        if (selectors_ok) {
                            guchho::logger::Range close_range = Current().range;
                            if (!ExpectWithMatchingLoc(TokenType::kCloseParen, matching_loc)) {
                                close_range.len = 0;
                            }
                            auto data = std::make_shared<SSPseudoClassWithSelectorList>();
                            data->kind = kind;
                            data->selectors = std::move(selectors);
                            return {data, guchho::logger::Range{guchho::logger::Loc{loc.start},
                                                            close_range.End() - loc.start}};
                        }
                    }
                    index_ = old;
                }
            }

            std::vector<Token> args = ConvertTokens(ParseAnyValue());
            guchho::logger::Range close_range = Current().range;
            if (!ExpectWithMatchingLoc(TokenType::kCloseParen, matching_loc)) {
                close_range.len = 0;
            }
            auto data = std::make_shared<SSPseudoClass>();
            data->is_element = is_element;
            data->name = text;
            data->args = std::make_shared<std::vector<Token>>(std::move(args));
            return {data, guchho::logger::Range{guchho::logger::Loc{loc.start}, close_range.End() - loc.start}};
        }

        guchho::logger::Range name_range = Current().range;
        std::string name = Decoded();
        auto sel = std::make_shared<SSPseudoClass>();
        sel->is_element = is_element;
        if (Expect(TokenType::kIdent)) {
            sel->name = name;

            // ":local .local_name :global .global_name {}"
            // ":local { .local_name { :global { .global_name {} } }"
            if (options_.symbol_mode != SymbolMode::kDisabled) {
                if (name == "local") {
                    make_local_symbols_ = true;
                } else if (name == "global") {
                    make_local_symbols_ = false;
                }
            }
        } else {
            name_range.len = 0;
        }
        return {sel, guchho::logger::Range{guchho::logger::Loc{loc.start}, name_range.End() - loc.start}};
    }

    // ParseAnyValue
    // -------------
    // Parses an arbitrary token sequence representing a CSS
    // declaration value, stopping at the matching close delimiter or
    // at a semicolon/exclamation mark at the top level.  This is used
    // as a fallback for pseudo-class arguments that are not a selector
    // list (e.g. :dir(ltr), :state(--foo)).
    //
    // The function uses a stack to track nested brackets, parens, and
    // braces, ensuring balanced matching.  It implements the
    // "any-value" production from the CSS Syntax Level 3 spec.
    //
    // Input:  "ltr"  (at top level)  =>  {"ltr"}
    // Input:  "rgb(255, 0, 0)"      =>  {"rgb", "(", "255", ",", "0", ",", "0", ")"}
    // Input:  ""                     =>  Unexpected() called, returns empty
    //
    // Edge case: a bare semicolon or exclamation mark at the top level
    // (empty stack) terminates parsing without consuming them.
    std::vector<lexer::Token> Parser::ParseAnyValue() {
        // Reference: https://drafts.csswg.org/css-syntax-3/#typedef-declaration-value

        stack_.clear(); // Reuse allocated memory
        size_t start = index_;

        for (;;) {
            switch (Current().kind) {
            case TokenType::kCloseParen:
            case TokenType::kCloseBracket:
            case TokenType::kCloseBrace:
                if (stack_.empty()) {
                    goto loop_done;
                }
                if (Current().kind != stack_.back()) {
                    goto loop_done;
                }
                stack_.pop_back();
                break;

            case TokenType::kSemicolon:
            case TokenType::kDelimExclamation:
                if (stack_.empty()) {
                    goto loop_done;
                }
                break;

            case TokenType::kOpenParen:
            case TokenType::kFunction:
                stack_.push_back(TokenType::kCloseParen);
                break;

            case TokenType::kOpenBracket:
                stack_.push_back(TokenType::kCloseBracket);
                break;

            case TokenType::kOpenBrace:
                stack_.push_back(TokenType::kCloseBrace);
                break;

            case TokenType::kEndOfFile:
                goto loop_done;

            default:
                break;
            }

            Advance();
        }

    loop_done:
        std::vector<lexer::Token> tokens(tokens_.begin() + static_cast<ptrdiff_t>(start),
                                        tokens_.begin() + static_cast<ptrdiff_t>(index_));
        if (tokens.empty()) {
            Unexpected();
        }
        return tokens;
    }

    // ParseCombinator
    // ---------------
    // Parses a combinator token (>, +, ~) or returns an empty
    // combinator if the current token is not a combinator.
    //
    // Input:  ">"  =>  Combinator{loc, '>'}
    // Input:  "+"  =>  Combinator{loc, '+'}
    // Input:  "~"  =>  Combinator{loc, '~'}
    // Input:  "a"  =>  Combinator{}  (empty, no combinator)
    Combinator Parser::ParseCombinator() {
        lexer::Token t = Current();

        switch (t.kind) {
        case TokenType::kDelimGreaterThan:
            Advance();
            return Combinator{t.range.loc, '>'};

        case TokenType::kDelimPlus:
            Advance();
            return Combinator{t.range.loc, '+'};

        case TokenType::kDelimTilde:
            Advance();
            return Combinator{t.range.loc, '~'};

        default:
            return Combinator{};
        }
    }

    // ParseInteger
    // ------------
    // Parses a string as an integer, trimming leading zeros.  Returns
    // the trimmed string and a success flag.  An empty input or input
    // containing non-digit characters is treated as failure.
    //
    // Input:  "0042"  =>  {"42", true}
    // Input:  "0"     =>  {"0", true}  (all zeros trimmed to single zero)
    // Input:  ""      =>  {"", false}
    // Input:  "abc"   =>  {"", false}
    std::pair<std::string, bool> ParseInteger(std::string text) {
        size_t n = text.size();
        if (n == 0) {
            return {"", false};
        }

        // Trim leading zeros
        size_t start = 0;
        while (start < n && text[start] == '0') {
            ++start;
        }

        // Make sure remaining characters are digits
        if (start == n) {
            return {"0", true};
        }
        for (size_t i = start; i < n; ++i) {
            char c = text[i];
            if (c < '0' || c > '9') {
                return {"", false};
            }
        }
        return {text.substr(start), true};
    }

    // ParseNthIndex
    // -------------
    // Parses the An+B microsyntax used by :nth-child(), :nth-last-child(),
    // :nth-of-type(), and :nth-last-of-type().  The syntax supports:
    //
    //   - Keyword forms: "even", "odd"
    //   - Plain integers: "3", "-1"
    //   - Full An+B expressions: "2n+1", "3n", "-n+3", "n-1"
    //
    // The function returns an NthIndex with the "a" coefficient and
    // optional "b" offset as strings.
    //
    // Input:  "even"       =>  NthIndex{a="", b="even"}
    // Input:  "odd"        =>  NthIndex{a="", b="odd"}
    // Input:  "3"          =>  NthIndex{a="", b="3"}
    // Input:  "2n+1"       =>  NthIndex{a="2", b="1"}
    // Input:  "n-1"        =>  NthIndex{a="1", b="-1"}
    // Input:  "-n+3"       =>  NthIndex{a="-1", b="3"}
    // Input:  "3n"         =>  NthIndex{a="3", b=""}
    //
    // Edge case: "2n+0" is valid input but the caller should call
    // NthIndex::Minify() to simplify it to "2n".
    std::pair<NthIndex, bool> Parser::ParseNthIndex() {
        enum class Sign : uint8_t {
            none,
            negative,
            positive,
        };

        // Reference: https://drafts.csswg.org/css-syntax-3/#anb-microsyntax
        lexer::Token t0 = Current();
        std::string text0 = Decoded();

        // Handle "even" and "odd"
        if (t0.kind == TokenType::kIdent && (text0 == "even" || text0 == "odd")) {
            Advance();
            Eat(TokenType::kWhitespace);
            return {NthIndex{std::string(), text0}, true};
        }

        // Handle a single number
        if (t0.kind == TokenType::kNumber) {
            bool b_neg = false;
            if (!text0.empty() && text0[0] == '-') {
                b_neg = true;
                text0 = text0.substr(1);
            } else if (!text0.empty() && text0[0] == '+') {
                text0 = text0.substr(1);
            }
            auto [b, ok] = ParseInteger(text0);
            if (ok) {
                if (b_neg) {
                    b = "-" + b;
                }
                Advance();
                Eat(TokenType::kWhitespace);
                return {NthIndex{std::string(), b}, true};
            }
            Unexpected();
            return {NthIndex{}, false};
        }

        Sign a_sign = Sign::none;
        if (Eat(TokenType::kDelimPlus)) {
            a_sign = Sign::positive;
            t0 = Current();
            text0 = Decoded();
        }

        // Everything from here must be able to contain an "n"
        if (t0.kind != TokenType::kIdent && t0.kind != TokenType::kDimension) {
            Unexpected();
            return {NthIndex{}, false};
        }

        // Check for a leading sign
        if (a_sign == Sign::none) {
            if (!text0.empty() && text0[0] == '-') {
                a_sign = Sign::negative;
                text0 = text0.substr(1);
            } else if (!text0.empty() && text0[0] == '+') {
                text0 = text0.substr(1);
            }
        }

        // The string must contain an "n"
        size_t n = text0.find('n');
        if (n == std::string::npos) {
            Unexpected();
            return {NthIndex{}, false};
        }

        // Parse the number before the "n"
        std::string a;
        if (n == 0) {
            if (a_sign == Sign::negative) {
                a = "-1";
            } else {
                a = "1";
            }
        } else {
            auto [a_int, ok] = ParseInteger(text0.substr(0, n));
            if (!ok) {
                Unexpected();
                return {NthIndex{}, false};
            }
            if (a_sign == Sign::negative) {
                a_int = "-" + a_int;
            }
            a = a_int;
        }
        text0 = text0.substr(n + 1);

        // Parse the stuff after the "n"
        Sign b_sign = Sign::none;
        if (!text0.empty() && text0[0] == '-') {
            text0 = text0.substr(1);
            auto [b, ok] = ParseInteger(text0);
            if (ok) {
                Advance();
                Eat(TokenType::kWhitespace);
                return {NthIndex{a, "-" + b}, true};
            }
            b_sign = Sign::negative;
        }
        if (!text0.empty()) {
            Unexpected();
            return {NthIndex{}, false};
        }
        Advance();
        Eat(TokenType::kWhitespace);

        // Parse an optional sign delimiter
        if (b_sign == Sign::none) {
            if (Eat(TokenType::kDelimMinus)) {
                b_sign = Sign::negative;
                Eat(TokenType::kWhitespace);
            } else if (Eat(TokenType::kDelimPlus)) {
                b_sign = Sign::positive;
                Eat(TokenType::kWhitespace);
            }
        }

        // Parse an optional trailing number
        lexer::Token t1 = Current();
        std::string text1 = Decoded();
        if (t1.kind == TokenType::kNumber) {
            if (b_sign == Sign::none) {
                if (!text1.empty() && text1[0] == '-') {
                    b_sign = Sign::negative;
                    text1 = text1.substr(1);
                } else if (!text1.empty() && text1[0] == '+') {
                    text1 = text1.substr(1);
                }
            }
            auto [b, ok] = ParseInteger(text1);
            if (ok) {
                if (b_sign == Sign::negative) {
                    b = "-" + b;
                }
                Advance();
                Eat(TokenType::kWhitespace);
                return {NthIndex{a, b}, true};
            }
        }

        // If there is a trailing sign, then there must also be a trailing number
        if (b_sign != Sign::none) {
            Expect(TokenType::kNumber);
            return {NthIndex{}, false};
        }

        return {NthIndex{a, std::string()}, true};
    }

}
