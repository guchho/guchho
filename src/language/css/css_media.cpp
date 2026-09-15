#include "guchho/css/css_helpers.hpp"

namespace guchho::css {

    // Appends a media query term to a vector, potentially flattening nested
    // binary expressions of the same operator type when minifying.
    //
    // When minify_syntax is true and the term is a binary expression with
    // the same operator as 'op', the child terms are flattened into the
    // output vector instead of adding the binary node itself.
    //
    // Input:  inner = [], term = MQBinary{op=AND, terms=[a, b]}, op=AND, minify=true
    // Output: [a, b]
    //
    // Input:  inner = [c], term = MQBinary{op=AND, terms=[a, b]}, op=AND, minify=true
    // Output: [c, a, b]
    //
    // Input:  inner = [], term = MQBinary{op=AND, terms=[a, b]}, op=OR, minify=true
    // Output: [MQBinary{op=AND, terms=[a, b]}] (different operator, no flattening)
    //
    // Input:  inner = [], term = a, op=AND, minify=false
    // Output: [a] (not minifying, no flattening)
    std::vector<MediaQuery> AppendMediaTerm(std::vector<MediaQuery> inner, const MediaQuery& term,
                                            MQBinaryOp op, bool minify_syntax) {
        if (auto binary = std::dynamic_pointer_cast<MQBinary>(term.data); binary) {
            if (binary->op == op && minify_syntax) {
                for (const MediaQuery& t : binary->terms) {
                    inner.push_back(t);
                }
                return inner;
            }
        }
        inner.push_back(term);
        return inner;
    }

    // Lowers a media range expression (e.g., "width >= 600px") into a
    // legacy media feature syntax that uses min-/max- prefixes.
    //
    // The conversion rules are:
    //   - "foo <= 123" => "max-foo: 123"
    //   - "foo >= 123" => "min-foo: 123"
    //   - "foo < 123"  => "not (min-foo: 123)"
    //   - "foo > 123"  => "not (max-foo: 123)"
    //   - "foo = 123"  => "foo: 123"
    //
    // Input:  name="width", cmp=GE, value=["600px"]
    // Output: MediaQuery{MQPlainOrBoolean{name="min-width", value=["600px"]}}
    //
    // Input:  name="width", cmp=LT, value=["600px"]
    // Output: MediaQuery{MQNot{inner=MQPlainOrBoolean{name="min-width", value=["600px"]}}}
    //
    // Input:  name="color-gamut", cmp=EQ, value=["srgb"]
    // Output: MediaQuery{MQPlainOrBoolean{name="color-gamut", value=["srgb"]}}
    MediaQuery LowerMediaRange(
        guchho::logger::Loc loc, 
        const std::string& name, 
        MQCmp cmp,
        std::vector<Token> value
    ) {
        auto make_plain = [&](const std::string& property_name) {
            auto plain = std::make_shared<MQPlainOrBoolean>();
            plain->name = property_name;
            plain->value_or_nil = value;
            return plain;
        };

        switch (cmp) {
        case MQCmp::kMQCmpLe:
            // "foo <= 123" => "max-foo: 123"
            return MediaQuery{make_plain("max-" + name), loc};

        case MQCmp::kMQCmpGe:
            // "foo >= 123" => "min-foo: 123"
            return MediaQuery{make_plain("min-" + name), loc};

        case MQCmp::kMQCmpLt: {
            // "foo < 123" => "not (min-foo: 123)"
            auto not_data = std::make_shared<MQNot>();
            not_data->inner = MediaQuery{make_plain("min-" + name), loc};
            return MediaQuery{not_data, loc};
        }

        case MQCmp::kMQCmpGt: {
            // "foo > 123" => "not (max-foo: 123)"
            auto not_data = std::make_shared<MQNot>();
            not_data->inner = MediaQuery{make_plain("max-" + name), loc};
            return MediaQuery{not_data, loc};
        }

        default:
            // "foo = 123" => "foo: 123"
            return MediaQuery{make_plain(name), loc};
        }
    }

    // Parses a plain or boolean media feature from a token span. This
    // handles two forms:
    //
    //   1. Boolean: "color-gamut" (just an identifier)
    //   2. Plain: "min-width: 800px" (identifier, colon, value)
    //
    // Input:  tokens = ["color-gamut"]
    // Output: MQPlainOrBoolean{name="color-gamut"}
    //
    // Input:  tokens = ["min-width", ":", "800px"]
    // Output: MQPlainOrBoolean{name="min-width", value=["800px"]}
    //
    // Input:  tokens = ["min-width", ":", "800px", "and"]
    // Output: nullptr (trailing tokens)
    //
    // Input:  tokens = []
    // Output: nullptr
    std::shared_ptr<MQPlainOrBoolean> ParsePlainOrBooleanMediaFeature(std::span<Token> tokens) {
        if (tokens.size() == 1 && tokens[0].kind == TokenType::kIdent) {
            auto result = std::make_shared<MQPlainOrBoolean>();
            result->name = tokens[0].text;
            return result;
        }

        if (tokens.size() >= 3 && tokens[0].kind == TokenType::kIdent && tokens[1].kind == TokenType::kColon) {
            auto [value, rest] = ScanMediaValue(tokens.subspan(2));
            if (rest.empty()) {
                auto result = std::make_shared<MQPlainOrBoolean>();
                result->name = tokens[0].text;
                result->value_or_nil.assign(value.begin(), value.end());
                return result;
            }
        }

        return nullptr;
    }

    // Parses a range media feature from a token span. This handles the
    // modern CSS media query range syntax:
    //
    //   - Simple range: "600px <= width"
    //   - Simple range: "width <= 800px"
    //   - Double range: "400px <= width <= 800px"
    //
    // The parser identifies the feature name (must be a single identifier)
    // and the comparison operators, then constructs an MQRange node.
    //
    // Input:  tokens = ["600px", "<=", "width"]
    // Output: MQRange{before=["600px"], before_cmp=LE, name="width"}
    //
    // Input:  tokens = ["width", "<=", "800px"]
    // Output: MQRange{name="width", after_cmp=LE, after=["800px"]}
    //
    // Input:  tokens = ["400px", "<=", "width", "<=", "800px"]
    // Output: MQRange{before=["400px"], before_cmp=LE, name="width", after_cmp=LE, after=["800px"]}
    //
    // Input:  tokens = ["width", ">", "800px"]
    // Output: nullptr (single comparison with GT is not a range)
    std::shared_ptr<MQRange> ParseRangeMediaFeature(std::span<Token> tokens) {
        auto [first, rest_after_first] = ScanMediaValue(tokens);
        if (!first.empty()) {
            auto [first_cmp, rest_after_cmp] = ScanMediaComparison(rest_after_first);
            if (first_cmp != MQCmp::kMQCmpNone) {
                auto [second, rest_after_second] = ScanMediaValue(rest_after_cmp);
                if (!second.empty()) {
                    if (rest_after_second.empty()) {
                        auto [name, name_loc, name_ok] = IsSingleIdent(first);
                        if (name_ok) {
                            auto result = std::make_shared<MQRange>();
                            result->name = name;
                            result->name_loc = name_loc;
                            result->after_cmp = first_cmp;
                            result->after.assign(second.begin(), second.end());
                            return result;
                        }

                        std::tie(name, name_loc, name_ok) = IsSingleIdent(second);
                        if (name_ok) {
                            auto result = std::make_shared<MQRange>();
                            result->before.assign(first.begin(), first.end());
                            result->before_cmp = first_cmp;
                            result->name = name;
                            result->name_loc = name_loc;
                            return result;
                        }
                    } else {
                        auto [name, name_loc, name_ok] = IsSingleIdent(second);
                        if (name_ok) {
                            auto [second_cmp, rest_after_second_cmp] = ScanMediaComparison(rest_after_second);
                            if (second_cmp != MQCmp::kMQCmpNone) {
                                int f = Dir(first_cmp);
                                int s = Dir(second_cmp);
                                if ((f < 0 && s < 0) || (f > 0 && s > 0)) {
                                    auto [third, rest_after_third] = ScanMediaValue(rest_after_second_cmp);
                                    if (!third.empty() && rest_after_third.empty()) {
                                        auto result = std::make_shared<MQRange>();
                                        result->before.assign(first.begin(), first.end());
                                        result->before_cmp = first_cmp;
                                        result->name = name;
                                        result->name_loc = name_loc;
                                        result->after_cmp = second_cmp;
                                        result->after.assign(third.begin(), third.end());
                                        return result;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        return nullptr;
    }

    // Attempts to simplify a "not" media query by applying logical
    // identities when minifying. The simplifications are:
    //
    //   1. Double negation: "not (not a)" => "a"
    //   2. De Morgan's laws:
    //      - "not ((not a) and (not b))" => "a or b"
    //      - "not ((not a) or (not b))" => "a and b"
    //   3. Range negation: flip comparison operators
    //      - "not (width < 600px)" => "width >= 600px"
    //
    // If no simplification applies, wraps the inner query in a new MQNot node.
    //
    // Input:  inner = MQNot{inner=a}, minify=true
    // Output: a (double negation)
    //
    // Input:  inner = MQBinary{op=AND, terms=[MQNot{a}, MQNot{b}]}, minify=true
    // Output: MQBinary{op=OR, terms=[a, b]} (De Morgan's)
    //
    // Input:  inner = MQRange{name="width", after_cmp=LT, after=["600px"]}, minify=true
    // Output: MQRange{name="width", after_cmp=GE, after=["600px"]} (range flip)
    //
    // Input:  inner = a, minify=false
    // Output: MQNot{inner=a} (not minifying, no simplification)
    MediaQuery MaybeSimplifyMediaNot(guchho::logger::Loc loc, MediaQuery inner, bool minify_syntax) {
        if (minify_syntax) {
            if (auto not_data = std::dynamic_pointer_cast<MQNot>(inner.data); not_data) {
                // "not (not a)" => "a"
                // "not (not (not a))" => "not a"
                return not_data->inner;
            }

            if (auto binary = std::dynamic_pointer_cast<MQBinary>(inner.data); binary) {
                // "not ((not a) and (not b))" => "a or b"
                // "not ((not a) or (not b))" => "a and b"
                std::vector<MediaQuery> terms;
                terms.reserve(binary->terms.size());
                for (const MediaQuery& term : binary->terms) {
                    if (auto term_not = std::dynamic_pointer_cast<MQNot>(term.data); term_not) {
                        terms.push_back(term_not->inner);
                    } else {
                        break;
                    }
                }
                if (terms.size() == binary->terms.size()) {
                    binary->op = (binary->op == MQBinaryOp::kMQBinaryOpAnd) ? MQBinaryOp::kMQBinaryOpOr
                                                                            : MQBinaryOp::kMQBinaryOpAnd;
                    binary->terms = std::move(terms);
                    return inner;
                }
            }

            if (auto range = std::dynamic_pointer_cast<MQRange>(inner.data); range) {
                if ((range->before_cmp == MQCmp::kMQCmpNone && range->after_cmp != MQCmp::kMQCmpEq) ||
                    (range->after_cmp == MQCmp::kMQCmpNone && range->before_cmp != MQCmp::kMQCmpEq)) {
                    range->before_cmp = Flip(range->before_cmp);
                    range->after_cmp = Flip(range->after_cmp);
                    return inner;
                }
            }
        }

        auto not_data = std::make_shared<MQNot>();
        not_data->inner = std::move(inner);
        return MediaQuery{not_data, loc};
    }

    // Checks if a token span consists of exactly one identifier token.
    // Returns the identifier text, its source location, and a success flag.
    //
    // Input:  tokens = ["width"]
    // Output: {"width", loc, true}
    //
    // Input:  tokens = ["600px"]
    // Output: {"", {}, false} (not an identifier)
    //
    // Input:  tokens = ["width", "600px"]
    // Output: {"", {}, false} (multiple tokens)
    std::tuple<std::string, guchho::logger::Loc, bool> IsSingleIdent(std::span<Token> tokens) {
        if (tokens.size() == 1 && tokens[0].kind == TokenType::kIdent) {
            return {tokens[0].text, tokens[0].loc, true};
        }
        return {"", guchho::logger::Loc{}, false};
    }

    // Scans a comparison operator from the beginning of a token span.
    // Supports the following operators:
    //
    //   - "="  => MQCmpEq
    //   - "<"  => MQCmpLt
    //   - "<=" => MQCmpLe (two tokens, no whitespace between them)
    //   - ">"  => MQCmpGt
    //   - ">=" => MQCmpGe (two tokens, no whitespace between them)
    //
    // The function requires that "<=" and ">=" are written without whitespace
    // between the two characters (e.g., "< =123" is not recognized as "<=").
    //
    // Input:  tokens = ["<=", "600px"]
    // Output: {MQCmpLe, ["600px"]}
    //
    // Input:  tokens = ["<", "600px"]
    // Output: {MQCmpLt, ["600px"]}
    //
    // Input:  tokens = ["600px"]
    // Output: {MQCmpNone, ["600px"]} (no comparison found)
    std::pair<MQCmp, std::span<Token>> ScanMediaComparison(std::span<Token> tokens) {
        if (tokens.size() >= 1) {
            switch (tokens[0].kind) {
            case TokenType::kDelimEquals:
                return {MQCmp::kMQCmpEq, tokens.subspan(1)};

            case TokenType::kDelimLessThan:
                // Handle "<=" or "<"
                if (tokens.size() >= 2 && tokens[1].kind == TokenType::kDelimEquals &&
                    ((static_cast<uint8_t>(tokens[0].whitespace) & static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter)) |
                    (static_cast<uint8_t>(tokens[1].whitespace) & static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore))) == 0) {
                    return {MQCmp::kMQCmpLe, tokens.subspan(2)};
                }
                return {MQCmp::kMQCmpLt, tokens.subspan(1)};

            case TokenType::kDelimGreaterThan:
                // Handle ">=" or ">"
                if (tokens.size() >= 2 && tokens[1].kind == TokenType::kDelimEquals &&
                    ((static_cast<uint8_t>(tokens[0].whitespace) & static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter)) |
                    (static_cast<uint8_t>(tokens[1].whitespace) & static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore))) == 0) {
                    return {MQCmp::kMQCmpGe, tokens.subspan(2)};
                }
                return {MQCmp::kMQCmpGt, tokens.subspan(1)};

            default:
                break;
            }
            
        }

        return {MQCmp::kMQCmpNone, tokens};
    }

    // Scans a media feature value from the beginning of a token span.
    // Returns the value tokens and the remaining unconsumed tokens.
    //
    // Recognized value types:
    //   - Dimension: "600px", "1.5em"
    //   - Ident: "srgb", "dark"
    //   - Number: "600", "1.5"
    //   - Ratio: "16 / 9" (number, slash, number)
    //
    // Whitespace is trimmed from the endpoints of the value tokens.
    //
    // Input:  tokens = ["600px", "and", "color"]
    // Output: value=["600px"], rest=["and", "color"]
    //
    // Input:  tokens = ["16", "/", "9", ">", "width"]
    // Output: value=["16", "/", "9"], rest=[">", "width"]
    //
    // Input:  tokens = ["and", "color"]
    // Output: value=[], rest=["and", "color"] (no value found)
    //
    // Input:  tokens = ["srgb"]
    // Output: value=["srgb"], rest=[]
    std::pair<std::span<Token>, std::span<Token>> ScanMediaValue(std::span<Token> tokens) {
        size_t n = 0;

        if (tokens.size() >= 1) {
            switch (tokens[0].kind) {
            case TokenType::kDimension:
            case TokenType::kIdent:
                n = 1;
                break;

            case TokenType::kNumber:
                // Potentially recognize a ratio which is "<number> / <number>"
                if (tokens.size() >= 3 && tokens[1].kind == TokenType::kDelimSlash && tokens[2].kind == TokenType::kNumber) {
                    n = 3;
                } else {
                    n = 1;
                }
                break;

            default:
                break;
            }
        }

        // Trim whitespace at the endpoints
        if (n > 0) {
            tokens[0].whitespace = static_cast<WhitespaceFlags>(
                static_cast<uint8_t>(tokens[0].whitespace) & ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
            tokens[n - 1].whitespace = static_cast<WhitespaceFlags>(
                static_cast<uint8_t>(tokens[n - 1].whitespace) & ~static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
        }

        return {tokens.first(n), tokens.subspan(n)};
    }

}
