#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/css/css_properties.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/compiler.hpp"
#include "guchho/css/css_ast.hpp"

namespace guchho::css {

    // Controls how CSS symbol names (identifiers) are handled during parsing.
    // kDisabled: Symbols are not tracked (default for most CSS).
    // kGlobal: Symbols are treated as global identifiers (available everywhere).
    // kLocal: Symbols are scoped to their containing rule (like CSS Modules).
    enum class SymbolMode : uint8_t {
        kDisabled,
        kGlobal,
        kLocal,
    };

    // Configuration options for the CSS parser. Controls minification behavior,
    // target environment compatibility, and CSS feature support. These options
    // affect how the parser processes selectors, declarations, and at-rules.
    struct ParserOptions {
        std::unordered_map<Declarations, guchho::compat::CSSPrefix> css_prefix_data;

        std::string original_target_env;
        guchho::compat::CSSFeature unsupported_css_features{};
        bool minify_syntax{};
        bool minify_whitespace{};
        bool minify_identifiers{};
        SymbolMode symbol_mode{};
    };

    bool operator==(const ParserOptions& a, const ParserOptions& b);

    ParserOptions OptionsFromConfig(guchho::config::Loader loader, const guchho::config::Options& options);

    AST Parse(guchho::logger::Log& log, const guchho::logger::Source& source, const ParserOptions& options);

    std::pair<std::string, bool> ShiftDot(std::string text, int dot_offset);
    std::tuple<std::string, std::string, bool> MangleDimension(std::string value, std::string unit);
    std::pair<std::string, bool> MangleNumber(std::string t);
    std::pair<std::string, bool> ParseInteger(std::string text);

    bool IsSafeSelectors(const std::vector<ComplexSelector>& complex_selectors);


    // Entry in a hash table for cross-file rule deduplication. Each entry
    // contains the rule data and a call counter for tracking import processing.
    struct RuleEntry {
        std::shared_ptr<RuleData> data;
        uint32_t call_counter{};
    };

    // Hash table entry that groups rules by their hash value for efficient
    // deduplication. Used during cross-file dead rule elimination.
    struct HashEntry {
        std::vector<RuleEntry> rules;
    };

    // Tracks import records and source file indices for cross-file
    // dependency resolution during dead rule elimination.
    struct CallEntry {
        std::vector<guchho::compiler::ImportRecord> import_records;
        uint32_t source_index{};
    };

    // Removes dead CSS rules across multiple files by analyzing selector usage
    // and eliminating rules whose selectors cannot match any elements. Uses
    // hash-based deduplication and cross-file equality checks to safely
    // remove redundant rules.
    class DeadRuleRemover {
    public:
        explicit DeadRuleRemover(const guchho::compiler::SymbolMap& symbols);

        std::vector<Rule> RemoveDeadRulesInPlace(uint32_t source_index, std::vector<Rule> rules,
                                                const std::vector<guchho::compiler::ImportRecord>& import_records);

    private:
        std::unordered_map<uint32_t, HashEntry> entries_;
        std::vector<CallEntry> calls_;
        guchho::compiler::SymbolMap symbols_;
        CrossFileEqualityCheck check_;
    };

    bool ContainsDeadSelectors(const std::vector<CompoundSelector>& selectors);
    bool AllSelectorsAreDead(const std::vector<ComplexSelector>& selectors);

    // Context for parsing a list of CSS rules. Controls whether we are at
    // the top level of a stylesheet (affects how at-rules are handled)
    // and whether to parse selectors (vs just declarations).
    struct RuleContext {
        bool is_top_level{};
        bool parse_selectors{};
    };

    // Validity states for at-rules like @charset and @import. kInvalid means
    // the rule is not valid at this position. kValid means it is valid.
    // kInvalidAfter means the rule was valid earlier but is now invalid
    // (e.g., @import after a non-@charset rule).
    enum class AtRuleValidity : uint8_t {
        kInvalid,
        kValid,
        kInvalidAfter,
    };

    // Context for parsing at-rules (@media, @supports, @layer, etc.).
    // Tracks validity of preceding at-rules (charset, import) and whether
    // we are in a declaration list context.
    struct AtRuleContext {
        guchho::logger::Loc after_loc;
        AtRuleValidity charset_validity{};
        AtRuleValidity import_validity{};
        bool can_inline_no_op_nesting{};
        bool is_declaration_list{};
        bool is_top_level{};
    };

    // Options for parsing a list of declarations. Controls whether :composes
    // is being processed and whether no-op nesting can be inlined.
    struct ListOfDeclarationsOpts {
        ComposesContext* composes_context{};
        bool can_inline_no_op_nesting{};
    };

    // Options for parsing CSS selectors. Controls whether we are in a
    // declaration context (affects how pseudo-classes are parsed), whether
    // to stop at closing parentheses, whether to parse only one complex
    // selector, and whether to use forgiving parsing mode.
    struct ParseSelectorOpts {
        ComposesContext* composes_context{};
        PseudoClassKind pseudo_class_kind{};
        bool is_declaration_context{};
        bool stop_on_close_paren{};
        bool only_one_complex_selector{};
        bool is_forgiving_selector_list{};
        bool no_leading_combinator{};
    };

    // Options for parsing complex selectors. Wraps ParseSelectorOpts and
    // adds a flag indicating whether this is the first selector being parsed.
    struct ParseComplexSelectorOpts {
        ParseSelectorOpts parse_selector_opts;
        bool is_first{};
    };

    // Options for parsing qualified rules (rules with a selector and declaration
    // block). Controls whether the rule is already marked invalid, whether we
    // are at the top level, and whether we are in a declaration context.
    struct ParseQualifiedRuleOpts {
        bool is_already_invalid{};
        bool is_top_level{};
        bool is_declaration_context{};
    };

    // Options for converting raw CSS tokens into higher-level tokens.
    // Controls whether @import rules are allowed, whether whitespace is
    // preserved verbatim, and whether we are inside a calc() function.
    struct ConvertTokensOpts {
        bool allow_imports{};
        bool verbatim_whitespace{};
        bool is_inside_calc_function{};
    };

    // Classification of at-rules for special handling during parsing.
    // kUnknown: Standard at-rules (e.g., @media, @supports).
    // kDeclarations: At-rules that contain only declarations (e.g., @font-face).
    // kInheritContext: At-rules that inherit their parent's context (e.g., @media).
    // kQualifiedOrEmpty: At-rules that can contain qualified rules or be empty.
    // kEmpty: At-rules that must be empty (e.g., @charset).
    enum class AtRuleKind : uint8_t {
        kUnknown,
        kDeclarations,
        kInheritContext,
        kQualifiedOrEmpty,
        kEmpty,
    };

    AtRuleKind LookupSpecialAtRule(std::string_view lower_at_token);
    bool AtKnownRuleCanBeRemovedIfEmpty(std::string_view lower_at_token);

    // Indicates what token was found when scanning for the end of a rule.
    // kUnknown: No end token found (rule continues).
    // kSemicolon: Rule ended with a semicolon (declaration list).
    // kOpenBrace: Rule ended with an opening brace (qualified rule).
    enum class EndOfRuleScan : uint8_t {
        kUnknown,
        kSemicolon,
        kOpenBrace,
    };

    // Controls whether a leading "&" (nesting selector) can be removed during
    // CSS nesting lowering. kCannotRemove: The "&" must stay (e.g., in :has()).
    // kCanAlwaysRemove: The "&" can always be removed (e.g., in simple selectors).
    // kCanRemoveIfNotFirst: The "&" can be removed unless it is the first selector.
    enum class LeadingAmpersand : uint8_t {
        kCannotRemove,
        kCanAlwaysRemove,
        kCanRemoveIfNotFirst,
    };

    // Controls whether the leading combinator of a replacement selector should
    // be stripped during "&" substitution. kKeepLeadingCombinator: Preserve the
    // combinator (used when the replacement is the full parent selector).
    // kStripLeadingCombinator: Remove the combinator (used when inlining into
    // an existing selector chain).
    enum class LeadingCombinatorStrip : uint8_t {
        kKeepLeadingCombinator,
        kStripLeadingCombinator,
    };

    // Context for lowering CSS nesting. Carries parent selectors (with and
    // without pseudo-elements) for "&" substitution, and accumulates the
    // lowered rules as they are processed.
    struct LowerNestingContext {
        std::vector<ComplexSelector> parent_selectors_with_pseudo;
        std::vector<ComplexSelector> parent_selectors_no_pseudo;
        std::vector<Rule> lowered_rules;
    };

    // Controls whether "or" is included in media query parsing.
    // kWithOr: Include "or" as a keyword in media conditions.
    // kWithoutOr: Do not include "or" (for older CSS specifications).
    enum class MediaOr : uint8_t {
        kWithOr,
        kWithoutOr,
    };

    int CompoundSelectorTermCount(const CompoundSelector& sel);
    int ComplexSelectorTermCount(const std::vector<ComplexSelector>& selectors);

    // Main CSS parser class. Handles tokenization, selector parsing,
    // declaration parsing, at-rule handling, CSS nesting lowering, and
    // dead rule elimination. Maintains parser state including token stream,
    // comment tracking, import records, symbol tables, and nesting context.
    class Parser {
    public:
        Parser(guchho::logger::Log& log, const guchho::logger::Source& source, const ParserOptions& options);

        guchho::logger::Log* log_{};
        const guchho::logger::Source* source_{};
        std::vector<lexer::Token> tokens_;
        std::vector<guchho::logger::Range> all_comments_;
        std::vector<lexer::Comment> legal_comments_;
        std::vector<TokenType> stack_;
        std::vector<guchho::compiler::ImportRecord> import_records_;
        std::vector<guchho::compiler::Symbol> symbols_;
        std::unordered_map<guchho::compiler::Ref, std::shared_ptr<Composes>, RefHash> composes_;
        std::vector<guchho::compiler::LocRef> local_symbols_;
        std::unordered_map<std::string, guchho::compiler::LocRef> local_scope_;
        std::unordered_map<std::string, guchho::compiler::LocRef> global_scope_;
        std::unordered_set<int32_t> nesting_warnings_;
        guchho::logger::LineColumnTracker tracker_;
        std::vector<std::vector<MediaQuery>> enclosing_at_media_;
        std::vector<std::vector<std::string>> layers_pre_import_;
        std::vector<std::vector<std::string>> layers_post_import_;
        std::vector<std::string> enclosing_layer_;
        int anon_layer_count_{};
        size_t index_{};
        size_t legal_comment_index_{};
        int in_selector_subtree_{};
        guchho::logger::Loc prev_error_;
        ParserOptions options_;
        lexer::Span source_map_comment_;
        int32_t approximate_line_count_{};
        bool nesting_is_present_{};
        bool make_local_symbols_{};
        bool has_seen_at_import_{};


        void Advance();
        lexer::Token At(size_t index) const;
        lexer::Token Current() const;
        lexer::Token Next() const;
        std::string Raw() const;
        std::string Decoded() const;
        bool Peek(TokenType kind) const;
        bool Eat(TokenType kind);
        bool Expect(TokenType kind);
        bool ExpectWithMatchingLoc(TokenType kind, guchho::logger::Loc matching_loc);
        void Unexpected();
        guchho::compiler::LocRef SymbolForName(guchho::logger::Loc loc, const std::string& name);
        void RecordAtLayerRule(std::vector<std::vector<std::string>> layers);
        std::shared_ptr<guchho::compiler::CharFreq> ComputeCharacterFrequency();

        std::vector<Rule> ParseListOfRules(const RuleContext& context);
        std::vector<Rule> ParseListOfDeclarations(const ListOfDeclarationsOpts& opts);
        std::vector<Rule> MangleRules(std::vector<Rule> rules, bool is_top_level);
        Rule ParseAtRule(const AtRuleContext& context);
        std::pair<std::string, bool> ExpectValidLayerNameIdent();
        std::tuple<std::string, guchho::logger::Range, bool> ParseURLOrString();
        std::tuple<std::string, guchho::logger::Range, bool> ExpectURLOrString();
        Rule ParseSelectorRule(bool is_top_level, const ParseSelectorOpts& opts);
        Rule ParseQualifiedRule(const ParseQualifiedRuleOpts& opts);
        std::pair<EndOfRuleScan, int32_t> ScanForEndOfRule();
        Rule ParseDeclaration();
        void ParseComponentValue();
        void ParseBlock(TokenType open, TokenType close);


        std::vector<Token> ConvertTokens(const std::vector<lexer::Token>& tokens);
        std::pair<std::vector<Token>, std::vector<lexer::Token>> ConvertTokensHelper(
            std::vector<lexer::Token> tokens, TokenType close, const ConvertTokensOpts& opts);

        std::pair<std::vector<ComplexSelector>, bool> ParseSelectorList(const ParseSelectorOpts& opts);
        std::vector<ComplexSelector> FlattenLocalAndGlobalSelectors(std::vector<ComplexSelector> list,
                                                                const ComplexSelector& sel);
        std::pair<ComplexSelector, bool> ParseComplexSelector(const ParseComplexSelectorOpts& opts);
        NameToken TokenName();
        std::pair<CompoundSelector, bool> ParseCompoundSelector(const ParseComplexSelectorOpts& opts);
        std::pair<SSAttribute, guchho::logger::Range> ParseAttributeSelector();
        std::pair<std::shared_ptr<SS>, guchho::logger::Range> ParsePseudoClassSelector(guchho::logger::Loc loc,
                                                                                    bool is_element);
        std::vector<lexer::Token> ParseAnyValue();
        Combinator ParseCombinator();
        std::pair<NthIndex, bool> ParseNthIndex();


        std::vector<MediaQuery> ParseMediaQueryListUntil(const std::function<bool(TokenType)>& stop);
        std::pair<MediaQuery, bool> ParseMediaQuery();
        bool LooksLikeMediaCondition();
        std::pair<MediaQuery, bool> ParseMediaCondition(MediaOr or_);
        std::pair<MediaQuery, bool> ParseMediaInParens();


        std::vector<Rule> LowerNestingInRule(Rule rule, std::vector<Rule> results);
        std::vector<Rule> LowerNestingInRulesAndReturnRemaining(
            std::vector<Rule> rules,
            LowerNestingContext& context
        );
        Rule LowerNestingInRuleWithContext(const Rule& rule, LowerNestingContext& context);
        std::vector<CompoundSelector> SubstituteAmpersandsInCompoundSelector(
            CompoundSelector sel, const std::function<ComplexSelector(guchho::logger::Loc)>& replacement_fn,
            std::vector<CompoundSelector> results, LeadingCombinatorStrip strip);
        std::function<ComplexSelector(guchho::logger::Loc)> MultipleComplexSelectorsToSingleComplexSelector(
            const std::vector<ComplexSelector>& selectors);
        void AddExpansionError(guchho::logger::Loc loc, int n);
        void ReportNestingWithGeneratedPseudoClassIs(guchho::logger::Loc nesting_selector_loc);
    };
}