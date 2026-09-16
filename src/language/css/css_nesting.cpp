
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_parser.hpp"
#include "guchho/logger.hpp"
#include "guchho/compat.hpp"


namespace guchho::css {

// Counts the total number of selector terms within a single compound selector,
// including any nested selectors inside functional pseudo-classes like :is(),
// :not(), or :where(). Each subclass selector (class, id, attribute, pseudo-class)
// counts as one term, plus the count of all nested complex selectors within
// any functional pseudo-class.
//
// Input:  sel = ".foo.bar:hover:not(.active)" (3 subclass selectors, no functional pseudo-classes)
// Output: 3
//
// Input:  sel = ":is(.a, .b, .c)" (1 functional pseudo-class containing 3 complex selectors)
// Output: 4 (1 for the :is itself + 3 for inner selectors)
int CompoundSelectorTermCount(const CompoundSelector& sel) {
    int count = 0;
    for (const SubclassSelector& ss : sel.subclass_selectors) {
        count++;
        if (auto* list = dynamic_cast<SSPseudoClassWithSelectorList*>(ss.data.get()); list) {
            count += ComplexSelectorTermCount(list->selectors);
        }
    }
    return count;
}

// Counts the total number of selector terms across a list of complex selectors.
// A complex selector is a chain of compound selectors separated by combinators.
// This counts every compound selector term in every complex selector in the list.
//
// Input:  selectors = [".foo:hover", ".bar > .baz"] (2 complex selectors)
// Output: 5 (2 terms from .foo:hover + 3 terms from .bar > .baz)
int ComplexSelectorTermCount(const std::vector<ComplexSelector>& selectors) {
    int count = 0;
    for (const ComplexSelector& sel : selectors) {
        for (const CompoundSelector& inner : sel.selectors) {
            count += CompoundSelectorTermCount(inner);
        }
    }
    return count;
}

// Emits a diagnostic error when CSS nesting expansion produces too many
// selectors, which could indicate a combinatorial explosion that would
// consume excessive memory or time. The threshold is 0xFF00 (65280) terms.
void Parser::AddExpansionError(guchho::logger::Loc loc, int n) {
    guchho::logger::MsgData note;
    note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_TooMuchExpansionNote, std::to_string(n));
    log_->AddErrorWithNotes(&tracker_, guchho::logger::Range{loc},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_TooMuchExpansion),
                            {note});
}

// Reports a warning when CSS nesting lowering generates a :is() pseudo-class
// that the target environment does not support. This warning is emitted at most
// once per source location to avoid duplicate diagnostics for the same nesting
// selector. The warning includes a note explaining that the target environment
// does not support :is(), which may be required when nesting is lowered.
void Parser::ReportNestingWithGeneratedPseudoClassIs(guchho::logger::Loc nesting_selector_loc) {
    if (guchho::compat::Has(options_.unsupported_css_features, guchho::compat::CSSFeature::kIsPseudoClass)) {
        if (nesting_warnings_.count(nesting_selector_loc.start) != 0) {

            return;
        }
        nesting_warnings_.insert(nesting_selector_loc.start);

        std::string text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_NestingNotSupportedInTarget,
                                                     options_.original_target_env);

        guchho::logger::MsgData note;
        note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_NestingNotSupportedInTargetNote);
        log_->AddIDWithNotes(guchho::logger::MsgID::kCSS_UnsupportedCSSNesting, guchho::logger::MsgKind::kWarning,
                             &tracker_, guchho::logger::Range{nesting_selector_loc, 1}, text, {note});
    }
}

// Wraps a list of complex selectors into a single complex selector by using
// the :is() pseudo-class. If the list has exactly one selector, it is returned
// directly without wrapping. Otherwise, all selectors are wrapped in :is(...),
// preserving the leading combinator from the first selector. This is used when
// lowering CSS nesting to combine multiple parent selectors into a single
// selector that can replace the "&" token.
//
// Input:  selectors = [".foo", ".bar"] (two selectors, both with no leading combinator)
// Output: a function that returns ":is(.foo, .bar)" when called
//
// Input:  selectors = [".only"] (single selector)
// Output: a function that returns ".only" directly without wrapping in :is()
//
// Edge case: If selectors is empty, the returned function produces ":is()" which
// matches nothing.
std::function<ComplexSelector(guchho::logger::Loc)> Parser::MultipleComplexSelectorsToSingleComplexSelector(
    const std::vector<ComplexSelector>& selectors) {
    if (selectors.size() == 1) {
        ComplexSelector single = selectors[0];
        return [single](guchho::logger::Loc) { return single; };
    }

    Combinator leading_combinator;
    std::vector<ComplexSelector> clones;
    clones.reserve(selectors.size());

        for (const ComplexSelector& sel : selectors) {
            leading_combinator = sel.selectors[0].combinator;
        clones.push_back(sel.Clone());
    }

    return [leading_combinator, clones](guchho::logger::Loc loc) {
        CompoundSelector compound;
        compound.combinator = leading_combinator;

        SubclassSelector ss;
        ss.range = guchho::logger::Range{loc};
        auto data = std::make_shared<SSPseudoClassWithSelectorList>();
        data->kind = PseudoClassKind::kPseudoClassIs;
        data->selectors = clones;
        ss.data = std::move(data);

        compound.subclass_selectors.push_back(std::move(ss));

        ComplexSelector cs;
        cs.selectors.push_back(std::move(compound));
        return cs;
    };
}

// Replaces all "&" (nesting selector) tokens in a compound selector with the
// parent selector provided by replacement_fn. The replacement can take three
// forms depending on the context:
//
// 1. When the compound has no leading combinator and the replacement is a
//    single compound or results are empty, the replacement's compounds are
//    prepended to results and the last compound becomes the base.
//    Input:  sel = "&.foo", replacement = ".parent", results = []
//    Output: results = [".parent", ".foo"] (prepending parent's compounds)
//
// 2. When the replacement is a single compound, it is used directly as the base
//    and merged with the current selector.
//    Input:  sel = "> &.hover", replacement = ".parent"
//    Output: ".parent > .parent.hover"
//
// 3. When the replacement has multiple compounds and results already exist,
//    the replacement is wrapped in :is() to avoid ambiguity.
//    Input:  sel = "&.foo", replacement = ".a, .b" (two alternatives)
//    Output: ":is(.a, .b).foo" with a warning if :is is unsupported
//
// The strip parameter controls whether the leading combinator of the replacement
// should be removed, which is needed when the replacement is being inlined into
// an existing selector chain.
//
// After processing all "&" tokens, this function also recurses into any
// functional pseudo-class selectors (like :is(), :not()) within the result
// to perform nested "&" substitution.
std::vector<CompoundSelector> Parser::SubstituteAmpersandsInCompoundSelector(
    CompoundSelector sel, const std::function<ComplexSelector(guchho::logger::Loc)>& replacement_fn,
    std::vector<CompoundSelector> results, LeadingCombinatorStrip strip) {
    for (guchho::logger::Loc nesting_selector_loc : sel.nesting_selector_locs) {
        ComplexSelector replacement = replacement_fn(nesting_selector_loc);

        CompoundSelector single;
        if (sel.combinator.byte_ == 0 && (replacement.selectors.size() == 1 || results.empty())) {
            size_t last = replacement.selectors.size() - 1;
            for (size_t i = 0; i < last; i++) {
                results.push_back(replacement.selectors[i]);
            }
            single = replacement.selectors[last];
            if (strip == LeadingCombinatorStrip::kStripLeadingCombinator) {
                single.combinator = Combinator{};
            }
            sel.combinator = single.combinator;
        } else if (replacement.selectors.size() == 1) {
            single = replacement.selectors[0];
            if (strip == LeadingCombinatorStrip::kStripLeadingCombinator) {
                single.combinator = Combinator{};
            }
        } else {
            ReportNestingWithGeneratedPseudoClassIs(nesting_selector_loc);
            SubclassSelector ss;
            ss.range = guchho::logger::Range{nesting_selector_loc};
            auto data = std::make_shared<SSPseudoClassWithSelectorList>();
            data->kind = PseudoClassKind::kPseudoClassIs;
            data->selectors.push_back(replacement.Clone());
            ss.data = std::move(data);
            single.subclass_selectors.push_back(std::move(ss));
        }

        std::vector<SubclassSelector> subclass_selector_prefix;

        if (single.type_selector != nullptr) {
            if (sel.type_selector != nullptr) {
                ReportNestingWithGeneratedPseudoClassIs(nesting_selector_loc);
                SubclassSelector ss;
                ss.range = sel.type_selector->Range();
                auto data = std::make_shared<SSPseudoClassWithSelectorList>();
                data->kind = PseudoClassKind::kPseudoClassIs;
                ComplexSelector complex;
                CompoundSelector inner;
                inner.type_selector = sel.type_selector;
                complex.selectors.push_back(std::move(inner));
                data->selectors.push_back(std::move(complex));
                ss.data = std::move(data);
                subclass_selector_prefix.push_back(std::move(ss));
            }
            sel.type_selector = single.type_selector;
        }

        subclass_selector_prefix.insert(subclass_selector_prefix.end(), single.subclass_selectors.begin(),
                                        single.subclass_selectors.end());

        if (!subclass_selector_prefix.empty()) {
            subclass_selector_prefix.insert(subclass_selector_prefix.end(), sel.subclass_selectors.begin(),
                                            sel.subclass_selectors.end());
            sel.subclass_selectors = std::move(subclass_selector_prefix);
        }
    }
    sel.nesting_selector_locs.clear();

    for (SubclassSelector& ss : sel.subclass_selectors) {
        if (auto* class_ = dynamic_cast<SSPseudoClassWithSelectorList*>(ss.data.get()); class_) {
            std::vector<ComplexSelector> outer;
            outer.reserve(class_->selectors.size());
            for (const ComplexSelector& complex : class_->selectors) {
                std::vector<CompoundSelector> inner;
                inner.reserve(complex.selectors.size());
                for (const CompoundSelector& inner_sel : complex.selectors) {
                    inner = SubstituteAmpersandsInCompoundSelector(inner_sel, replacement_fn, std::move(inner),
                                                                  LeadingCombinatorStrip::kStripLeadingCombinator);
                }
                ComplexSelector outer_cs;
                outer_cs.selectors = std::move(inner);
                outer.push_back(std::move(outer_cs));
            }
            class_->selectors = std::move(outer);
        }
    }

    results.push_back(std::move(sel));
    return results;
}

// Processes a list of CSS rules, lowering any nested rules within each rule
// and filtering out rules that become empty after lowering. The context carries
// the parent selectors needed for "&" substitution. Rules are processed in
// order, and only non-null results are kept. Returns the filtered list of
// remaining rules.
std::vector<Rule> Parser::LowerNestingInRulesAndReturnRemaining(std::vector<Rule> rules,
                                                                LowerNestingContext& context) {
    size_t n = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        Rule child = LowerNestingInRuleWithContext(rules[i], context);
        if (child.data != nullptr) {
            rules[n++] = std::move(child);
        }
    }
    rules.resize(n);
    return rules;
}

// Lowers CSS nesting within a single rule by recursively processing its children
// and accumulating the results. For selector rules, this function performs "&"
// substitution, builds parent selector lists, and processes nested children.
// For at-rules (@supports, @media, @layer), it recursively lowers nesting
// within their child rules.
//
// For selector rules:
// - Builds two parent selector lists: one with pseudo-elements, one without
// - Substitutes "&" in the selector itself (replacing with :scope at top level)
// - Processes all nested children using LowerNestingInRuleWithContext
// - Removes the selector rule if it ends up with no children
//
// Input:  rule = ".parent { &.child { color: red } }"
// Output: [".parent.child { color: red }"] (nested selector lowered)
//
// Input:  rule = ".empty { }" (selector with no children)
// Output: [] (empty selector removed)
//
// Input:  rule = "@supports (display: grid) { &.grid { display: grid } }"
// Output: ["@supports (display: grid) { .parent.grid { display: grid } }"]
std::vector<Rule> Parser::LowerNestingInRule(Rule rule, std::vector<Rule> results) {
    if (auto r = std::dynamic_pointer_cast<RSelector>(rule.data); r) {
        auto scope = [](guchho::logger::Loc loc) {
            SubclassSelector ss;
            ss.range = guchho::logger::Range{loc};
            auto data = std::make_shared<SSPseudoClass>();
            data->name = "scope";
            ss.data = std::move(data);

            CompoundSelector compound;
            compound.subclass_selectors.push_back(std::move(ss));

            ComplexSelector cs;
            cs.selectors.push_back(std::move(compound));
            return cs;
        };

        std::vector<ComplexSelector> parent_selectors_with_pseudo;
        std::vector<ComplexSelector> parent_selectors_no_pseudo;
        parent_selectors_with_pseudo.reserve(r->selectors.size());
        parent_selectors_no_pseudo.reserve(r->selectors.size());

        for (size_t i = 0; i < r->selectors.size(); i++) {
            const ComplexSelector& sel = r->selectors[i];

            bool uses_pseudo_element = sel.UsesPseudoElement();

            std::vector<CompoundSelector> substituted;
            substituted.reserve(sel.selectors.size());
            for (const CompoundSelector& x : sel.selectors) {
                substituted = SubstituteAmpersandsInCompoundSelector(x, scope, std::move(substituted),
                                                                    LeadingCombinatorStrip::kKeepLeadingCombinator);
            }
            r->selectors[i].selectors = substituted;

            if (!uses_pseudo_element) {
                ComplexSelector no_pseudo;
                no_pseudo.selectors = substituted;
                parent_selectors_no_pseudo.push_back(std::move(no_pseudo));
            }

            ComplexSelector with_pseudo;
            with_pseudo.selectors = substituted;
            parent_selectors_with_pseudo.push_back(std::move(with_pseudo));
        }

        size_t start = results.size();
        results.push_back(rule);

        LowerNestingContext context;
        context.parent_selectors_with_pseudo = std::move(parent_selectors_with_pseudo);
        context.parent_selectors_no_pseudo = std::move(parent_selectors_no_pseudo);
        context.lowered_rules = std::move(results);

        r->rules = LowerNestingInRulesAndReturnRemaining(std::move(r->rules), context);

        if (r->rules.empty()) {
            context.lowered_rules.erase(context.lowered_rules.begin() + static_cast<ptrdiff_t>(start));
        }

        return context.lowered_rules;
    }

    if (auto r = std::dynamic_pointer_cast<RKnownAt>(rule.data); r) {
        std::vector<Rule> rules;
        rules.reserve(r->rules.size());
        for (const Rule& child : r->rules) {
            rules = LowerNestingInRule(child, std::move(rules));
        }
        r->rules = std::move(rules);
    } else if (auto r2 = std::dynamic_pointer_cast<RAtLayer>(rule.data); r2) {
        std::vector<Rule> rules;
        rules.reserve(r2->rules.size());
        for (const Rule& child : r2->rules) {
            rules = LowerNestingInRule(child, std::move(rules));
        }
        r2->rules = std::move(rules);
    } else if (auto r3 = std::dynamic_pointer_cast<RAtMedia>(rule.data); r3) {
        std::vector<Rule> rules;
        rules.reserve(r3->rules.size());
        for (const Rule& child : r3->rules) {
            rules = LowerNestingInRule(child, std::move(rules));
        }
        r3->rules = std::move(rules);
    }

    results.push_back(std::move(rule));
    return results;
}

// Lowers CSS nesting within a rule using the provided context (which carries
// parent selectors for "&" substitution). This is the core of the nesting
// lowering algorithm. For selector rules, it performs a two-phase transform:
//
// Phase 1: Canonicalize selectors by injecting implicit "&" tokens for
// relative selectors (those starting with a combinator like > or ~).
//
// Phase 2: Substitute "&" tokens with the parent selector. There are two
// strategies depending on whether :is() is supported:
//
//   - If :is() is supported (or there is only one parent), use
//     MultipleComplexSelectorsToSingleComplexSelector to create a single
//     replacement function, then substitute directly.
//
//   - If :is() is NOT supported and there are multiple parents, iterate
//     through all combinations of parent selectors using a counter with
//     carry (like an odometer). Each combination produces a separate set
//     of output selectors, which can lead to exponential growth.
//
// After substitution, the algorithm checks for combinatorial explosion by
// comparing the new selector count and complexity against thresholds. If
// either exceeds 0xFF00, an error is emitted and the rule is dropped.
//
// For at-rules (@supports, @media, @layer), the algorithm creates a child
// context with the same parent selectors, lowers nesting recursively, then
// wraps the results in the parent selector to hoist them outside the at-rule.
//
// Input:  rule = ".a, .b { &.c { color: red } }", context.parent = ".a, .b"
// Output: Rule{} (consumed, results added to context.lowered_rules)
//         context.lowered_rules += [".a.c { color: red }", ".b.c { color: red }"]
//
// Input:  rule = "&.hover { color: blue }", context.parent = ".parent"
// Output: context.lowered_rules += [".parent.hover { color: blue }"]
Rule Parser::LowerNestingInRuleWithContext(const Rule& rule, LowerNestingContext& context) {
    if (auto r = std::dynamic_pointer_cast<RSelector>(rule.data); r) {
        int old_selectors_len = static_cast<int>(r->selectors.size());
        int old_selectors_complexity = ComplexSelectorTermCount(r->selectors);

        for (ComplexSelector& sel : r->selectors) {
            if (sel.IsRelative()) {
                CompoundSelector compound;
                compound.nesting_selector_locs.push_back(rule.loc);
                sel.selectors.insert(sel.selectors.begin(), std::move(compound));
            }
        }

        if (!guchho::compat::Has(options_.unsupported_css_features, guchho::compat::CSSFeature::kIsPseudoClass) ||
            context.parent_selectors_no_pseudo.size() <= 1) {
            auto parent = MultipleComplexSelectorsToSingleComplexSelector(context.parent_selectors_no_pseudo);
            for (ComplexSelector& complex : r->selectors) {
                std::vector<CompoundSelector> results;
                results.reserve(complex.selectors.size());
                for (const CompoundSelector& compound : complex.selectors) {
                    results = SubstituteAmpersandsInCompoundSelector(compound, parent, std::move(results),
                                                                    LeadingCombinatorStrip::kKeepLeadingCombinator);
                }
                complex.selectors = std::move(results);
            }
        } else {
            std::vector<ComplexSelector> selectors;
            std::vector<size_t> indices;
            for (;;) {
                size_t offset = 0;
                auto parent = [&](guchho::logger::Loc) -> ComplexSelector {
                    if (offset == indices.size()) {
                        indices.push_back(0);
                    }
                    size_t index = indices[offset];
                    offset++;
                    return context.parent_selectors_no_pseudo[index];
                };

                for (const ComplexSelector& complex_in : r->selectors) {
                    ComplexSelector complex = complex_in;
                    std::vector<CompoundSelector> results;
                    results.reserve(complex.selectors.size());
                    for (const CompoundSelector& compound : complex.selectors) {
                        results = SubstituteAmpersandsInCompoundSelector(compound, parent, std::move(results),
                                                                        LeadingCombinatorStrip::kKeepLeadingCombinator);
                    }
                    complex.selectors = std::move(results);
                    selectors.push_back(std::move(complex));
                    offset = 0;
                }

                int carry = static_cast<int>(indices.size());
                while (carry > 0) {
                    size_t& index = indices[static_cast<size_t>(carry - 1)];
                    if (index + 1 < context.parent_selectors_no_pseudo.size()) {
                        index++;
                        break;
                    }
                    index = 0;
                    carry--;
                }
                if (carry == 0) {
                    break;
                }
            }
            r->selectors = std::move(selectors);
        }

        int n = static_cast<int>(r->selectors.size());
        if (n > old_selectors_len && n > 0xFF00) {
            AddExpansionError(rule.loc, n);
            return Rule{};
        }
        n = ComplexSelectorTermCount(r->selectors);
        if (n > old_selectors_complexity && n > 0xFF00) {
            AddExpansionError(rule.loc, n);
            return Rule{};
        }

        context.lowered_rules = LowerNestingInRule(rule, std::move(context.lowered_rules));
        return Rule{};
    }

    if (auto r = std::dynamic_pointer_cast<RKnownAt>(rule.data); r) {
        LowerNestingContext child_context;
        child_context.parent_selectors_with_pseudo = context.parent_selectors_with_pseudo;
        child_context.parent_selectors_no_pseudo = context.parent_selectors_no_pseudo;

        r->rules = LowerNestingInRulesAndReturnRemaining(std::move(r->rules), child_context);

        if (!r->rules.empty()) {
            Rule wrapper;
            wrapper.loc = rule.loc;
            auto sel = std::make_shared<RSelector>();
            sel->selectors = context.parent_selectors_with_pseudo;
            sel->rules = r->rules;
            wrapper.data = std::move(sel);
            child_context.lowered_rules.insert(child_context.lowered_rules.begin(), std::move(wrapper));
        }

        if (!child_context.lowered_rules.empty()) {
            r->rules = std::move(child_context.lowered_rules);
            context.lowered_rules.push_back(rule);
        }

        return Rule{};
    }

    if (auto r = std::dynamic_pointer_cast<RAtMedia>(rule.data); r) {
        LowerNestingContext child_context;
        child_context.parent_selectors_with_pseudo = context.parent_selectors_with_pseudo;
        child_context.parent_selectors_no_pseudo = context.parent_selectors_no_pseudo;

        r->rules = LowerNestingInRulesAndReturnRemaining(std::move(r->rules), child_context);

        if (!r->rules.empty()) {
            Rule wrapper;
            wrapper.loc = rule.loc;
            auto sel = std::make_shared<RSelector>();
            sel->selectors = context.parent_selectors_with_pseudo;
            sel->rules = r->rules;
            wrapper.data = std::move(sel);
            child_context.lowered_rules.insert(child_context.lowered_rules.begin(), std::move(wrapper));
        }

        if (!child_context.lowered_rules.empty()) {
            r->rules = std::move(child_context.lowered_rules);
            context.lowered_rules.push_back(rule);
        }

        return Rule{};
    }

    if (auto r = std::dynamic_pointer_cast<RAtLayer>(rule.data); r) {
        LowerNestingContext child_context;
        child_context.parent_selectors_with_pseudo = context.parent_selectors_with_pseudo;
        child_context.parent_selectors_no_pseudo = context.parent_selectors_no_pseudo;

        r->rules = LowerNestingInRulesAndReturnRemaining(std::move(r->rules), child_context);

        if (!r->rules.empty()) {
            Rule wrapper;
            wrapper.loc = rule.loc;
            auto sel = std::make_shared<RSelector>();
            sel->selectors = context.parent_selectors_with_pseudo;
            sel->rules = r->rules;
            wrapper.data = std::move(sel);
            child_context.lowered_rules.insert(child_context.lowered_rules.begin(), std::move(wrapper));
        }

        r->rules = std::move(child_context.lowered_rules);
        context.lowered_rules.push_back(rule);
        return Rule{};
    }

    return rule;
}

}
