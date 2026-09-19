#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_parser.hpp"


namespace guchho::javascript {

    namespace {

        // JSONParser: A recursive-descent parser that consumes a JSON token
        // stream produced by Lexer::NewJSON and builds a Guchho AST (Expr tree).
        // It supports strict JSON as well as relaxed flavors (JSONC, JSON5,
        // JSON-TF) controlled by JSONOptions.  The parser handles objects,
        // arrays, strings, numbers, booleans, null, negative numeric literals,
        // and (in define contexts only) bigint literals.
        //
        // Syntax errors cause the lexer to emit a LexerPanic, which unwinds
        // back to the caller.  The parser itself never throws.
        class JSONParser {
        public:
            JSONParser(logger::Log log, logger::Source source, JSONOptions options)
                : log_(log),
                source_(source),
                tracker_(&source_),
                options_(options),
                lexer_(Lexer::NewJSON(log, source, options.flavor, options.error_suffix)),
                suppress_warnings_about_weird_code_(helpers::IsInsideNodeModules(source_.key_path.text)) {}

            // ParseMaybeTrailingComma: Consumes an expected comma token between
            // elements of an array or properties of an object.  If the token
            // immediately after the comma is the close bracket/brace, this is a
            // trailing comma.
            //
            // In strict JSON mode (JSONFlavor::kJSON) a trailing comma is an
            // error and is reported via the logger.  In relaxed flavors (JSONC,
            // JSON5, JSON-TF) the trailing comma is silently accepted.
            //
            // Returns false when a trailing comma followed by the close token was
            // found, signaling the caller to stop parsing further elements.
            //
            // Examples:
            //   [1, 2, 3]     => comma consumed, returns true  (next element expected)
            //   [1, 2, 3,]    => trailing comma in JSON  => error, returns false
            //   [1, 2, 3,]    => trailing comma in JSONC => accepted,  returns false
            //   {"a":1,"b":2}  => comma consumed, returns true
            bool ParseMaybeTrailingComma(T close_token) {
                logger::Range comma_range = lexer_.Range();
                lexer_.Expect(T::kComma);

                if (lexer_.token == close_token) {
                    if (options_.flavor == JSONFlavor::kJSON) {
                        log_.AddError(&tracker_, comma_range, logger::FormatMsg(logger::MsgCat::kJSON_NoTrailingCommas));
                    }
                    return false;
                }

                return true;
            }

            // ParseExpr: Parses a single JSON value expression and returns an
            // AST node (Expr).  This is the core recursive-descent routine —
            // arrays and objects call back into it for each child element.
            //
            // Supported value types and their AST representations:
            //   false / true    => EBoolean
            //   null            => ENull
            //   string literal  => EString
            //   numeric literal => ENumber
            //   - numeric literal=> ENumber (negated)
            //   [...]           => EArray  (children parsed recursively)
            //   {...}           => EObject (key-value properties parsed recursively)
            //   bigint literal  => EBigInt (only permitted in define contexts)
            //
            // Both EArray and EObject record whether they were written on a
            // single line (no newlines between tokens), which the printer uses
            // to decide output formatting.
            //
            // Edge cases:
            //   - An empty array [] or object {} is valid.
            //   - Trailing commas are handled by ParseMaybeTrailingComma and
            //     are errors in strict JSON but allowed in relaxed flavors.
            //   - Negative zero (-0) is parsed as ENumber{-0.0}.
            //   - Bigint literals outside of define contexts trigger an
            //     unexpected-token error.
            //
            // Example:
            //   Input tokens:  { "count" : 3 }
            //   Output:  EObject {
            //     properties: [ Property{ key: EString{"count"},
            //                               value: ENumber{3.0} } ]
            //   }
            javascript::Expr ParseExpr() {
                logger::Loc loc = lexer_.Loc();

                switch (lexer_.token) {
                case T::kFalse:
                    lexer_.Next();
                    return javascript::Expr{std::make_shared<EBoolean>(EBoolean{false}), loc};

                case T::kTrue:
                    lexer_.Next();
                    return javascript::Expr{std::make_shared<EBoolean>(EBoolean{true}), loc};

                case T::kNull:
                    lexer_.Next();
                    return javascript::Expr{kENullShared, loc};

                case T::kStringLiteral: {
                    std::u16string value = lexer_.StringLiteral();
                    lexer_.Next();
                    return javascript::Expr{std::make_shared<EString>(EString{std::move(value), {}}), loc};
                }

                case T::kNumericLiteral: {
                    double value = lexer_.number;
                    lexer_.Next();
                    return javascript::Expr{std::make_shared<ENumber>(ENumber{value}), loc};
                }

                case T::kMinus: {
                    lexer_.Next();
                    double value = lexer_.number;
                    lexer_.Expect(T::kNumericLiteral);
                    return javascript::Expr{std::make_shared<ENumber>(ENumber{-value}), loc};
                }

                case T::kOpenBracket: {
                    lexer_.Next();
                    bool is_single_line = !lexer_.has_newline_before;
                    std::vector<javascript::Expr> items;

                    while (lexer_.token != T::kCloseBracket) {
                        if (!items.empty()) {
                            if (lexer_.has_newline_before) {
                                is_single_line = false;
                            }
                            if (!ParseMaybeTrailingComma(T::kCloseBracket)) {
                                break;
                            }
                            if (lexer_.has_newline_before) {
                                is_single_line = false;
                            }
                        }

                        javascript::Expr item = ParseExpr();
                        items.push_back(std::move(item));
                    }

                    if (lexer_.has_newline_before) {
                        is_single_line = false;
                    }
                    logger::Loc close_bracket_loc = lexer_.Loc();
                    lexer_.Expect(T::kCloseBracket);
                    auto array = std::make_shared<EArray>();
                    array->items = std::move(items);
                    array->is_single_line = is_single_line;
                    array->close_bracket_loc = close_bracket_loc;
                    return javascript::Expr{array, loc};
                }

                case T::kOpenBrace: {
                    lexer_.Next();
                    bool is_single_line = !lexer_.has_newline_before;
                    std::vector<javascript::Property> properties;
                    std::unordered_map<std::string, logger::Range> duplicates;

                    while (lexer_.token != T::kCloseBrace) {
                        if (!properties.empty()) {
                            if (lexer_.has_newline_before) {
                                is_single_line = false;
                            }
                            if (!ParseMaybeTrailingComma(T::kCloseBrace)) {
                                break;
                            }
                            if (lexer_.has_newline_before) {
                                is_single_line = false;
                            }
                        }

                        std::u16string key_string = lexer_.StringLiteral();
                        logger::Range key_range = lexer_.Range();
                        javascript::Expr key{std::make_shared<EString>(EString{key_string, {}}), key_range.loc};
                        lexer_.Expect(T::kStringLiteral);

                        // Detect duplicate keys within the same object literal.
                        // Duplicate keys are technically valid JSON (the last
                        // value wins), but they are almost always a mistake.
                        // When a duplicate is found, a warning is emitted with
                        // a note pointing at the first occurrence.  This check
                        // is suppressed for files inside node_modules to avoid
                        // noise from third-party code.
                        if (!suppress_warnings_about_weird_code_) {
                            std::string key_text = helpers::UTF16ToString(key_string);
                            auto it = duplicates.find(key_text);
                            if (it != duplicates.end()) {
                                logger::Range prev_range = it->second;
                                logger::MsgData note = tracker_.MakeMsgData(
                                    prev_range, logger::FormatMsg(logger::MsgCat::kJSON_DuplicateKeyOriginalNote, helpers::quoteString(key_text)));
                                log_.AddIDWithNotes(
                                    logger::MsgID::kJS_DuplicateObjectKey, logger::MsgKind::kWarning, &tracker_, key_range,
                                    logger::FormatMsg(logger::MsgCat::kJSON_DuplicateKey, helpers::quoteString(key_text)),
                                    {std::move(note)});
                            } else {
                                duplicates[key_text] = key_range;
                            }
                        }

                        lexer_.Expect(T::kColon);
                        javascript::Expr value = ParseExpr();

                        javascript::Property property;
                        property.kind = javascript::PropertyKind::kField;
                        property.loc = key_range.loc;
                        property.key = key;
                        property.value_or_nil = std::move(value);

                        // When the key is "__proto__" and the target environment
                        // does not support object extension features, mark the
                        // property as computed.  A literal "__proto__" key in
                        // JavaScript modifies the object's prototype chain,
                        // which would cause subtle runtime behavior differences
                        // compared to the intended JSON semantics.  Using a
                        // computed property key ([ "__proto__" ]) avoids that
                        // side-effect and produces the expected plain property.
                        if (helpers::UTF16EqualsString(key_string, "__proto__") &&
                            !compat::Has(options_.unsupported_js_features, compat::JSFeature::kObjectExtensions)) {
                            property.flags = javascript::PropertyFlags::kIsComputed;
                        }

                        properties.push_back(std::move(property));
                    }

                    if (lexer_.has_newline_before) {
                        is_single_line = false;
                    }
                    logger::Loc close_brace_loc = lexer_.Loc();
                    lexer_.Expect(T::kCloseBrace);
                    auto object = std::make_shared<EObject>();
                    object->properties = std::move(properties);
                    object->is_single_line = is_single_line;
                    object->close_brace_loc = close_brace_loc;
                    return javascript::Expr{object, loc};
                }

                case T::kBigIntegerLiteral:
                    if (!options_.is_for_define) {
                        lexer_.Unexpected();
                    }
                    {
                        std::string value = lexer_.identifier.str;
                        lexer_.Next();
                        auto bigint = std::make_shared<EBigInt>();
                        bigint->value = std::move(value);
                        return javascript::Expr{bigint, loc};
                    }

                default:
                    lexer_.Unexpected();
                    return {};
                }
            }

            logger::Log& Log() { return log_; }
            logger::LineColumnTracker& Tracker() { return tracker_; }
            Lexer& Lexer() { return lexer_; }

        private:
            logger::Log log_;
            logger::Source source_;
            logger::LineColumnTracker tracker_;
            JSONOptions options_;
            javascript::Lexer lexer_;
            bool suppress_warnings_about_weird_code_{};
        };

    }

    std::pair<Expr, bool> ParseJSON(logger::Log log, logger::Source source, JSONOptions options) {
        if (options.error_suffix.empty()) {
            options.error_suffix = " in JSON";
        }

        // Top-level entry point for parsing a complete JSON document.
        // Creates a JSONParser, parses a single expression, and verifies
        // that the entire input was consumed (Expect T::kEndOfFile).  If
        // the lexer or parser encounters any syntax error it emits a
        // LexerPanic, which is caught here — the function then returns
        // ok = false so the caller knows the parse failed.
        //
        // The error_suffix option is defaulted to " in JSON" when not
        // already set, so error messages read naturally (e.g.
        // "Unexpected token } in JSON").
        //
        // Example:
        //   Input:  "hello"
        //   Output: (EString{"hello"}, ok=true)
        //
        //   Input:  "{,}"
        //   Output: (Expr{}, ok=false)
        bool ok = true;
        Expr result{};
        try {
            JSONParser parser(log, source, options);
            result = parser.ParseExpr();
            parser.Lexer().Expect(T::kEndOfFile);
        } catch (const LexerPanic&) {
            ok = false;
        }

        return {std::move(result), ok};
    }

    std::pair<Expr, bool> Parser::ParseJSON(logger::Log log, logger::Source source, JSONOptions options) {
        return ::guchho::javascript::ParseJSON(log, source, options);
    }

    // IsValidJSON: Recursively checks whether an AST tree represents a
    // valid JSON value.  A valid JSON tree may only contain:
    //   - Primitive atoms: EString, ENumber, EBoolean, ENull
    //   - EArray:  every child element must itself be valid JSON
    //   - EObject: every property must be a plain field (not a method,
    //     getter, setter, or computed key), its key must be an EString,
    //     and its value must be valid JSON
    //
    // Any other node type (identifiers, function calls, template literals,
    // etc.) is not valid JSON and causes the function to return false.
    //
    // The AST node types are stored in a std::variant; we dispatch on the
    // held type using std::holds_alternative / std::get_if.
    //
    // Examples:
    //   IsValidJSON(ENumber{42})         => true
    //   IsValidJSON(EBoolean{true})      => true
    //   IsValidJSON(EArray{items: []})   => true
    //   IsValidJSON(EObject{computed: …})=> false (computed key not allowed)
    bool IsValidJSON(const Expr& value) {
        if (std::holds_alternative<std::shared_ptr<EString>>(value.data) ||
            std::holds_alternative<std::shared_ptr<ENumber>>(value.data) ||
            std::holds_alternative<std::shared_ptr<EBoolean>>(value.data) ||
            std::holds_alternative<std::shared_ptr<ENull>>(value.data)) {
            // Primitive JSON value: string, number, boolean, or null.
            // These are always valid JSON and require no further checks.
        } else if (auto* array = std::get_if<std::shared_ptr<EArray>>(&value.data); array != nullptr) {
            for (const Expr& item : (*array)->items) {
                if (!IsValidJSON(item)) {
                    return false;
                }
            }
        } else if (auto* object = std::get_if<std::shared_ptr<EObject>>(&value.data); object != nullptr) {
            for (const javascript::Property& property : (*object)->properties) {
                if (property.kind != javascript::PropertyKind::kField ||
                    javascript::Has(property.flags, javascript::PropertyFlags::kIsComputed)) {
                    return false;
                }
                if (!std::holds_alternative<std::shared_ptr<EString>>(property.key.data)) {
                    return false;
                }
                if (!IsValidJSON(property.value_or_nil)) {
                    return false;
                }
            }
        } else {
            return false;
        }

        return true;
    }

    // ParseGlobalName: Parses a dotted/bracketed identifier path that
    // refers to a global name in the JavaScript environment.  The path
    // can consist of:
    //   - A simple identifier:            "process"
    //   - Dot-separated identifiers:      "process.env.NODE_ENV"
    // - Bracket string-index access:     "process['env']"
    //   - The "this" keyword:            "this"
    //   - The "import.meta" meta-property: "import.meta"
    //
    // Returns a vector of path segments.  For example,
    //   "a.b['c']"  =>  {"a", "b", "c"}
    //   "import.meta.url" => {"import", "meta", "url"}
    //
    // If the input is not a valid global-name expression (e.g. it
    // contains a syntax error), the lexer panics, which is caught here
    // and returned as ok = false.
    std::pair<std::vector<std::string>, bool> ParseGlobalName(logger::Log log, logger::Source source) {
        // Recover from any lexer panic (syntax error) and return ok = false.
        // Non-lexer panics are rethrown by the catch-all above.
        bool ok = true;
        std::vector<std::string> result;
        try {
            Lexer lexer = Lexer::NewGlobalName(log, source);

            // The first token must be an identifier, "this", or "import".
            // Record this initial segment before looking for property access.
            result.push_back(lexer.identifier.str);
            switch (lexer.token) {
            case T::kThis:
                lexer.Next();
                break;

            case T::kImport:
                // Handle the special "import.meta" meta-property access.
                // Consume the dot and expect the contextual keyword "meta".
                lexer.Next();
                lexer.Expect(T::kDot);
                result.push_back(lexer.identifier.str);
                lexer.ExpectContextualKeyword("meta");
                break;

            default:
                lexer.Expect(T::kIdentifier);
                break;
            }

            // Continue parsing chained property access: dot access (foo.bar)
            // and bracket index access (foo["bar"]).  Each segment is appended
            // to the result path.  Stops at end-of-file.
            while (lexer.token != T::kEndOfFile) {
                switch (lexer.token) {
                case T::kDot:
                    lexer.Next();
                    if (!lexer.IsIdentifierOrKeyword()) {
                        lexer.Expect(T::kIdentifier);
                    }
                    result.push_back(lexer.identifier.str);
                    lexer.Next();
                    break;

                case T::kOpenBracket:
                    lexer.Next();
                    result.push_back(helpers::UTF16ToString(lexer.StringLiteral()));
                    lexer.Expect(T::kStringLiteral);
                    lexer.Expect(T::kCloseBracket);
                    break;

                default:
                    lexer.Expect(T::kDot);
                    break;
                }
            }
        } catch (const LexerPanic&) {
            ok = false;
        }

        return {std::move(result), ok};
    }

}