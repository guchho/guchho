#include "guchho/css/css_helpers.hpp"
#include "guchho/helpers.hpp"

namespace guchho::css {

    namespace {

        // Parses a string view into a double value. Returns std::nullopt if
        // the entire string cannot be parsed as a valid floating-point number.
        //
        // Input:  text = "3.14"
        // Output: 3.14
        //
        // Input:  text = "var(--x)"
        // Output: std::nullopt (not a pure number)
        //
        // Input:  text = "10px"
        // Output: std::nullopt (trailing characters)
        //
        // Edge cases:
        //   - Empty string returns std::nullopt.
        //   - Strings with leading/trailing whitespace that isn't part of
        //     the number return std::nullopt.
        std::optional<double> ParseFloat(std::string_view text) {
            std::string s(text);
            char* end = nullptr;
            double value = std::strtod(s.c_str(), &end);
            if (end != s.c_str() + s.size()) {
                return std::nullopt;
            }
            return value;
        }

        // Converts a double to a string representation suitable for CSS calc()
        // expressions. The output is normalized to remove trailing zeros, leading
        // zeros, and unnecessary decimal points.
        //
        // The conversion follows these steps:
        //   1. Return std::nullopt for NaN or Infinity values.
        //   2. Format the number with 5 decimal places.
        //   3. Strip trailing zeros after the decimal point.
        //   4. Strip the decimal point if no fractional part remains.
        //   5. Remove leading zero (e.g., "0.5" → ".5").
        //   6. Remove zero after minus sign (e.g., "-0.5" → "-.5").
        //   7. Verify the round-trip is exact; bail if not.
        //
        // Input:  a = 1.0
        // Output: "1"
        //
        // Input:  a = 0.5
        // Output: ".5"
        //
        // Input:  a = -0.5
        // Output: "-.5"
        //
        // Input:  a = 100.0
        // Output: "100"
        //
        // Input:  a = NaN
        // Output: std::nullopt
        //
        // Edge cases:
        //   - Very large or very small numbers may fail the round-trip
        //     check and return std::nullopt.
        //   - Numbers that cannot be exactly represented in IEEE 754
        //     double precision return std::nullopt.
        std::optional<std::string> FloatToStringForCalc(double a) {
            // Handle non-finite cases
            if (std::isnan(a) || std::isinf(a)) {
                return std::nullopt;
            }

            // Print the number as a string
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.5f", a);
            std::string text(buf);
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            if (text.rfind("0.", 0) == 0) {
                text = text.substr(1);
            } else if (text.rfind("-0.", 0) == 0) {
                text = "-" + text.substr(2);
            }

            // Bail if the number is not exactly represented
            auto number = ParseFloat(text);
            if (!number.has_value() || *number != a) {
                return std::nullopt;
            }

            return text;
        }

        // Abstract base class for calc() expression tree nodes, following the
        // CSS Values 4 specification for calc() internal representation.
        // See: https://www.w3.org/TR/css-values-4/#calc-internal
        //
        // Each node can:
        //   - ConvertToToken: Serialize the node back to a CSS token tree.
        //   - PartiallySimplify: Attempt to simplify the expression.
        class CalcTerm : public std::enable_shared_from_this<CalcTerm> {
        public:
            virtual ~CalcTerm() = default;

            virtual std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) = 0;
            virtual std::shared_ptr<CalcTerm> PartiallySimplify() = 0;
        };

        // Pairs a CalcTerm with the source location of the operator that
        // connects it to its parent node. This is used for error reporting
        // and for reconstructing the original source position during serialization.
        struct CalcTermWithOp {
            std::shared_ptr<CalcTerm> data;
            guchho::logger::Loc op_loc;
        };

        // Represents a sum of terms (addition/subtraction) in a calc() expression.
        // For example, "calc(1px + 2px - 3px)" produces a CalcSum with three terms.
        //
        // The terms vector contains alternating values and operators:
        //   terms[0] is the first operand
        //   terms[1] is the second operand (with its operator in op_loc)
        //   terms[2] is the third operand, etc.
        //
        // Subtraction is represented as a CalcNegate node wrapping the operand.
        class CalcSum : public CalcTerm {
        public:
            std::vector<CalcTermWithOp> terms;

            std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) override;
            std::shared_ptr<CalcTerm> PartiallySimplify() override;
        };

        // Represents a product of terms (multiplication/division) in a calc() expression.
        // For example, "calc(2px * 3)" produces a CalcProduct with two terms.
        //
        // Division is represented as a CalcInvert node wrapping the divisor.
        class CalcProduct : public CalcTerm {
        public:
            std::vector<CalcTermWithOp> terms;

            std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) override;
            std::shared_ptr<CalcTerm> PartiallySimplify() override;
        };

        // Represents unary negation in a calc() expression.
        // For example, "-1px" in "calc(1px + -1px)" is represented as
        // a CalcNegate wrapping the operand.
        //
        // During serialization, this becomes "(-1 * operand)".
        class CalcNegate : public CalcTerm {
        public:
            CalcTermWithOp term;

            std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) override;
            std::shared_ptr<CalcTerm> PartiallySimplify() override;
        };

        // Represents unary inversion (reciprocal) in a calc() expression.
        // For example, "1 / 2" in "calc(1px / 2)" wraps the divisor in
        // a CalcInvert node.
        //
        // During serialization, this becomes "(1 / operand)".
        class CalcInvert : public CalcTerm {
        public:
            CalcTermWithOp term;

            std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) override;
            std::shared_ptr<CalcTerm> PartiallySimplify() override;
        };

        // Represents a numeric value in a calc() expression, with an optional
        // unit. This is the leaf node for values that can be mathematically
        // combined (e.g., "10px", "50%", "3.14").
        //
        // The unit is empty for unitless numbers, "%" for percentages, or
        // the CSS unit string (e.g., "px", "em") for dimensions.
        class CalcNumeric : public CalcTerm {
        public:
            std::string unit;
            double number = 0;
            guchho::logger::Loc loc;

            std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) override;
            std::shared_ptr<CalcTerm> PartiallySimplify() override;
        };

        // Represents a non-numeric value in a calc() expression that cannot
        // be simplified. This includes CSS variables (var()), custom
        // properties, and other tokens that may resolve to numeric values
        // at runtime.
        //
        // The is_invalid_plus_or_minus flag tracks whether this token appears
        // adjacent to a +/- operator without proper whitespace, which is an
        // error per the CSS specification.
        class CalcValue : public CalcTerm {
        public:
            Token token;
            bool is_invalid_plus_or_minus = false;

            std::optional<Token> ConvertToToken(WhitespaceFlags whitespace) override;
            std::shared_ptr<CalcTerm> PartiallySimplify() override;
        };

        // Serializes a CalcSum node into a parenthesized token list.
        //
        // The serialization follows the CSS Values 4 specification:
        //   - If a child is a Negate node, use "-" operator
        //   - If a child is a negative numeric, use "-" and negate the value
        //   - Otherwise, use "+" operator
        //   - Product nodes inside sums are not parenthesized (algorithm deviation)
        //
        // Input:  CalcSum with terms [1px, +, 2px]
        // Output: Token "(" with children [1px, +, 2px]
        //
        // Input:  CalcSum with terms [1px, -, 2px]
        // Output: Token "(" with children [1px, -, CalcNegate(2px)]
        //
        // Edge cases:
        //   - Returns std::nullopt if any child cannot be serialized.
        std::optional<Token> CalcSum::ConvertToToken(WhitespaceFlags whitespace) {
            // Specification: https://www.w3.org/TR/css-values-4/#calc-serialize
            std::vector<Token> tokens;
            tokens.reserve(terms.size() * 2);

            // ALGORITHM DEVIATION: Avoid parenthesizing product nodes inside sum nodes
            if (auto product = std::dynamic_pointer_cast<CalcProduct>(terms[0].data); product) {
                auto token = product->ConvertToToken(whitespace);
                if (!token.has_value()) {
                    return std::nullopt;
                }
                for (const Token& child : *token->children) {
                    tokens.push_back(child);
                }
            } else {
                auto token = terms[0].data->ConvertToToken(whitespace);
                if (!token.has_value()) {
                    return std::nullopt;
                }
                tokens.push_back(*token);
            }

            for (size_t i = 1; i < terms.size(); i++) {
                const CalcTermWithOp& term = terms[i];

                // If child is a Negate node, append " - " to s, then serialize the Negate's
                // child and append the result to s.
                if (auto negate = std::dynamic_pointer_cast<CalcNegate>(term.data); negate) {
                    auto token = negate->term.data->ConvertToToken(whitespace);
                    if (!token.has_value()) {
                        return std::nullopt;
                    }
                    Token minus;
                    minus.loc = term.op_loc;
                    minus.kind = TokenType::kDelimMinus;
                    minus.text = "-";
                    minus.whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
                    tokens.push_back(minus);
                    tokens.push_back(*token);
                    continue;
                }

                // If child is a negative numeric value, append " - " to s, then serialize
                // the negation of child as normal and append the result to s.
                if (auto numeric = std::dynamic_pointer_cast<CalcNumeric>(term.data); numeric) {
                    if (numeric->number < 0) {
                        CalcNumeric clone = *numeric;
                        clone.number = -clone.number;
                        auto token = clone.ConvertToToken(whitespace);
                        if (!token.has_value()) {
                            return std::nullopt;
                        }
                        Token minus;
                        minus.loc = term.op_loc;
                        minus.kind = TokenType::kDelimMinus;
                        minus.text = "-";
                        minus.whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
                        tokens.push_back(minus);
                        tokens.push_back(*token);
                        continue;
                    }
                }

                // Otherwise, append " + " to s, then serialize child and append the result to s.
                Token plus;
                plus.loc = term.op_loc;
                plus.kind = TokenType::kDelimPlus;
                plus.text = "+";
                plus.whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
                tokens.push_back(plus);

                // ALGORITHM DEVIATION: Avoid parenthesizing product nodes inside sum nodes
                if (auto product = std::dynamic_pointer_cast<CalcProduct>(term.data); product) {
                    auto token = product->ConvertToToken(whitespace);
                    if (!token.has_value()) {
                        return std::nullopt;
                    }
                    for (const Token& child : *token->children) {
                        tokens.push_back(child);
                    }
                } else {
                    auto token = term.data->ConvertToToken(whitespace);
                    if (!token.has_value()) {
                        return std::nullopt;
                    }
                    tokens.push_back(*token);
                }
            }

            Token result;
            result.loc = tokens[0].loc;
            result.kind = TokenType::kOpenParen;
            result.text = "(";
            result.children = std::make_shared<std::vector<Token>>(std::move(tokens));
            return result;
        }

        // Serializes a CalcProduct node into a parenthesized token list.
        //
        // The serialization follows the CSS Values 4 specification:
        //   - If a child is an Invert node, use "/" operator
        //   - Otherwise, use "*" operator
        //
        // Input:  CalcProduct with terms [2px, *, 3]
        // Output: Token "(" with children [2px, *, 3]
        //
        // Input:  CalcProduct with terms [2px, /, 3]
        // Output: Token "(" with children [2px, /, CalcInvert(3)]
        //
        // Edge cases:
        //   - Returns std::nullopt if any child cannot be serialized.
        std::optional<Token> CalcProduct::ConvertToToken(WhitespaceFlags whitespace) {
            // Specification: https://www.w3.org/TR/css-values-4/#calc-serialize
            std::vector<Token> tokens;
            tokens.reserve(terms.size() * 2);
            auto first = terms[0].data->ConvertToToken(whitespace);
            if (!first.has_value()) {
                return std::nullopt;
            }
            tokens.push_back(*first);

            for (size_t i = 1; i < terms.size(); i++) {
                const CalcTermWithOp& term = terms[i];

                // If child is an Invert node, append " / " to s, then serialize the Invert's
                // child and append the result to s.
                if (auto invert = std::dynamic_pointer_cast<CalcInvert>(term.data); invert) {
                    auto token = invert->term.data->ConvertToToken(whitespace);
                    if (!token.has_value()) {
                        return std::nullopt;
                    }
                    Token slash;
                    slash.loc = term.op_loc;
                    slash.kind = TokenType::kDelimSlash;
                    slash.text = "/";
                    slash.whitespace = whitespace;
                    tokens.push_back(slash);
                    tokens.push_back(*token);
                    continue;
                }

                // Otherwise, append " * " to s, then serialize child and append the result to s.
                auto token = term.data->ConvertToToken(whitespace);
                if (!token.has_value()) {
                    return std::nullopt;
                }
                Token star;
                star.loc = term.op_loc;
                star.kind = TokenType::kDelimAsterisk;
                star.text = "*";
                star.whitespace = whitespace;
                tokens.push_back(star);
                tokens.push_back(*token);
            }

            Token result;
            result.loc = tokens[0].loc;
            result.kind = TokenType::kOpenParen;
            result.text = "(";
            result.children = std::make_shared<std::vector<Token>>(std::move(tokens));
            return result;
        }

        // Serializes a CalcNegate node into a parenthesized expression.
        // The output is "(-1 * operand)".
        //
        // Input:  CalcNegate wrapping 2px
        // Output: Token "(" with children [-1, *, 2px]
        //
        // Edge cases:
        //   - Returns std::nullopt if the child cannot be serialized.
    std::optional<Token> CalcNegate::ConvertToToken(WhitespaceFlags whitespace) {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-serialize
        auto token = term.data->ConvertToToken(whitespace);
        if (!token.has_value()) {
            return std::nullopt;
        }
        Token result;
        result.kind = TokenType::kOpenParen;
        result.text = "(";
        std::vector<Token> children;
        children.push_back(Token{});
        children.back().loc = term.op_loc;
        children.back().kind = TokenType::kNumber;
        children.back().text = "-1";
        children.push_back(Token{});
        children.back().loc = term.op_loc;
        children.back().kind = TokenType::kDelimSlash;
        children.back().text = "*";
        children.back().whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
        children.push_back(*token);
        result.children = std::make_shared<std::vector<Token>>(std::move(children));
        return result;
    }

        // Serializes a CalcInvert node into a parenthesized expression.
        // The output is "(1 / operand)".
        //
        // Input:  CalcInvert wrapping 2
        // Output: Token "(" with children [1, /, 2]
        //
        // Edge cases:
        //   - Returns std::nullopt if the child cannot be serialized.
    std::optional<Token> CalcInvert::ConvertToToken(WhitespaceFlags whitespace) {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-serialize
        auto token = term.data->ConvertToToken(whitespace);
        if (!token.has_value()) {
            return std::nullopt;
        }
        Token result;
        result.kind = TokenType::kOpenParen;
        result.text = "(";
        std::vector<Token> children;
        children.push_back(Token{});
        children.back().loc = term.op_loc;
        children.back().kind = TokenType::kNumber;
        children.back().text = "1";
        children.push_back(Token{});
        children.back().loc = term.op_loc;
        children.back().kind = TokenType::kDelimSlash;
        children.back().text = "/";
        children.back().whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
        children.push_back(*token);
        result.children = std::make_shared<std::vector<Token>>(std::move(children));
        return result;
    }

        // Serializes a CalcNumeric node into a token. The token type depends
        // on the unit:
        //   - Empty unit: TokenType::kNumber
        //   - "%" unit: TokenType::kPercentage
        //   - Other units: TokenType::kDimension
        //
        // Input:  CalcNumeric{number=10, unit="px"}
        // Output: Token{kind=kDimension, text="10px"}
        //
        // Input:  CalcNumeric{number=50, unit="%"}
        // Output: Token{kind=kPercentage, text="50%"}
        //
        // Input:  CalcNumeric{number=3.14, unit=""}
        // Output: Token{kind=kNumber, text="3.14"}
        //
        // Edge cases:
        //   - Returns std::nullopt if the number cannot be exactly represented
        //     as a string (see FloatToStringForCalc).
    std::optional<Token> CalcNumeric::ConvertToToken(WhitespaceFlags whitespace) {
        (void)whitespace;
        auto text = FloatToStringForCalc(number);
        if (!text.has_value()) {
            return std::nullopt;
        }
        if (unit.empty()) {
            Token result;
            result.loc = loc;
            result.kind = TokenType::kNumber;
            result.text = *text;
            return result;
        }
        if (unit == "%") {
            Token result;
            result.loc = loc;
            result.kind = TokenType::kPercentage;
            result.text = *text + "%";
            return result;
        }
        Token result;
        result.loc = loc;
        result.kind = TokenType::kDimension;
        result.text = *text + unit;
        result.unit_offset = static_cast<uint16_t>(text->size());
        return result;
    }

        // Serializes a CalcValue node by returning its stored token with
        // whitespace stripped. This preserves the original token representation
        // for values that cannot be simplified (e.g., CSS variables).
        //
        // Input:  CalcValue with token "var(--x)"
        // Output: Token "var(--x)" with no whitespace flags
        //
        // Edge cases:
        //   - Always succeeds (returns the stored token).
    std::optional<Token> CalcValue::ConvertToToken(WhitespaceFlags whitespace) {
        (void)whitespace;
        Token result = token;
        result.whitespace = WhitespaceFlags::kNone;
        return result;
    }

        // Partially simplifies a CalcSum node according to the CSS Values 4
        // specification. The simplification process is:
        //
        //   1. Flatten nested Sum nodes (distribute addition).
        //   2. Combine adjacent numeric values with identical units.
        //   3. If only one term remains, return that term directly.
        //
        // This method does NOT fully simplify expressions that cross unit
        // boundaries (e.g., "1px + 2em" remains as-is).
        //
        // Input:  CalcSum with terms [1px, +, 2px]
        // Output: CalcNumeric{number=3, unit="px"}
        //
        // Input:  CalcSum with terms [1px, +, 2em]
        // Output: CalcSum (unchanged, different units)
        //
        // Input:  CalcSum with terms [CalcSum(1px, +, 2px), +, 3px]
        // Output: CalcNumeric{number=6, unit="px"} (flattened and combined)
        //
        // Edge cases:
        //   - Empty terms list returns the CalcSum unchanged.
        //   - Terms with different unit casing are combined (case-insensitive).
    std::shared_ptr<CalcTerm> CalcSum::PartiallySimplify() {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-simplification

        // For each of root's children that are Sum nodes, replace them with their children.
        std::vector<CalcTermWithOp> terms_out;
        terms_out.reserve(terms.size());
        for (CalcTermWithOp& term : terms) {
            term.data = term.data->PartiallySimplify();
            if (auto sum = std::dynamic_pointer_cast<CalcSum>(term.data); sum) {
                for (CalcTermWithOp& t : sum->terms) {
                    terms_out.push_back(t);
                }
            } else {
                terms_out.push_back(term);
            }
        }

        // For each set of root's children that are numeric values with identical units, remove
        // those children and replace them with a single numeric value containing the sum of the
        // removed nodes, and with the same unit. (E.g. combine numbers, combine percentages,
        // combine px values, etc.)
        for (size_t i = 0; i < terms_out.size(); i++) {
            CalcTermWithOp& term = terms_out[i];
            if (auto numeric = std::dynamic_pointer_cast<CalcNumeric>(term.data); numeric) {
                size_t end = i + 1;
                for (size_t j = end; j < terms_out.size(); j++) {
                    CalcTermWithOp& term2 = terms_out[j];
                    if (auto numeric2 = std::dynamic_pointer_cast<CalcNumeric>(term2.data); numeric2) {
                        if (helpers::EqualFoldASCII(numeric2->unit, numeric->unit)) {
                            numeric->number += numeric2->number;
                        } else {
                            terms_out[end++] = term2;
                        }
                    } else {
                        terms_out[end++] = term2;
                    }
                }
                terms_out.resize(end);
            }
        }

        // If root has only a single child at this point, return the child.
        if (terms_out.size() == 1) {
            return terms_out[0].data;
        }

        // Otherwise, return root.
        terms = std::move(terms_out);
        return shared_from_this();
    }

        // Partially simplifies a CalcProduct node according to the CSS Values 4
        // specification. The simplification process is:
        //
        //   1. Flatten nested Product nodes (distribute multiplication).
        //   2. Combine adjacent unitless numeric values by multiplication.
        //   3. If one factor is unitless and the other has a unit, multiply
        //      the unitless factor into the other.
        //   4. Convert multiplication to division if the reciprocal is shorter.
        //   5. If only one term remains, return that term directly.
        //
        // Input:  CalcProduct with terms [2, *, 3px]
        // Output: CalcNumeric{number=6, unit="px"}
        //
        // Input:  CalcProduct with terms [2px, *, 3]
        // Output: CalcNumeric{number=6, unit="px"}
        //
        // Input:  CalcProduct with terms [1, /, 2]
        // Output: CalcNumeric{number=0.5, unit=""} (simplified to ".5")
        //
        // Edge cases:
        //   - Division by zero is not simplified (Infinity handling).
        //   - Only unitless numbers are combined; "2px * 3em" stays as product.
    std::shared_ptr<CalcTerm> CalcProduct::PartiallySimplify() {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-simplification

        // For each of root's children that are Product nodes, replace them with their children.
        std::vector<CalcTermWithOp> terms_out;
        terms_out.reserve(terms.size());
        for (CalcTermWithOp& term : terms) {
            term.data = term.data->PartiallySimplify();
            if (auto product = std::dynamic_pointer_cast<CalcProduct>(term.data); product) {
                for (CalcTermWithOp& t : product->terms) {
                    terms_out.push_back(t);
                }
            } else {
                terms_out.push_back(term);
            }
        }

        // If root has multiple children that are numbers (not percentages or dimensions), remove
        // them and replace them with a single number containing the product of the removed nodes.
        for (size_t i = 0; i < terms_out.size(); i++) {
            CalcTermWithOp& term = terms_out[i];
            if (auto numeric = std::dynamic_pointer_cast<CalcNumeric>(term.data); numeric) {
                if (numeric->unit.empty()) {
                    size_t end = i + 1;
                    for (size_t j = end; j < terms_out.size(); j++) {
                        CalcTermWithOp& term2 = terms_out[j];
                        if (auto numeric2 = std::dynamic_pointer_cast<CalcNumeric>(term2.data); numeric2) {
                            if (numeric2->unit.empty()) {
                                numeric->number *= numeric2->number;
                            } else {
                                terms_out[end++] = term2;
                            }
                        } else {
                            terms_out[end++] = term2;
                        }
                    }
                    terms_out.resize(end);
                    break;
                }
            }
        }

        // If root contains only numeric values and/or Invert nodes containing numeric values,
        // and multiplying the types of all the children (noting that the type of an Invert
        // node is the inverse of its child's type) results in a type that matches any of the
        // types that a math function can resolve to, return the result of multiplying all the
        // values of the children (noting that the value of an Invert node is the reciprocal
        // of its child's value), expressed in the result's canonical unit.
        if (terms_out.size() == 2) {
            // Right now, only handle the case of two numbers, one of which has no unit
            if (auto first = std::dynamic_pointer_cast<CalcNumeric>(terms_out[0].data); first) {
                if (auto second = std::dynamic_pointer_cast<CalcNumeric>(terms_out[1].data); second) {
                    if (first->unit.empty()) {
                        second->number *= first->number;
                        return second;
                    }
                    if (second->unit.empty()) {
                        first->number *= second->number;
                        return first;
                    }
                }
            }
        }

        // ALGORITHM DEVIATION: Divide instead of multiply if the reciprocal is shorter
        for (size_t i = 1; i < terms_out.size(); i++) {
            CalcTermWithOp& term = terms_out[i];
            if (auto numeric = std::dynamic_pointer_cast<CalcNumeric>(term.data); numeric) {
                double reciprocal = 1 / numeric->number;
                auto multiply = FloatToStringForCalc(numeric->number);
                auto divide = FloatToStringForCalc(reciprocal);
                if (multiply.has_value() && divide.has_value() && divide->size() < multiply->size()) {
                    numeric->number = reciprocal;
                    auto invert = std::make_shared<CalcInvert>();
                    invert->term.data = numeric;
                    invert->term.op_loc = term.op_loc;
                    term.data = invert;
                }
            }
        }

        // If root has only a single child at this point, return the child.
        if (terms_out.size() == 1) {
            return terms_out[0].data;
        }

        // Otherwise, return root.
        terms = std::move(terms_out);
        return shared_from_this();
    }

        // Partially simplifies a CalcNegate node according to the CSS Values 4
        // specification. The simplification rules are:
        //
        //   1. If the child is a numeric value, negate it and return directly.
        //   2. If the child is a Negate node, return the grandchild (double negation).
        //   3. Otherwise, return unchanged.
        //
        // Input:  CalcNegate wrapping CalcNumeric{number=5}
        // Output: CalcNumeric{number=-5}
        //
        // Input:  CalcNegate wrapping CalcNegate(calcValue)
        // Output: calcValue
        //
        // Input:  CalcNegate wrapping var(--x)
        // Output: CalcNegate (unchanged)
        //
        // Edge cases:
        //   - Negating zero produces negative zero (-0).
        //   - Negating NaN produces NaN.
    std::shared_ptr<CalcTerm> CalcNegate::PartiallySimplify() {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-simplification

        term.data = term.data->PartiallySimplify();

        // If root's child is a numeric value, return an equivalent numeric value, but with the
        // value negated (0 - value).
        if (auto numeric = std::dynamic_pointer_cast<CalcNumeric>(term.data); numeric) {
            numeric->number = -numeric->number;
            return numeric;
        }

        // If root's child is a Negate node, return the child's child.
        if (auto negate = std::dynamic_pointer_cast<CalcNegate>(term.data); negate) {
            return negate->term.data;
        }

        return shared_from_this();
    }

        // Partially simplifies a CalcInvert node according to the CSS Values 4
        // specification. The simplification rules are:
        //
        //   1. If the child is a unitless number, return its reciprocal.
        //   2. If the child is an Invert node, return the grandchild (double inversion).
        //   3. Otherwise, return unchanged.
        //
        // Input:  CalcInvert wrapping CalcNumeric{number=4, unit=""}
        // Output: CalcNumeric{number=0.25, unit=""}
        //
        // Input:  CalcInvert wrapping CalcInvert(calcValue)
        // Output: calcValue
        //
        // Input:  CalcInvert wrapping CalcNumeric{number=4, unit="px"}
        // Output: CalcInvert (unchanged, cannot invert dimensions)
        //
        // Edge cases:
        //   - Inverting zero produces Infinity.
        //   - Inverting Infinity produces zero.
        //   - Only unitless numbers can be inverted; dimensions and percentages are not.
    std::shared_ptr<CalcTerm> CalcInvert::PartiallySimplify() {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-simplification

        term.data = term.data->PartiallySimplify();

        // If root's child is a number (not a percentage or dimension) return the reciprocal of
        // the child's value.
        if (auto numeric = std::dynamic_pointer_cast<CalcNumeric>(term.data); numeric) {
            if (numeric->unit.empty()) {
                numeric->number = 1 / numeric->number;
                return numeric;
            }
        }

        // If root's child is an Invert node, return the child's child.
        if (auto invert = std::dynamic_pointer_cast<CalcInvert>(term.data); invert) {
            return invert->term.data;
        }

        return shared_from_this();
    }

        // Returns the CalcNumeric node unchanged. Numeric values are already
        // in their simplest form.
    std::shared_ptr<CalcTerm> CalcNumeric::PartiallySimplify() {
        return shared_from_this();
    }

        // Returns the CalcValue node unchanged. Non-numeric values cannot
        // be simplified further.
    std::shared_ptr<CalcTerm> CalcValue::PartiallySimplify() {
        return shared_from_this();
    }

        // Parses a flat list of tokens into a CalcTerm tree. This is the main
        // parsing function that implements the CSS Values 4 calc() grammar.
        //
        // The parsing process:
        //   1. Convert each token into a CalcTerm (numeric, value, or nested calc).
        //   2. Collect runs of * and / operators into CalcProduct nodes.
        //   3. Collect runs of + and - operators into CalcSum nodes.
        //   4. Return the single remaining term, or nullptr on failure.
        //
        // Input:  tokens = [1px, +, 2px]
        // Output: CalcSum with terms [CalcNumeric(1px), CalcNumeric(2px)]
        //
        // Input:  tokens = [2, *, 3px]
        // Output: CalcProduct with terms [CalcNumeric(2), CalcNumeric(3px)]
        //
        // Input:  tokens = [1px, +, var(--x)]
        // Output: CalcSum with terms [CalcNumeric(1px), CalcValue(var(--x))]
        //
        // Edge cases:
        //   - Returns nullptr if var() is encountered (can expand to anything).
        //   - Returns nullptr if the token list doesn't reduce to a single term.
        //   - +/- operators without surrounding whitespace are treated as values,
        //     not operators (CSS spec requirement).
        //   - Nested calc() and parenthesized expressions are recursively parsed.
        //   - Special identifiers Infinity, -Infinity, and NaN are parsed as
        //     numeric values with infinite/NaN values.
    std::shared_ptr<CalcTerm> TryToParseCalcTerm(const std::vector<Token>& tokens) {
        // Specification: https://www.w3.org/TR/css-values-4/#calc-internal
        std::vector<CalcTermWithOp> terms;
        terms.resize(tokens.size());

        for (size_t i = 0; i < tokens.size(); i++) {
            const Token& token = tokens[i];
            std::shared_ptr<CalcTerm> term;

            if (token.kind == TokenType::kFunction && helpers::EqualFoldASCII(token.text, "var")) {
                // Using "var()" should bail because it can expand to any number of tokens
                return nullptr;
            } else if (token.kind == TokenType::kOpenParen ||
                    (token.kind == TokenType::kFunction &&  helpers::EqualFoldASCII(token.text, "calc"))) {
                if (!token.children) {
                    return nullptr;
                }
                term = TryToParseCalcTerm(*token.children);
                if (!term) {
                    return nullptr;
                }
            } else if (token.kind == TokenType::kNumber) {
                if (auto number = ParseFloat(token.text); number.has_value()) {
                    auto numeric = std::make_shared<CalcNumeric>();
                    numeric->loc = token.loc;
                    numeric->number = *number;
                    term = numeric;
                } else {
                    auto value = std::make_shared<CalcValue>();
                    value->token = token;
                    term = value;
                }
            } else if (token.kind == TokenType::kPercentage) {
                if (auto number = ParseFloat(token.PercentageValue()); number.has_value()) {
                    auto numeric = std::make_shared<CalcNumeric>();
                    numeric->loc = token.loc;
                    numeric->number = *number;
                    numeric->unit = "%";
                    term = numeric;
                } else {
                    auto value = std::make_shared<CalcValue>();
                    value->token = token;
                    term = value;
                }
            } else if (token.kind == TokenType::kDimension) {
                if (auto number = ParseFloat(token.DimensionValue()); number.has_value()) {
                    auto numeric = std::make_shared<CalcNumeric>();
                    numeric->loc = token.loc;
                    numeric->number = *number;
                    numeric->unit = token.DimensionUnit();
                    term = numeric;
                } else {
                    auto value = std::make_shared<CalcValue>();
                    value->token = token;
                    term = value;
                }
            } else if (token.kind == TokenType::kIdent &&  helpers::EqualFoldASCII(token.text, "Infinity")) {
                auto numeric = std::make_shared<CalcNumeric>();
                numeric->loc = token.loc;
                numeric->number = std::numeric_limits<double>::infinity();
                term = numeric;
            } else if (token.kind == TokenType::kIdent &&  helpers::EqualFoldASCII(token.text, "-Infinity")) {
                auto numeric = std::make_shared<CalcNumeric>();
                numeric->loc = token.loc;
                numeric->number = -std::numeric_limits<double>::infinity();
                term = numeric;
            } else if (token.kind == TokenType::kIdent &&  helpers::EqualFoldASCII(token.text, "NaN")) {
                auto numeric = std::make_shared<CalcNumeric>();
                numeric->loc = token.loc;
                numeric->number = std::numeric_limits<double>::quiet_NaN();
                term = numeric;
            } else {
                auto value = std::make_shared<CalcValue>();
                value->token = token;

                // From the specification: "In addition, whitespace is required on both sides of the
                // + and - operators. (The * and / operators can be used without white space around them.)"
                value->is_invalid_plus_or_minus =
                    i > 0 && i + 1 < tokens.size() &&
                    (token.kind == TokenType::kDelimPlus || token.kind == TokenType::kDelimMinus) &&
                    (((token.whitespace & WhitespaceFlags::kWhitespaceBefore) == WhitespaceFlags::kNone &&
                    (tokens[i - 1].whitespace & WhitespaceFlags::kWhitespaceAfter) == WhitespaceFlags::kNone) ||
                    ((token.whitespace & WhitespaceFlags::kWhitespaceAfter) == WhitespaceFlags::kNone &&
                    (tokens[i + 1].whitespace & WhitespaceFlags::kWhitespaceBefore) == WhitespaceFlags::kNone));
                term = value;
            }

            terms[i].data = term;
        }

        // Collect children into Product and Invert nodes
        size_t first = 1;
        while (first + 1 < terms.size()) {
            // If this is a "*" or "/" operator
            if (auto value = std::dynamic_pointer_cast<CalcValue>(terms[first].data); value) {
                if (value->token.kind == TokenType::kDelimAsterisk || value->token.kind == TokenType::kDelimSlash) {
                    // Scan over the run
                    size_t last = first;
                    while (last + 3 < terms.size()) {
                        if (auto v = std::dynamic_pointer_cast<CalcValue>(terms[last + 2].data); v) {
                            if (v->token.kind == TokenType::kDelimAsterisk || v->token.kind == TokenType::kDelimSlash) {
                                last += 2;
                            } else {
                                break;
                            }
                        } else {
                            break;
                        }
                    }

                    // Generate a node for the run
                    auto product = std::make_shared<CalcProduct>();
                    product->terms.resize((last - first) / 2 + 2);
                    for (size_t i = 0; i < product->terms.size(); i++) {
                        CalcTermWithOp term = terms[first + i * 2 - 1];
                        if (i > 0) {
                            const Token& op =
                                std::dynamic_pointer_cast<CalcValue>(terms[first + i * 2 - 2].data)->token;
                            term.op_loc = op.loc;
                            if (op.kind == TokenType::kDelimSlash) {
                                auto invert = std::make_shared<CalcInvert>();
                                invert->term = term;
                                term.data = invert;
                            }
                        }
                        product->terms[i] = term;
                    }

                    // Replace the run with a single node
                    terms[first - 1].data = product;
                    terms.erase(terms.begin() + static_cast<ptrdiff_t>(first),
                                terms.begin() + static_cast<ptrdiff_t>(last + 2));
                    continue;
                }
            }

            first++;
        }

        // Collect children into Sum and Negate nodes
        first = 1;
        while (first + 1 < terms.size()) {
            // If this is a "+" or "-" operator
            if (auto value = std::dynamic_pointer_cast<CalcValue>(terms[first].data); value) {
                if (!value->is_invalid_plus_or_minus &&
                    (value->token.kind == TokenType::kDelimPlus || value->token.kind == TokenType::kDelimMinus)) {
                    // Scan over the run
                    size_t last = first;
                    while (last + 3 < terms.size()) {
                        if (auto v = std::dynamic_pointer_cast<CalcValue>(terms[last + 2].data); v) {
                            if (!v->is_invalid_plus_or_minus &&
                                (v->token.kind == TokenType::kDelimPlus || v->token.kind == TokenType::kDelimMinus)) {
                                last += 2;
                            } else {
                                break;
                            }
                        } else {
                            break;
                        }
                    }

                    // Generate a node for the run
                    auto sum = std::make_shared<CalcSum>();
                    sum->terms.resize((last - first) / 2 + 2);
                    for (size_t i = 0; i < sum->terms.size(); i++) {
                        CalcTermWithOp term = terms[first + i * 2 - 1];
                        if (i > 0) {
                            const Token& op =
                                std::dynamic_pointer_cast<CalcValue>(terms[first + i * 2 - 2].data)->token;
                            term.op_loc = op.loc;
                            if (op.kind == TokenType::kDelimMinus) {
                                auto negate = std::make_shared<CalcNegate>();
                                negate->term = term;
                                term.data = negate;
                            }
                        }
                        sum->terms[i] = term;
                    }

                    // Replace the run with a single node
                    terms[first - 1].data = sum;
                    terms.erase(terms.begin() + static_cast<ptrdiff_t>(first),
                                terms.begin() + static_cast<ptrdiff_t>(last + 2));
                    continue;
                }
            }

            first++;
        }

        // This only succeeds if everything reduces to a single term
        if (terms.size() == 1) {
            return terms[0].data;
        }
        return nullptr;
    }

    }

    // Attempts to simplify a CSS calc() expression token. This is the main
    // entry point for calc() optimization. It parses the expression into an
    // AST, partially simplifies it, and serializes it back to a token.
    //
    // The simplification process:
    //   1. Parse the token's children into a CalcTerm tree.
    //   2. Call PartiallySimplify() to combine like terms.
    //   3. Serialize the simplified tree back to a token.
    //   4. If serialization fails, return the original token unchanged.
    //
    // Input:  token = calc(1px + 2px)
    // Output: token = calc(3px)
    //
    // Input:  token = calc(100% - 20px)
    // Output: token = calc(100% - 20px) (cannot simplify across units)
    //
    // Input:  token = calc(2 * 3px)
    // Output: token = calc(6px)
    //
    // Input:  token = calc(var(--x) + 1px)
    // Output: token = calc(var(--x) + 1px) (var() prevents simplification)
    //
    // Edge cases:
    //   - If parsing fails, the original token is returned unchanged.
    //   - If serialization fails, the original token is returned unchanged.
    //   - The simplified expression is wrapped in calc() if it becomes
    //     a parenthesized expression.
    //   - Whitespace flags are preserved based on minify_whitespace.
Token TryToReduceCalcExpression(Token token, bool minify_whitespace) {
    if (!token.children) {
        return token;
    }

    if (auto term = TryToParseCalcTerm(*token.children); term) {
        WhitespaceFlags whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
        if (minify_whitespace) {
            whitespace = WhitespaceFlags::kNone;
        }

        term = term->PartiallySimplify();
        if (auto result = term->ConvertToToken(whitespace); result.has_value()) {
            if (result->kind == TokenType::kOpenParen) {
                result->kind = TokenType::kFunction;
                result->text = "calc";
            }
            result->loc = token.loc;
            result->whitespace = WhitespaceFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter;
            return *result;
        }
    }

    return token;
}

}
