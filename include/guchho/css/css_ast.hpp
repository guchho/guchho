#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "guchho/compiler.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/css/css_properties.hpp"

namespace guchho::css {
    // Flags indicating whether whitespace exists before or after a token.
    // Used during minification to decide when whitespace can be safely removed.
    enum class WhitespaceFlags : uint8_t {
        kNone = 0,
        kWhitespaceBefore = 1 << 0,
        kWhitespaceAfter = 1 << 1,
    };

    // Flags controlling how percentage values are validated and clamped.
    // Some CSS properties allow percentages outside the normal 0-100 range
    // (e.g., transform-origin can use negative percentages).
    enum class PercentageFlags : uint8_t {
        kNone = 0,
        kAllowPercentageBelow0 = 1 << 0,
        kAllowPercentageAbove100 = 1 << 1,
        kAllowAnyPercentage = kAllowPercentageBelow0 | kAllowPercentageAbove100,
    };

    inline PercentageFlags operator|(PercentageFlags a, PercentageFlags b) {
        return static_cast<PercentageFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline PercentageFlags operator&(PercentageFlags a, PercentageFlags b) {
        return static_cast<PercentageFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline bool Has(PercentageFlags flags, PercentageFlags flag) {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }


    inline WhitespaceFlags operator|(WhitespaceFlags a, WhitespaceFlags b) {
        return static_cast<WhitespaceFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline WhitespaceFlags operator&(WhitespaceFlags a, WhitespaceFlags b) {
        return static_cast<WhitespaceFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline bool Has(WhitespaceFlags flags, WhitespaceFlags flag) {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    // Context for checking equality of CSS tokens across different source files.
    // When comparing tokens from different files, import records must be
    // resolved to determine if two Ref values point to equivalent symbols.
    struct CrossFileEqualityCheck {
        const std::vector<guchho::compiler::ImportRecord>* import_records_a{};
        const std::vector<guchho::compiler::ImportRecord>* import_records_b{};
        guchho::compiler::SymbolMap* symbols{};
        uint32_t source_index_a{};
        uint32_t source_index_b{};

        // Checks whether two Ref values from different files refer to the
        // same symbol. Resolves import records to compare the actual targets.
        bool RefsAreEquivalent(guchho::compiler::Ref a, guchho::compiler::Ref b) const;
    };


    // Represents a single CSS token produced by the lexer. This is the most
    // fundamental unit of the CSS AST. The struct is designed to be memory-
    // efficient since thousands of tokens are created during parsing.
    struct Token {
        // Child tokens for simple blocks (parentheses, braces, brackets)
        // and function tokens. Closing delimiters are not stored separately.
        std::shared_ptr<std::vector<Token>> children;

        // The raw text of the token. For TString tokens, this holds the
        // decoded string content (escaping already resolved).
        std::string text;

        // Source location where this token begins in the original source.
        guchho::logger::Loc loc;

        // For URL tokens: index into the AST's top-level import records.
        // For Symbol tokens: the InnerIndex of the associated symbol.
        uint32_t payload_index{};

        // For TDimension tokens: offset between the numeric value and the
        // unit string within the token text (e.g., in "10px", offset is 2).
        uint16_t unit_offset{};

        // The type of this token. Whitespace is not stored as a separate
        // token; instead, WhitespaceFlags tracks its presence.
        TokenType kind{};

        // Flags indicating whether whitespace exists before or after this token.
        WhitespaceFlags whitespace{};

        // Compares this token for equality with another token. If a
        // CrossFileEqualityCheck is provided, Refs are resolved across files.
        //
        // Example:
        //   Token a = {.text = "10px", .kind = kDimension};
        //   Token b = {.text = "10px", .kind = kDimension};
        //   a.Equal(b, nullptr) -> true
        bool Equal(const Token& b, const CrossFileEqualityCheck* check) const;

        // Compares tokens for equality ignoring any whitespace differences.
        // Useful for determining if two tokens represent the same CSS value
        // regardless of formatting.
        //
        // Example:
        //   Token a = {.text = "10", .whitespace = kWhitespaceAfter};
        //   Token b = {.text = "10", .whitespace = kNone};
        //   a.EqualIgnoringWhitespace(b) -> true
        bool EqualIgnoringWhitespace(const Token& b) const;

        // Extracts the numeric portion of a percentage token (e.g., "50%"
        // returns "50").
        //
        // Example:
        //   Token t = {.text = "75.5%", .kind = kPercentage};
        //   t.PercentageValue() -> "75.5"
        std::string PercentageValue() const;

        // Extracts the numeric portion of a dimension token (e.g., "10px"
        // returns "10").
        //
        // Example:
        //   Token t = {.text = "3.14em", .kind = kDimension, .unit_offset = 4};
        //   t.DimensionValue() -> "3.14"
        std::string DimensionValue() const;

        // Extracts the unit portion of a dimension token (e.g., "10px"
        // returns "px").
        //
        // Example:
        //   Token t = {.text = "16rem", .kind = kDimension, .unit_offset = 2};
        //   t.DimensionUnit() -> "rem"
        std::string DimensionUnit() const;

        // Returns true if this dimension token uses a "safe" length unit
        // that can be freely combined in shorthand properties. Safe units
        // are absolute (px, pt, pc, in, mm, cm) and em/rem (relative to
        // font/parent). Units like vw, vh, % are not safe because they
        // depend on layout context.
        //
        // Example:
        //   Token {.text = "10px"}.DimensionUnitIsSafeLength() -> true
        //   Token {.text = "50%"}.DimensionUnitIsSafeLength()  -> false
        bool DimensionUnitIsSafeLength() const;

        // Converts a zero-valued length token to a plain number "0". This
        // allows the unit to be dropped in output (e.g., "0px" -> "0").
        //
        // Example:
        //   Token t = {.text = "0px", .kind = kDimension};
        //   t.TurnLengthIntoNumberIfZero() -> true
        //   // t.text is now "0", t.kind is now kNumber
        bool TurnLengthIntoNumberIfZero();

        // Converts a zero-valued length or percentage token to a plain
        // number "0". More permissive than TurnLengthIntoNumberIfZero.
        //
        // Example:
        //   Token t = {.text = "0%", .kind = kPercentage};
        //   t.TurnLengthOrPercentageIntoNumberIfZero() -> true
        //   // t.text is now "0"
        bool TurnLengthOrPercentageIntoNumberIfZero();

        // Returns true if this token's numeric value is exactly zero.
        // Handles both number tokens ("0") and dimension/percentage tokens
        // with zero values ("0px", "0%").
        //
        // Example:
        //   Token {.text = "0"}.IsZero()    -> true
        //   Token {.text = "0px"}.IsZero()  -> true
        //   Token {.text = "0.0"}.IsZero()  -> true
        //   Token {.text = "1"}.IsZero()    -> false
        bool IsZero() const;

        // Returns true if this token's numeric value is exactly one.
        //
        // Example:
        //   Token {.text = "1"}.IsOne()     -> true
        //   Token {.text = "1.0"}.IsOne()   -> true
        //   Token {.text = "2"}.IsOne()     -> false
        bool IsOne() const;

        // Returns true if this token represents an angle value (deg, rad,
        // grad, turn).
        //
        // Example:
        //   Token {.text = "90deg"}.IsAngle()  -> true
        //   Token {.text = "10px"}.IsAngle()   -> false
        bool IsAngle() const;

        // Converts a percentage token to a numeric value relative to a
        // reference range. The flags control whether values outside 0-100
        // are allowed.
        //
        // Example:
        //   Token {.text = "50%"}
        //   .NumberOrFractionForPercentage(100.0, kNone) -> (50.0, true)
        //
        //   Token {.text = "150%"}
        //   .NumberOrFractionForPercentage(100.0, kNone) -> (0.0, false)  // out of range
        //
        //   Token {.text = "150%"}
        //   .NumberOrFractionForPercentage(100.0, kAllowPercentageAbove100) -> (150.0, true)
        std::pair<double, bool> NumberOrFractionForPercentage(
            double percent_reference_range,
            PercentageFlags flags) const;

        // Converts a percentage to a clamped fraction in the range [0, 1].
        // Values below 0% become 0.0, values above 100% become 1.0.
        //
        // Example:
        //   Token {.text = "50%"}.ClampedFractionForPercentage()  -> (0.5, true)
        //   Token {.text = "-10%"}.ClampedFractionForPercentage() -> (0.0, true)
        //   Token {.text = "200%"}.ClampedFractionForPercentage() -> (1.0, true)
        //   Token {.text = "10px"}.ClampedFractionForPercentage() -> (0.0, false)
        std::pair<double, bool> ClampedFractionForPercentage() const;
    };

    // Compares two token vectors for equality, ignoring whitespace differences.
    bool TokensEqualIgnoringWhitespace(const std::vector<Token>& a, const std::vector<Token>& b);

    // Returns true if the token vector is a comma-separated list.
    bool TokensAreCommaSeparated(const std::vector<Token>& tokens);

    // Computes a hash of a token vector for use in hash maps.
    uint32_t HashTokens(uint32_t hash, const std::vector<Token>& tokens);

    // Creates a deep copy of tokens, stripping any import record references.
    // Used when cloning tokens that don't need cross-file symbol resolution.
    std::vector<Token> CloneTokensWithoutImportRecords(const std::vector<Token>& tokens_in);

    // Creates a deep copy of tokens, remapping import record references from
    // the input records to newly created output records. This is used when
    // inlining or duplicating CSS across files.
    std::vector<Token> CloneTokensWithImportRecords(
        const std::vector<Token>& tokens_in,
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out
    );

    // Compares two token vectors for exact equality, optionally resolving
    // cross-file references using the provided equality check context.
    bool TokensEqual(const std::vector<Token>& a, const std::vector<Token>& b, const CrossFileEqualityCheck* check);

    // Base class for all media query AST nodes. Media queries can be
    // combined with AND/OR operators, negated with NOT, or wrapped in
    // parentheses. This polymorphic hierarchy represents the different
    // forms a media query can take.
    class MQ {
        public:
            virtual ~MQ() = default;

            // Checks structural equality with another media query, optionally
            // resolving cross-file references.
            virtual bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const = 0;

            // Checks structural equality ignoring whitespace differences.
            virtual bool EqualIgnoringWhitespace(const MQ* query) const = 0;

            // Computes a hash for use in hash maps and deduplication.
            virtual uint32_t Hash() const = 0;

            // Creates a deep copy, remapping import record references.
            virtual std::unique_ptr<MQ> CloneWithImportRecords(
                const std::vector<guchho::compiler::ImportRecord>& import_records_in,
                std::vector<guchho::compiler::ImportRecord>& import_records_out) const = 0;
    };

    // Wraps a media query with its source location for error reporting.
    struct MediaQuery {
        std::shared_ptr<MQ> data;
        guchho::logger::Loc loc;
    };

    // Compares two media query lists for equality, resolving cross-file references.
    bool MediaQueriesEqual(const std::vector<MediaQuery>& a, const std::vector<MediaQuery>& b, const CrossFileEqualityCheck* check);

    // Compares two media query lists for equality, ignoring whitespace.
    bool MediaQueriesEqualIgnoringWhitespace(const std::vector<MediaQuery>& a, const std::vector<MediaQuery>& b);

    // Hashes a media query list for deduplication.
    uint32_t HashMediaQueries(uint32_t hash, const std::vector<MediaQuery>& queries);

    // Deep-copies media queries, remapping import record references.
    std::vector<MediaQuery> CloneMediaQueriesWithImportRecords(
        const std::vector<MediaQuery>& queries_in,
        const std::vector<guchho::compiler::ImportRecord>& import_records_in,
        std::vector<guchho::compiler::ImportRecord>& import_records_out
    );

    // Represents the type-level operator in a media query: "not", "only",
    // or no operator (which defaults to "only" semantically).
    enum class MQTypeOp : uint8_t {
        kMQTypeOpNone,
        kMQTypeOpNot,
        kMQTypeOpOnly,
    };

    // A media type query like "screen", "print", or "all". May be preceded
    // by "not" or "only". Can be combined with additional conditions via AND.
    //
    // Example:
    //   MQType { .op = kMQTypeOpNone, .type = "screen" }
    //   // represents: screen
    //
    //   MQType { .op = kMQTypeOpNot, .type = "print" }
    //   // represents: not print
    struct MQType : MQ {
        MQTypeOp op{};
        std::string type;
        MediaQuery and_or_null;

        bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const override;
        bool EqualIgnoringWhitespace(const MQ* query) const override;
        uint32_t Hash() const override;
        std::unique_ptr<MQ> CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const override;
    };

    // A negated media query: "not (condition)".
    //
    // Example:
    //   MQNot { .inner = MQPlainOrBoolean{name: "color"} }
    //   // represents: not (color)
    struct MQNot : MQ {
    MediaQuery inner;

    bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const override;
    bool EqualIgnoringWhitespace(const MQ* query) const override;
    uint32_t Hash() const override;
    std::unique_ptr<MQ> CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const override;
    };

    // Binary operators that combine multiple media query terms.
    enum class MQBinaryOp : uint8_t {
        kMQBinaryOpAnd,
        kMQBinaryOpOr,
    };

    // Combines multiple media query terms with AND or OR operators.
    //
    // Example:
    //   MQBinary { .op = kMQBinaryOpAnd, .terms = [..., ...] }
    //   // represents: query1 and query2
    struct MQBinary : MQ {
        MQBinaryOp op{};
        std::vector<MediaQuery> terms;

        bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const override;
        bool EqualIgnoringWhitespace(const MQ* query) const override;
        uint32_t Hash() const override;
        std::unique_ptr<MQ> CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const override;
    };

    // A fallback for media queries that couldn't be parsed into a more
    // specific AST node. Stores the raw tokens so they can be preserved
    // in output without loss.
    struct MQArbitraryTokens : MQ {
        std::vector<Token> tokens;

        bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const override;
        bool EqualIgnoringWhitespace(const MQ* query) const override;
        uint32_t Hash() const override;
        std::unique_ptr<MQ> CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const override;
    };

    // A plain or boolean media feature like "color", "hover", or
    // "width: 100px". Boolean features have no value; plain features
    // have a colon and value.
    //
    // Example:
    //   MQPlainOrBoolean { .name = "hover" }
    //   // represents: hover (boolean)
    //
    //   MQPlainOrBoolean { .name = "width", .value_or_nil = {Token("100px")} }
    //   // represents: width: 100px
    struct MQPlainOrBoolean : MQ {
        std::string name;
        std::vector<Token> value_or_nil;

        bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const override;
        bool EqualIgnoringWhitespace(const MQ* query) const override;
        uint32_t Hash() const override;
        std::unique_ptr<MQ> CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const override;
    };

    // Comparison operators for range-style media features.
    enum class MQCmp : uint8_t {
        kMQCmpNone,
        kMQCmpEq,   // =
        kMQCmpLt,   // <
        kMQCmpLe,   // <=
        kMQCmpGt,   // >
        kMQCmpGe,   // >=
    };

    // Returns the string representation of a comparison operator.
    const char* ToString(MQCmp cmp);

    // Returns -1 for <, <= and +1 for >, >= and 0 for = and none.
    int Dir(MQCmp cmp);

    // Flips a comparison operator to its opposite (< -> >, <= -> >=).
    MQCmp Flip(MQCmp cmp);

    // Reverses a comparison operator for the opposite side of a range.
    // Used when rewriting "500px <= width" to "width >= 500px".
    MQCmp Reverse(MQCmp cmp);

    // A range-style media feature like "500px <= width <= 1200px" or
    // "width >= 500px". Supports single-ended and double-ended ranges.
    //
    // Example:
    //   MQRange {
    //     .name = "width",
    //     .before = {Token("500px")},
    //     .after = {Token("1200px")},
    //     .before_cmp = kMQCmpLe,
    //     .after_cmp = kMQCmpLe
    //   }
    //   // represents: 500px <= width <= 1200px
    struct MQRange : MQ {
        std::vector<Token> before;
        std::string name;
        std::vector<Token> after;
        guchho::logger::Loc name_loc;
        MQCmp before_cmp{};
        MQCmp after_cmp{};

        bool Equal(const MQ* query, const CrossFileEqualityCheck* check) const override;
        bool EqualIgnoringWhitespace(const MQ* query) const override;
        uint32_t Hash() const override;
        std::unique_ptr<MQ> CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const override;
    };

    // -- Selectors --

    // Represents a combinator between compound selectors in a complex
    // selector. The byte value indicates the combinator type: ' ' for
    // descendant, '>' for child, '+' for adjacent sibling, '~' for
    // general sibling. A zero byte means no combinator.
    struct Combinator {
        guchho::logger::Loc loc;
        uint8_t byte_{}; // Optional, may be 0 for no combinator
    };

    // A single token within a selector name (e.g., an identifier in a
    // class name or element name).
    struct NameToken {
        std::string text;
        guchho::logger::Range range;
        TokenType kind{};

        bool Equal(const NameToken& b) const;
    };

    // A namespaced name like "svg|circle" or just "div". The namespace
    // prefix is optional; if present, it's followed by a "|" character.
    struct NamespacedName {
        // If present, this is an identifier or "*" (any namespace)
        std::shared_ptr<NameToken> namespace_prefix;

        // The local name, an identifier or "*" (any element)
        NameToken name;

        // Returns the source range covering the full namespaced name.
        guchho::logger::Range Range() const;

        // Creates a deep copy of this namespaced name.
        NamespacedName Clone() const;

        // Compares two namespaced names for structural equality.
        bool Equal(const NamespacedName& b) const;
    };

    // Base class for all subclass selector AST nodes. Subclass selectors
    // are the parts of a selector that follow the type selector (if any):
    // class selectors, ID selectors, attribute selectors, pseudo-classes,
    // and pseudo-elements.
    class SS {
    public:
        virtual ~SS() = default;

        // Structural equality check, optionally resolving cross-file refs.
        virtual bool Equal(const SS* ss, const CrossFileEqualityCheck* check) const = 0;

        // Hash computation for deduplication.
        virtual uint32_t Hash() const = 0;

        // Deep copy.
        virtual std::unique_ptr<SS> Clone() const = 0;
    };

    // Wraps a subclass selector with its source range.
    struct SubclassSelector {
        std::shared_ptr<SS> data;
        guchho::logger::Range range;
    };

    // An ID selector like "#myId". The name is a LocRef that can be
    // renamed during minification.
    struct SSHash : SS {
        guchho::compiler::LocRef name;

        bool Equal(const SS* ss, const CrossFileEqualityCheck* check) const override;
        uint32_t Hash() const override;
        std::unique_ptr<SS> Clone() const override;
    };

    // A class selector like ".myClass". The name is a LocRef that can be
    // renamed during minification.
    struct SSClass : SS {
        guchho::compiler::LocRef name;

        bool Equal(const SS* ss, const CrossFileEqualityCheck* check) const override;
        uint32_t Hash() const override;
        std::unique_ptr<SS> Clone() const override;
    };

    // An attribute selector like "[href^='https']" or "[disabled]". Supports
    // all CSS attribute matching operators.
    struct SSAttribute : SS {
        std::string matcher_op;      // Either "" or one of: "=" "~=" "|=" "^=" "$=" "*="
        std::string matcher_value;
        NamespacedName namespaced_name;
        uint8_t matcher_modifier{};  // Either 0 or one of: 'i' 'I' 's' 'S'

        bool Equal(const SS* ss, const CrossFileEqualityCheck* check) const override;
        uint32_t Hash() const override;
        std::unique_ptr<SS> Clone() const override;
    };

    // A pseudo-class or pseudo-element selector like ":hover", "::before",
    // or ":nth-child(2n+1)". The args field distinguishes between no
    // parentheses (nil) and empty parentheses (non-nil but empty vector),
    // which is important because ":foo()" is a parse error but should be
    // preserved in output.
    struct SSPseudoClass : SS {
        std::string name;

        // nil means no parentheses (e.g., ":hover").
        // non-nil but possibly empty means parentheses present (e.g., ":is()").
        // This distinction matters because ":foo()" is invalid but shouldn't
        // accidentally become valid when printed.
        std::shared_ptr<std::vector<Token>> args;
        bool is_element{}; // If true, prefixed by "::" instead of ":"

        bool Equal(const SS* ss, const CrossFileEqualityCheck* check) const override;
        uint32_t Hash() const override;
        std::unique_ptr<SS> Clone() const override;
    };

    // Identifies well-known pseudo-class types for specialized handling
    // during lowering and minification.
    enum class PseudoClassKind : uint8_t {
        kPseudoClassGlobal,      // :global() - CSS Modules global scope
        kPseudoClassHas,         // :has() - relational pseudo-class
        kPseudoClassIs,          // :is() - matching pseudo-class
        kPseudoClassLocal,       // :local() - CSS Modules local scope
        kPseudoClassNot,         // :not() - negation pseudo-class
        kPseudoClassNthChild,    // :nth-child() - positional
        kPseudoClassNthLastChild, // :nth-last-child() - reverse positional
        kPseudoClassNthLastOfType, // :nth-last-of-type() - type reverse positional
        kPseudoClassNthOfType,   // :nth-of-type() - type positional
        kPseudoClassWhere,       // :where() - specificity-zero matching
    };

    // Returns true if this pseudo-class kind uses the An+B index syntax.
    bool HasNthIndex(PseudoClassKind kind);

    // Returns the string name of a pseudo-class kind.
    const char* ToString(PseudoClassKind kind);

    // Represents the "An+B" notation used in :nth-child() and similar
    // pseudo-classes. The "a" and "b" fields are stored as strings to
    // preserve the original formatting. "b" may be "even" or "odd" as
    // keywords.
    struct NthIndex {
        std::string a;
        std::string b; // May be "even" or "odd"

        // Simplifies the An+B notation (e.g., "1n+0" -> "n", "2n+1" -> "2n+1").
        void Minify();
    };

    inline bool operator==(const NthIndex& a, const NthIndex& b) {
        return a.a == b.a && a.b == b.b;
    }

    inline bool operator!=(const NthIndex& a, const NthIndex& b) {
        return !(a == b);
    }



    // A compound selector combines a type selector (optional) with zero
    // or more subclass selectors, plus an optional combinator. This is
    // the building block of complex selectors.
    //
    // Example:
    //   div.container:hover  ->  type="div", subclass=[.container, :hover]
    //   & > .child           ->  nesting=[&], combinator=">", type omitted, subclass=[.child]
    struct CompoundSelector {
        std::shared_ptr<NamespacedName> type_selector;
        std::vector<SubclassSelector> subclass_selectors;
        std::vector<guchho::logger::Loc> nesting_selector_locs; // "&" vs "&&" differ in specificity
        Combinator combinator;                                 // Optional, may be 0

        // True if this was a bare ":local" or ":global" that produced an empty "&"
        bool was_empty_from_local_or_global{};

        // Returns true if this compound selector is a single "&" (nesting selector).
        bool IsSingleAmpersand() const;

        // Returns true if this selector is invalid because it's empty
        // (e.g., just a combinator with no actual selector content).
        bool IsInvalidBecauseEmpty() const;

        // Returns the source range covering the full compound selector.
        guchho::logger::Range Range() const;

        // Creates a deep copy of this compound selector.
        CompoundSelector Clone() const;
    };

    // A complex selector is a chain of compound selectors connected by
    // combinators. It represents a full CSS selector like "div > .child:hover".
    struct ComplexSelector {
        std::vector<CompoundSelector> selectors;

        // Creates a deep copy of this complex selector.
        ComplexSelector Clone() const;

        // Returns true if this selector contains a nesting combinator ("&").
        bool ContainsNestingCombinator() const;

        // Returns true if this selector starts with a combinator (making it
        // relative to the parent).
        bool IsRelative() const;

        // Returns true if this selector uses a pseudo-element (::before, ::after, etc.).
        bool UsesPseudoElement() const;

        // Structural equality check, optionally resolving cross-file references.
        bool Equal(const ComplexSelector& b, const CrossFileEqualityCheck* check) const;
    };

    // Compares two complex selector lists for structural equality.
    bool ComplexSelectorsEqual(const std::vector<ComplexSelector>& a, const std::vector<ComplexSelector>& b, const CrossFileEqualityCheck* check);

    // Hashes a complex selector list for deduplication.
    uint32_t HashComplexSelectors(uint32_t hash, const std::vector<ComplexSelector>& selectors);

    // Recursively checks if a token vector contains an ampersand (&).
    // Used to detect nesting selectors in property values.
    bool TokensContainAmpersandRecursive(const std::vector<Token>& tokens);

    // -- Composes --

    // Represents an imported composes name from another file. When a CSS
    // Modules file uses "composes: foo from './other.css'", this struct
    // tracks the local alias and the import record index.
    struct ImportedComposesName {
        std::string alias;
        guchho::logger::Loc alias_loc;
        uint32_t import_record_index{};
    };

    // Tracks the composes relationships for a CSS class. Each composes
    // directive associates a class with one or more other class names
    // (local or imported). The properties map tracks which CSS properties
    // the class declares, used for validating correct composes usage.
    struct Composes {
        // Local or global class names this class composes with.
        std::vector<guchho::compiler::LocRef> names;

        // Class names imported from other files.
        std::vector<ImportedComposesName> imported_names;

        // Maps CSS property names to their source locations. Used to warn
        // when composes is used with properties that shouldn't be composed.
        std::unordered_map<std::string, guchho::logger::Loc> properties;
    };

    // Hash functor for compiler::Ref values, combining source_index and
    // inner_index into a single hash.
    struct RefHash {
        size_t operator()(const guchho::compiler::Ref& r) const {
            return (static_cast<size_t>(r.source_index) << 32) | r.inner_index;
        }
    };


    // Base class for all CSS rule AST nodes. Rules include declarations,
    // at-rules, comments, and selector blocks. Each concrete rule type
    // inherits from this and implements the virtual methods for equality
    // checking and hashing.
    class RuleData {
        public:
            virtual ~RuleData() = default;

            // Structural equality check, optionally resolving cross-file references.
            virtual bool Equal(
                const RuleData* rule,
                const CrossFileEqualityCheck* check) const = 0;

            // Hash computation. Returns nullopt if the rule type doesn't
            // support hashing (e.g., bad declarations).
            virtual std::optional<uint32_t> Hash() const = 0;
    };

    // Wraps a rule with its source location for error reporting.
    struct Rule {
        std::shared_ptr<RuleData> data;
        guchho::logger::Loc loc{};
    };

    // Compares two rule lists for structural equality.
    bool RulesEqual(const std::vector<Rule>& a, const std::vector<Rule>& b, const CrossFileEqualityCheck* check);

    // Hashes a rule list for deduplication.
    uint32_t HashRules(uint32_t hash, const std::vector<Rule>& rules);

    // An @charset rule specifying the character encoding of the stylesheet.
    // Example: @charset "UTF-8";
    struct RAtCharset : RuleData {
    std::string encoding;

    bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // Conditions that can be applied to an @import rule, including media
    // queries, layer names, and support() conditions.
    struct ImportConditions {
        std::vector<MediaQuery> queries;
        std::vector<Token> layers;
        std::vector<Token> supports;

        // Deep-copies conditions, remapping import record references.
        ImportConditions CloneWithImportRecords(
            const std::vector<guchho::compiler::ImportRecord>& import_records_in,
            std::vector<guchho::compiler::ImportRecord>& import_records_out) const;
    };

    // An @import rule that pulls in another stylesheet. The
    // import_record_index points to the corresponding entry in the AST's
    // import_records vector.
    struct RAtImport : RuleData {
        std::shared_ptr<ImportConditions> import_conditions;
        uint32_t import_record_index{};

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A single block within an @keyframes rule. Each block has one or
    // more selectors (like "0%", "50%", "from", "to") and a set of
    // declarations.
    struct KeyframeBlock {
        std::vector<std::string> selectors;
        std::vector<Rule> rules;
        guchho::logger::Loc loc;
        guchho::logger::Loc close_brace_loc;
    };

    // An @keyframes rule defining animation keyframes. The name is a
    // LocRef that can be renamed during minification.
    struct RAtKeyframes : RuleData {
        std::string at_token;      // "keyframes" or "-webkit-keyframes"
        guchho::compiler::LocRef name;
        std::vector<KeyframeBlock> blocks;
        guchho::logger::Loc close_brace_loc;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A known at-rule that contains CSS rules (e.g., @supports, @container).
    // The prelude tokens are the content between the at-rule name and the
    // opening brace.
    struct RKnownAt : RuleData {
        std::string at_token;
        std::vector<Token> prelude;
        std::vector<Rule> rules;
        guchho::logger::Loc close_brace_loc;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // An unknown at-rule whose block content is opaque (not parsed into
    // declarations). This preserves vendor-prefixed or experimental
    // at-rules without losing their content.
    struct RUnknownAt : RuleData {
        std::string at_token;
        std::vector<Token> prelude;
        std::vector<Token> block;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A rule set with parsed selectors (e.g., ".class { color: red; }").
    // The selectors have been fully parsed into ComplexSelector AST nodes.
    struct RSelector : RuleData {
        std::vector<ComplexSelector> selectors;
        std::vector<Rule> rules;
        guchho::logger::Loc close_brace_loc;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A qualified rule whose prelude hasn't been parsed into selectors.
    // Used for @page rules or other contexts where the prelude isn't
    // a standard selector.
    struct RQualified : RuleData {
        std::vector<Token> prelude;
        std::vector<Rule> rules;
        guchho::logger::Loc close_brace_loc;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A CSS declaration like "color: red" or "margin: 10px". The key_text
    // is the property name string, key is the recognized declaration type,
    // and value contains the parsed tokens.
    struct RDeclaration : RuleData {
        std::string key_text;
        std::vector<Token> value;
        guchho::logger::Range key_range;
        Declarations key{};
        bool important{};

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A declaration that couldn't be parsed (syntax error). The raw tokens
    // are preserved so they can be output without loss.
    struct RBadDeclaration : RuleData {
        std::vector<Token> tokens;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A CSS comment (/* ... */). Preserved in the AST so it can be
    // output in the final CSS if desired.
    struct RComment : RuleData {
        std::string text;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // An @layer rule that defines or assigns styles to a cascade layer.
    // Can contain nested rules or just declare layer names without a block.
    struct RAtLayer : RuleData {
        std::vector<std::vector<std::string>> names;
        std::vector<Rule> rules;
        guchho::logger::Loc close_brace_loc;
        bool has_block = false;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // A pseudo-class that takes a selector list as its argument, such as
    // :is(), :not(), :has(), :where(), and the nth-child variants.
    // See https://drafts.csswg.org/selectors/#grouping
    struct SSPseudoClassWithSelectorList : SS {
        std::vector<ComplexSelector> selectors;
        NthIndex index;
        PseudoClassKind kind{};

        bool Equal(const SS* ss, const CrossFileEqualityCheck* check) const override;
        uint32_t Hash() const override;
        std::unique_ptr<SS> Clone() const override;
    };

    // An @media rule containing media queries and nested declarations.
    struct RAtMedia : RuleData {
        std::vector<MediaQuery> queries;
        std::vector<Rule> rules;
        guchho::logger::Loc close_brace_loc;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // An @scope rule that limits selector matching to a subtree of the DOM.
    struct RAtScope : RuleData {
        std::vector<ComplexSelector> start;
        std::vector<ComplexSelector> end;
        std::vector<Rule> rules;
        guchho::logger::Loc close_brace_loc;

        bool Equal(const RuleData* rule, const CrossFileEqualityCheck* check) const override;
        std::optional<uint32_t> Hash() const override;
    };

    // The top-level AST for a parsed CSS file. Contains all rules,
    // symbols, import records, and scope information needed for
    // further processing and minification.
    struct AST {
        // All symbols defined or referenced in this file.
        std::vector<guchho::compiler::Symbol> symbols;

        // Character frequency data for optimal encoding.
        std::shared_ptr<guchho::compiler::CharFreq> char_freq;

        // Import records mapping source references to target files.
        std::vector<guchho::compiler::ImportRecord> import_records;

        // The top-level CSS rules.
        std::vector<Rule> rules;

        // The source map comment span, if present.
        lexer::Span source_map_comment;

        // Approximate line count of the source file.
        int32_t approximate_line_count{};

        // Locally-scoped symbols created in this file.
        std::vector<guchho::compiler::LocRef> local_symbols;

        // Local scope map for symbol lookup within this file.
        std::unordered_map<std::string, guchho::compiler::LocRef> local_scope;

        // Global scope map for cross-file symbol lookup.
        std::unordered_map<std::string, guchho::compiler::LocRef> global_scope;

        // Composes relationships keyed by class selector references.
        std::unordered_map<guchho::compiler::Ref, std::shared_ptr<Composes>, RefHash> composes;

        // Layer declarations, split into those before and after @import rules.
        // This ordering matters for cascade layer resolution.
        std::vector<std::vector<std::string>> layers_pre_import;
        std::vector<std::vector<std::string>> layers_post_import;
    };
}
