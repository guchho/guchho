#include "guchho/javascript/js_printer.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/javascript/js_helpers.hpp"
#include "guchho/javascript/js_parser.hpp"


// ============================================================================
// js_printer.cpp - JavaScript AST-to-text serializer
//
// Converts a parsed JavaScript AST back into JavaScript source code.  Supports
// minification (whitespace, identifiers, syntax), source-map generation,
// legal-comment extraction, TypeScript enum/const inlining, and property
// mangling.
//
// Public API:
//   Print(tree, symbols, renamer, options) -> PrintResult
//   CanEscapeIdentifier(name, unsupported_features, ascii_only) -> bool
//
// Internal structure:
//   1. Helper functions (number formatting, identifier quoting, escaping)
//   2. Flags enums controlling expression/statement printing behavior
//   3. The `printer` class which walks the AST and emits text
// ============================================================================

namespace guchho::javascript {

    namespace {

        // =============================================================================
        // Constants
        // =============================================================================

        // Hex digit lookup table used for \uXXXX escape sequences and number formatting.
        constexpr char kHexChars[] = "0123456789ABCDEF";
        constexpr int kFirstASCII = 0x20;
        constexpr int kLastASCII = 0x7E;
        constexpr int kFirstHighSurrogate = 0xD800;
        constexpr int kLastHighSurrogate = 0xDBFF;
        constexpr int kFirstLowSurrogate = 0xDC00;
        constexpr int kLastLowSurrogate = 0xDFFF;

        const double kPositiveInfinity = std::numeric_limits<double>::infinity();
        const double kNegativeInfinity = -std::numeric_limits<double>::infinity();

        // Parses a small integer from a string of ASCII digits.
        // Used for floating-point exponents which are always small enough to fit in int.
        // Input: "123" -> 123,  "-5" -> -5
        // Edge cases: assumes valid digit-only input (possibly with leading '-').
        int ParseSmallInt(std::string_view bytes) {
            bool was_negative = bytes[0] == '-';
            if (was_negative) {
                bytes = bytes.substr(1);
            }

            int n = 0;
            for (char c : bytes) {
                n = n * 10 + (c - '0');
            }

            if (was_negative) {
                return -n;
            }
            return n;
        }

        // Converts a bigint literal body from any base (hex 0x, binary 0b, octal 0o,
        // or decimal) into its canonical decimal string representation.
        // Input: "0xFF" -> "255",  "0b1010" -> "10",  "42" -> "42"
        // Stops at the first invalid digit for the given base.
        std::string BigIntToDecimalString(std::string_view value) {
            uint32_t base = 10;
            size_t start = 0;
            if (value.size() >= 2 && value[0] == '0') {
                switch (value[1]) {
                    case 'x':
                    case 'X':
                        base = 16;
                        start = 2;
                        break;
                    case 'b':
                    case 'B':
                        base = 2;
                        start = 2;
                        break;
                    case 'o':
                    case 'O':
                        base = 8;
                        start = 2;
                        break;
                    default:
                        break;
                }
            }

            // Repeated multiply-by-base-and-add over little-endian decimal digits
            std::vector<uint8_t> dec;
            auto mul_add = [&](uint32_t m, uint32_t a) {
                uint64_t carry = a;
                for (size_t i = 0; i < dec.size(); i++) {
                    uint64_t v = static_cast<uint64_t>(dec[i]) * m + carry;
                    dec[i] = static_cast<uint8_t>(v % 10);
                    carry = v / 10;
                }
                while (carry > 0) {
                    dec.push_back(static_cast<uint8_t>(carry % 10));
                    carry /= 10;
                }
            };

            for (size_t i = start; i < value.size(); i++) {
                char c = value[i];
                uint32_t d;
                if (c >= '0' && c <= '9') {
                    d = static_cast<uint32_t>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    d = static_cast<uint32_t>(c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    d = static_cast<uint32_t>(c - 'A' + 10);
                } else {
                    break;
                }
                if (d >= base) {
                    break;
                }
                mul_add(base, d);
            }

            if (dec.empty()) {
                return "0";
            }
            std::string str;
            str.reserve(dec.size());
            for (size_t i = dec.size(); i-- > 0;) {
                str.push_back(static_cast<char>('0' + dec[i]));
            }
            return str;
        }

        // Formats a double into the shortest decimal representation that round-trips,
        // exponential notation when exponent < -4 or >= 6,
        // plain decimal notation otherwise.
        // Input: 123456.0 -> "123456",  0.001 -> "0.001",  1e8 -> "100000000"
        // Edge case: -0 formats as "-0".
        std::string FormatFloatGoStyle(double value) {
            // Get the shortest round-tripping digits via exponential notation
            char buf[40];
            auto result = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::scientific);
            if (result.ec != std::errc()) {
                throw std::runtime_error("Internal error");
            }
            std::string_view text(buf, static_cast<size_t>(result.ptr - buf));

            // Split "-d.dddde±XX" into a sign, the significant digits, and the
            // decimal exponent
            bool is_negative = text[0] == '-';
            if (is_negative) {
                text.remove_prefix(1);
            }
            size_t e = text.find('e');
            std::string_view exponent_text = text.substr(e + 1);
            bool is_exponent_negative = false;
            if (exponent_text[0] == '+') {
                exponent_text.remove_prefix(1);
            } else if (exponent_text[0] == '-') {
                is_exponent_negative = true;
                exponent_text.remove_prefix(1);
            }
            int exponent = ParseSmallInt(exponent_text);
            if (is_exponent_negative) {
                exponent = -exponent;
            }

            std::string digits;
            digits.reserve(e);
            for (size_t i = 0; i < e; i++) {
                if (text[i] != '.') {
                    digits += text[i];
                }
            }

            // Use exponential notation when the exponent is < -4 or >= 6,
            if (exponent < -4 || exponent >= 6) {
                std::string js;
                if (is_negative) {
                    js += '-';
                }
                js += text.substr(0, e);
                js += 'e';
                js += exponent < 0 ? '-' : '+';
                int absolute_exponent = exponent < 0 ? -exponent : exponent;
                if (absolute_exponent < 10) {
                    js += '0';
                }
                js += std::to_string(absolute_exponent);
                return js;
            }

            // Otherwise use plain notation by inserting a decimal point into
            // the digits
            std::string js;
            if (is_negative) {
                js += '-';
            }
            if (exponent >= 0) {
                size_t point = static_cast<size_t>(exponent) + 1;
                if (point >= digits.size()) {
                    // An integer, possibly with trailing zeros: "123" => "123"
                    // and "120" => "12000"
                    js += digits;
                    js.append(point - digits.size(), '0');
                } else {
                    // A number with a fractional part: "123456" => "123.456"
                    js += digits.substr(0, point);
                    js += '.';
                    js += digits.substr(point);
                }
            } else {
                // A number smaller than one: "123" => "0.00123"
                js += "0.";
                js.append(static_cast<size_t>(-exponent) - 1, '0');
                js += digits;
            }
            return js;
        }

        // Appends "\uXXXX" (exactly four uppercase hex digits) for a BMP code point.
        // Input: 0x41 -> appends "\\u0041"
        void AppendHex4(std::string& js, uint32_t c) {
            js += '\\';
            js += 'u';
            js += kHexChars[(c >> 12) & 15];
            js += kHexChars[(c >> 8) & 15];
            js += kHexChars[(c >> 4) & 15];
            js += kHexChars[c & 15];
        }

        // Appends "\u{...}" using uppercase hex without leading zeros, for code points
        // outside the BMP (i.e., > 0xFFFF).
        // Input: 0x1F600 -> appends "\\u{1F600}"
        void AppendUnicodeEscape(std::string& js, char32_t rune) {
            js += "\\u{";
            char buf[9];
            int len = 0;
            uint32_t v = static_cast<uint32_t>(rune);
            do {
                buf[len++] = kHexChars[v & 15];
                v >>= 4;
            } while (v != 0);
            while (len > 0) {
                js += buf[--len];
            }
            js += '}';
        }

        // =============================================================================
        // Flags — bitfield enums that control how expressions and statements are printed
        // =============================================================================

        // Flags passed to print_stmt() to influence statement printing behavior.
        enum class print_stmt_flags : uint8_t {
            kNone = 0,
            kCanOmitStatement = 1 << 0,
        };

        inline bool Has(print_stmt_flags flags, print_stmt_flags flag) {
            return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
        }

        // Flags passed to print_expr() to influence expression printing behavior.
        // Controls things like operator precedence wrapping, whether 'in' is forbidden,
        // whether this is a new.target expression, etc.
        enum class print_expr_flags : uint16_t {
            kNone = 0,
            kIsNewTarget = 1 << 0,
            kForbidIn = 1 << 1,
            kHasNonOptionalChainParent = 1 << 2,
            kExprResultIsUnused = 1 << 3,
            kDidAlreadySimplifyUnusedExprs = 1 << 4,
            kIsFollowedByOf = 1 << 5,
            kIsInsideForAwait = 1 << 6,
            kIsDeleteTarget = 1 << 7,
            kIsCallTargetOrTemplateTag = 1 << 8,
            kIsPropertyAccessTarget = 1 << 9,
            kParentWasUnaryOrBinaryOrIfTest = 1 << 10,
        };

        inline print_expr_flags operator|(print_expr_flags a, print_expr_flags b) {
            return static_cast<print_expr_flags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
        }

        inline print_expr_flags operator&(print_expr_flags a, print_expr_flags b) {
            return static_cast<print_expr_flags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
        }

        inline bool Has(print_expr_flags flags, print_expr_flags flag) {
            return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
        }

        inline print_expr_flags Clear(print_expr_flags flags, print_expr_flags flag) {
            return static_cast<print_expr_flags>(static_cast<uint16_t>(flags) & ~static_cast<uint16_t>(flag));
        }

        // Flags controlling how quoted strings are printed (quote character, wrapping).
        enum class print_quoted_flags : uint8_t {
            kNone = 0,
            kAllowBacktick = 1 << 0,
            kNoWrap = 1 << 1,
        };

        inline print_quoted_flags operator|(print_quoted_flags a, print_quoted_flags b) {
            return static_cast<print_quoted_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
        }

        inline bool Has(print_quoted_flags flags, print_quoted_flags flag) {
            return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
        }

        // Flags indicating what syntactic context an expression starts in.
        // Used to decide whether the expression needs parentheses to avoid
        // being parsed as a different statement form (e.g., a bare object literal
        // at statement start is parsed as a block).
        enum class expr_start_flags : uint8_t {
            kNone = 0,
            kStmtStart = 1 << 0,
            kExportDefaultStart = 1 << 1,
            kArrowExprStart = 1 << 2,
            kForOfInitStart = 1 << 3,
        };

        inline expr_start_flags operator|(expr_start_flags a, expr_start_flags b) {
            return static_cast<expr_start_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
        }

        inline bool Has(expr_start_flags flags, expr_start_flags flag) {
            return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
        }

        inline PropertyFlags Clear(PropertyFlags flags, PropertyFlags flag) {
            return static_cast<PropertyFlags>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(flag));
        }

        enum class print_after_decorator : uint8_t {
            kNewline,
            kSpace,
        };

        struct fn_args_opts {
            logger::Loc open_paren_loc{};
            bool add_mapping_for_open_paren_loc{};
            bool has_rest_arg{};
            bool is_arrow{};
        };

        // Used by "printer::print_decorators" to decide whether to omit the
        // following indent.
        bool can_use_shorthand_property(std::span<const char16_t> key, std::string_view name, PropertyFlags flags);

        // =============================================================================
        // binary_expr_visitor — iterative (heap-based) binary expression printer
        // =============================================================================
        // Instead of recursing into left-associative binary chains (which can overflow
        // the call stack for deeply nested ASTs), we iterate leftward pushing entries
        // onto a stack, then unwind printing right operands on the way back.
        struct binary_expr_visitor {
            // Inputs
            const EBinary* e{};

            // Inputs
            L level{L::kLowest};
            print_expr_flags flags{print_expr_flags::kNone};

            // Input for visiting the left child
            L left_level{L::kLowest};
            print_expr_flags left_flags{print_expr_flags::kNone};

            // "Local variables" passed from "checkAndPrepare" to
            // "visitRightAndFinish"
            OpTableEntry entry{};
            bool wrap{};
            L right_level{L::kLowest};
        };

        // Result type for TypeScript enum value lookups during cross-module inlining.
        // Contains the resolved value (numeric or string), the enum member name,
        // and whether a match was found.
        struct TSEnumLookupResult {
            TSEnumValue value;
            std::string name;
            bool found{};
        };

        // =============================================================================
        // QuoteIdentifier — escapes non-ASCII characters in identifiers
        // =============================================================================
        // Converts an identifier name to a valid JavaScript identifier by escaping
        // non-ASCII characters as \uXXXX or \u{XXXXX}. ASCII runs are copied in bulk.
        // Input: "café" -> "caf\\u00E9",  "foo" -> "foo"
        // Edge: throws if Unicode escapes are unsupported but non-ASCII chars are present.
        void QuoteIdentifier(std::string& js, std::string_view name, compat::JSFeature unsupported_features) {
            bool is_ascii = false;
            size_t ascii_start = 0;
            size_t n = name.size();

            for (size_t i = 0; i < n;) {
                unsigned char c = static_cast<unsigned char>(name[i]);
                if (c >= kFirstASCII && c <= kLastASCII) {
                    // Fast path: a run of ASCII characters
                    if (!is_ascii) {
                        is_ascii = true;
                        ascii_start = i;
                    }
                    i++;
                } else {
                    // Slow path: escape non-ASCII characters
                    if (is_ascii) {
                        js.append(name.data() + ascii_start, i - ascii_start);
                        is_ascii = false;
                    }

                    auto [rune, width] = helpers::DecodeRuneInString(std::string_view(name).substr(i));
                    i += static_cast<size_t>(width);

                    if (rune <= 0xFFFF) {
                        AppendHex4(js, static_cast<uint32_t>(rune));
                    } else if (!compat::Has(unsupported_features, compat::JSFeature::kUnicodeEscapes)) {
                        AppendUnicodeEscape(js, rune);
                    } else {
                        throw std::runtime_error("Internal error: Cannot encode identifier: Unicode escapes are unsupported");
                    }
                }
            }

            if (is_ascii) {
                // Print one final run of ASCII characters
                js.append(name.data() + ascii_start, n - ascii_start);
            }
        }

        // =============================================================================
        // printer — the core AST-to-text serialization engine
        // =============================================================================
        // Walks every AST node (expressions, statements, bindings, properties, etc.)
        // and appends the corresponding JavaScript source text to `js_`.  Manages:
        //   - Indentation and whitespace (respects minify_whitespace flag)
        //   - Source-map offsets via `builder_`
        //   - Operator precedence / parenthesization via `level` parameters
        //   - Expression comments (preserved non-semantic comments)
        //   - Legal comment extraction
        //   - Cross-module TypeScript enum / constant inlining
        //   - Property name mangling
        class printer {
        public:
            // Constructs the printer with all configuration and AST data needed to
            // emit JavaScript.  Initializes the source-map builder from the input
            // source-map and line offset tables.
            printer(const PrinterOptions& options, compiler::SymbolMap& symbols, Renamer& renamer, const AST& tree)
                : options_(options),
                symbols_(symbols),
                ast_helpers_(MakeHelperContext([this](compiler::Ref ref) {
                    ref = compiler::FollowSymbols(symbols_, ref);
                    return symbols_.Get(ref)->kind == compiler::SymbolKind::kUnbound;
                })),
                renamer_(renamer),
                parts_(tree.parts),
                directives_(tree.directives),
                import_records_(tree.import_records),
                expr_comments_(tree.expr_comments),
                builder_(sourcemap::MakeChunkBuilder(options.input_source_map, options.line_offset_tables, options.ascii_only)),
                stmt_start_(-1),
                export_default_start_(-1),
                arrow_expr_start_(-1),
                for_of_init_start_(-1),
                prev_op_end_(-1),
                need_space_before_dot_(-1),
                prev_regexp_end_(-1),
                no_leading_newline_here_(-1),
                indent_(options.indent),
                was_lazy_export_(tree.has_lazy_export),
                module_type_(tree.module_type_data.type) {}

            PrintResult Run();

        private:
            const PrinterOptions& options_;
            compiler::SymbolMap& symbols_;
            HelperContext ast_helpers_;
            Renamer& renamer_;
            const std::vector<Part>& parts_;
            const std::vector<std::string>& directives_;
            const std::vector<compiler::ImportRecord>& import_records_;
            const std::unordered_map<logger::Loc, std::vector<std::string>, LocHash>& expr_comments_;
            std::unordered_map<logger::Loc, bool, LocHash> printed_expr_comments_;
            std::unordered_set<std::string> has_legal_comment_;
            std::vector<std::string> extracted_legal_comments_;
            std::string js_;
            std::vector<std::string> json_metadata_imports_;
            std::vector<binary_expr_visitor> binary_expr_stack_;
            sourcemap::ChunkBuilder builder_;
            bool print_next_indent_as_space_{};

            int stmt_start_{};
            int export_default_start_{};
            int arrow_expr_start_{};
            int for_of_init_start_{};

            int with_nesting_{};
            int prev_op_end_{};
            int need_space_before_dot_{};
            int prev_regexp_end_{};
            int no_leading_newline_here_{};
            int old_line_start_{};
            int old_line_end_{};
            int indent_{};
            std::array<char, 64> int_to_bytes_buffer_{};
            bool needs_semicolon_{};
            bool was_lazy_export_{};
            OpCode prev_op_{OpCode::kUnOpPos};
            const EImportIdentifier* call_target_{};
            ModuleType module_type_{ModuleType::kUnknown};

            // -- Primitive printing --

            void print(std::string_view text);
            void print_bytes(std::string_view bytes);
            void print_newline();
            bool print_newline_past_line_limit();
            void print_space();
            void print_indent();
            void print_semicolon_after_statement();
            void print_semicolon_if_needed();
            void print_space_before_identifier();
            void print_space_before_operator(OpCode next);
            int current_line_length();

            void add_source_mapping(logger::Loc loc);
            void add_source_mapping_for_name(logger::Loc loc, std::string_view name, compiler::Ref ref);

            std::string mangled_prop_name(compiler::Ref ref);
            std::pair<TSEnumValue, bool> try_to_get_imported_enum_value(const Expr& target, const std::string& name);
            TSEnumLookupResult try_to_get_imported_enum_value_utf16(const Expr& target, std::span<const char16_t> name);
            void print_clause_alias(logger::Loc loc, const std::string& alias);

            bool can_print_identifier(const std::string& name);
            bool can_print_identifier_utf16(std::span<const uint16_t> name);
            void print_identifier(const std::string& name);
            void print_identifier_utf16(std::span<const uint16_t> name);
            void print_number(double value, L level);
            void print_non_negative_float(double abs_value);
            std::string_view small_int_to_bytes(int n);

            void print_quoted_utf8(std::string_view text, print_quoted_flags flags);
            void print_quoted_utf16(std::span<const char16_t> data, print_quoted_flags flags);
            void print_unquoted_utf16(std::span<const char16_t> text, char quote, print_quoted_flags flags);

            void print_jsx_tag(const Expr& tag_or_nil);

            void print_binding(const Binding& binding);

            void print_fn_args(const std::vector<Arg>& args, const fn_args_opts& opts);
            void print_fn(const Fn& fn);
            bool print_decorators(const std::vector<Decorator>& decorators, print_after_decorator default_mode);
            void print_block(logger::Loc loc, const SBlock& block);
            void print_class(const Class& class_);
            void print_property(const Property& property);

            L print_dot_then_prefix();
            void print_dot_then_suffix();
            void print_undefined(logger::Loc loc, L level);

            Expr simplify_unused_expr(const Expr& expr);
            Expr guard_against_behavior_change_due_to_substitution(Expr expr, print_expr_flags flags);
            Expr late_constant_fold_unary_or_binary_or_if_expr(Expr expr);
            bool is_identifier_or_numeric_constant_or_property_access(const Expr& expr);
            bool is_unbound_identifier(const Expr& expr);
            bool is_unbound_eval_identifier(const Expr& expr);

            expr_start_flags save_expr_start_flags();
            void restore_expr_start_flags(expr_start_flags flags);

            void print_indented_comment(std::string_view text);
            void print_expr_comments_at_loc(logger::Loc loc);
            void print_expr_comments_after_close_token_at_loc(logger::Loc loc);
            bool will_print_expr_comments_at_loc(logger::Loc loc);
            bool will_print_expr_comments_for_any_of(std::span<const Expr> exprs);
            void print_expr_without_leading_newline(const Expr& expr, L level, print_expr_flags flags);

            void print_require_or_import_expr(uint32_t import_record_index, L level, print_expr_flags flags,
                                              logger::Loc close_paren_loc, compiler::ImportPhase phase);
            void print_path(uint32_t import_record_index, compiler::ImportKind import_kind);
            bool omit_runtime_import(uint32_t import_record_index) const;
            void print_import_call_assert_or_with(const std::shared_ptr<compiler::ImportAssertOrWith>& assert_or_with,
                                                  bool outer_is_multi_line);
            void print_import_assert_or_with_clause(const compiler::ImportAssertOrWith& assert_or_with);

            bool check_and_prepare(binary_expr_visitor& v);
            void visit_right_and_finish(binary_expr_visitor& v);

            void print_decl_stmt(bool is_export, std::string_view keyword, const std::vector<Decl>& decls);
            void print_for_loop_init(const Stmt& init, print_expr_flags flags);
            void print_decls(std::string_view keyword, const std::vector<Decl>& decls, print_expr_flags flags);
            void print_body(const Stmt& body, bool is_single_line);
            void print_if(const SIf& s);
            void print_stmt(const Stmt& stmt, print_stmt_flags flags);

            // Not yet ported (callers are wired up so the port can proceed).
            void print_expr(Expr expr, L level, print_expr_flags flags);
        };

        // =============================================================================
        // Free functions
        // =============================================================================

    } // namespace

    // Returns true if `name` can be safely printed as a bare identifier in the
    // target environment without quoting.  A name needs quoting if it is not a
    // valid ES5+ identifier, or if ascii-only mode is on and it contains non-BMP
    // characters that can't be escaped.
    bool CanEscapeIdentifier(std::string_view name, compat::JSFeature unsupported_features, bool ascii_only) {
        return IsIdentifierES5AndESNext(name) && (!ascii_only ||
            !compat::Has(unsupported_features, compat::JSFeature::kUnicodeEscapes) ||
            !helpers::ContainsNonBMPCodePoint(name));
    }

    // Entry point: serializes a complete AST into JavaScript source code.
    // Applies minification, source-map generation, legal-comment extraction,
    // and cross-module constant/enum inlining as configured.
    // Input:  AST tree + symbol table + renamer + options
    // Output: PrintResult containing JS text, extracted comments, metadata imports,
    //         and a source-map chunk.
    PrintResult Print(const AST& tree, compiler::SymbolMap& symbols, Renamer& renamer, const PrinterOptions& options) {
        printer p(options, symbols, renamer, tree);
        return p.Run();
    }

    namespace {

        // =============================================================================
        // printer implementation
        // =============================================================================

        // Main entry: iterates all parts/statements in the AST and serializes each
        // one.  Returns the generated JS text plus any extracted metadata.
        PrintResult printer::Run() {
            // Add the top-level directive if present
            for (const std::string& directive : directives_) {
                print_indent();
                print_quoted_utf8(directive, print_quoted_flags::kNone);
                print(";");
                print_newline();
            }

            for (const Part& part : parts_) {
                for (const Stmt& stmt : part.stmts) {
                    print_stmt(stmt, print_stmt_flags::kCanOmitStatement);
                    print_semicolon_if_needed();
                }
            }

            PrintResult result;
            result.js = std::move(js_);
            result.json_metadata_imports = std::move(json_metadata_imports_);
            result.extracted_legal_comments = std::move(extracted_legal_comments_);

            if (options_.source_map != config::SourceMap::kNone) {
                // This is expensive. Only do this if it's necessary.
                result.source_map_chunk = builder_.GenerateChunk(result.js);
            }
            return result;
        }

        // -- Primitive output methods --
        // These are the lowest-level output primitives that all higher-level
        // printing functions build upon.  They append directly to js_.

        // Appends raw text to the output buffer.
        void printer::print(std::string_view text) {
            js_.append(text.data(), text.size());
        }

        // Appends raw bytes to the output buffer (same as print, avoids a temporary).
        void printer::print_bytes(std::string_view bytes) {
            js_.append(bytes.data(), bytes.size());
        }

        // Appends a newline character (suppressed when minifying whitespace).
        void printer::print_newline() {
            if (!options_.minify_whitespace) {
                print("\n");
            }
        }

        // Inserts a newline if the current line exceeds the configured line limit.
        // Returns true if a newline was inserted (caller may need to print indent).
        bool printer::print_newline_past_line_limit() {
            if (current_line_length() < options_.line_limit) {
                return false;
            }
            print("\n");
            print_indent();
            return true;
        }

        // Appends a space (suppressed when minifying whitespace).
        void printer::print_space() {
            if (!options_.minify_whitespace) {
                print(" ");
            }
        }

        // Prints the current indentation level as spaces (2 spaces per indent level).
        // In minified mode this is a no-op.  Respects the line limit by capping
        // indent width.  If print_next_indent_as_space_ is set, prints a single
        // space instead (used for single-line arrow function bodies).
        void printer::print_indent() {
            if (options_.minify_whitespace) {
                return;
            }

            if (print_next_indent_as_space_) {
                print(" ");
                print_next_indent_as_space_ = false;
                return;
            }

            int indent = indent_;
            if (options_.line_limit > 0 && indent * 2 >= options_.line_limit) {
                indent = options_.line_limit / 2;
            }
            for (int i = 0; i < indent; i++) {
                print("  ");
            }
        }

        // Prints ";\n" in normal mode, or sets a deferred-semicolon flag in minified mode.
        void printer::print_semicolon_after_statement() {
            if (!options_.minify_whitespace) {
                print(";\n");
            } else {
                needs_semicolon_ = true;
            }
        }

        // Emits a deferred semicolon if one was requested by print_semicolon_after_statement
        // in minified mode.
        void printer::print_semicolon_if_needed() {
            if (needs_semicolon_) {
                print(";");
                needs_semicolon_ = false;
            }
        }

        // Prints a space before an identifier if the previous character would
        // otherwise merge with the identifier (e.g., after another identifier,
        // number, or regex literal).
        void printer::print_space_before_identifier() {
            char32_t c = 0;
            if (!js_.empty()) {
                auto [decoded, width] = helpers::DecodeLastRuneInString(js_);
                c = decoded;
            }
            if (IsIdentifierContinue(c) || prev_regexp_end_ == static_cast<int>(js_.size())) {
                print(" ");
            }
        }

        // Prints a space between two consecutive operators when needed to avoid
        // forming a different operator (e.g., "+ +" vs "++", "-->" vs "-- >").
        void printer::print_space_before_operator(OpCode next) {
            if (prev_op_end_ != static_cast<int>(js_.size())) {
                return;
            }

            OpCode prev = prev_op_;

            // "+ + y" => "+ +y"
            // "+ ++ y" => "+ ++y"
            // "x + + y" => "x+ +y"
            // "x ++ + y" => "x+++y"
            // "x + ++ y" => "x+ ++y"
            // "-- >" => "-- >"
            // "< ! --" => "<! --"
            if (((prev == OpCode::kBinOpAdd || prev == OpCode::kUnOpPos) && (next == OpCode::kBinOpAdd || next == OpCode::kUnOpPos || next == OpCode::kUnOpPreInc)) ||
                ((prev == OpCode::kBinOpSub || prev == OpCode::kUnOpNeg) && (next == OpCode::kBinOpSub || next == OpCode::kUnOpNeg || next == OpCode::kUnOpPreDec)) ||
                (prev == OpCode::kUnOpPostDec && next == OpCode::kBinOpGt) ||
                (prev == OpCode::kUnOpNot && next == OpCode::kUnOpPreDec && js_.size() > 1 && js_[js_.size() - 2] == '<')) {
                print(" ");
            }
        }

        // Returns the number of characters on the current line (from the last newline
        // to the current output position).  Also updates old_line_start_ / old_line_end_.
        int printer::current_line_length() {
            std::string& js = js_;
            int n = static_cast<int>(js.size());
            int stop = old_line_end_;

            // Update "old_line_start_" to the start of the current line
            for (int i = n; i > stop; i--) {
                char c = js[static_cast<size_t>(i - 1)];
                if (c == '\r' || c == '\n') {
                    old_line_start_ = i;
                    break;
                }
            }

            old_line_end_ = n;
            return n - old_line_start_;
        }

        // -- Source mapping --
        // Records a source mapping for the current output position.
        // When add_source_mappings is disabled, this is a no-op.
        void printer::add_source_mapping(logger::Loc loc) {
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(loc, "", js_);
            }
        }

        // Records a source mapping for a name, including the original (pre-minification)
        // name if it differs from the printed name.
        void printer::add_source_mapping_for_name(logger::Loc loc, std::string_view name, compiler::Ref ref) {
            if (options_.add_source_mappings) {
                const std::string& original_name = symbols_.Get(compiler::FollowSymbols(symbols_, ref))->original_name;
                if (original_name != name) {
                    builder_.AddSourceMapping(loc, original_name, js_);
                } else {
                    builder_.AddSourceMapping(loc, "", js_);
                }
            }
        }

        // Returns the mangled property name for a symbol, consulting the property
        // mangling map first, then falling back to the renamer.
        std::string printer::mangled_prop_name(compiler::Ref ref) {
            ref = compiler::FollowSymbols(symbols_, ref);
            if (auto it = options_.mangled_props.find(ref); it != options_.mangled_props.end()) {
                return it->second;
            }
            return renamer_.NameForSymbol(ref);
        }

        // Attempts to look up a TypeScript enum member value by its string name
        // from a cross-module enum reference.  Returns {value, found=true} on success.
        std::pair<TSEnumValue, bool> printer::try_to_get_imported_enum_value(const Expr& target, const std::string& name) {
            if (const auto* id = Get<EImportIdentifier>(target.data)) {
                compiler::Ref ref = compiler::FollowSymbols(symbols_, id->ref);
                if (symbols_.Get(ref)->kind == compiler::SymbolKind::kTSEnum) {
                    if (auto it = options_.ts_enums.find(ref); it != options_.ts_enums.end()) {
                        if (auto it2 = it->second.find(name); it2 != it->second.end()) {
                            return {it2->second, true};
                        }
                    }
                }
            }
            return {TSEnumValue{}, false};
        }

        // Same as try_to_get_imported_enum_value but accepts a UTF-16 key.
        TSEnumLookupResult printer::try_to_get_imported_enum_value_utf16(const Expr& target, std::span<const char16_t> name) {
            if (const auto* id = Get<EImportIdentifier>(target.data)) {
                compiler::Ref ref = compiler::FollowSymbols(symbols_, id->ref);
                if (symbols_.Get(ref)->kind == compiler::SymbolKind::kTSEnum) {
                    if (auto it = options_.ts_enums.find(ref); it != options_.ts_enums.end()) {
                        std::string name_string = helpers::UTF16ToString(name);
                        if (auto it2 = it->second.find(name_string); it2 != it->second.end()) {
                            return {it2->second, std::move(name_string), true};
                        }
                    }
                }
            }
            return {TSEnumValue{}, "", false};
        }

        // Prints a clause alias (e.g., in `import { x as y } from '...'` or
        // `export { x as y }`).  Uses identifier printing if the alias is a valid
        // identifier, otherwise quotes it as a string.
        void printer::print_clause_alias(logger::Loc loc, const std::string& alias) {
            if (IsIdentifier(alias)) {
                print_space_before_identifier();
                add_source_mapping(loc);
                print_identifier(alias);
            } else {
                add_source_mapping(loc);
                print_quoted_utf8(alias, print_quoted_flags::kNone);
            }
        }

        // -- Identifier printing --
        // Returns true if `name` can be printed as a bare identifier (valid ES5+ name
        // and compatible with ascii-only / unicode-escape constraints).
        bool printer::can_print_identifier(const std::string& name) {
            return IsIdentifierES5AndESNext(name) && (!options_.ascii_only ||
                !compat::Has(options_.unsupported_features, compat::JSFeature::kUnicodeEscapes) ||
                !helpers::ContainsNonBMPCodePoint(name));
        }

        // Same as can_print_identifier but for UTF-16 encoded names.
        bool printer::can_print_identifier_utf16(std::span<const uint16_t> name) {
            return IsIdentifierES5AndESNextUTF16(name) && (!options_.ascii_only ||
                !compat::Has(options_.unsupported_features, compat::JSFeature::kUnicodeEscapes) ||
                !helpers::ContainsNonBMPCodePointUTF16(
                    std::span<const char16_t>(reinterpret_cast<const char16_t*>(name.data()), name.size())));
        }

        // Prints a UTF-8 identifier, escaping non-ASCII characters if ascii_only mode is on.
        void printer::print_identifier(const std::string& name) {
            if (options_.ascii_only) {
                QuoteIdentifier(js_, name, options_.unsupported_features);
            } else {
                print(name);
            }
        }

        // Prints a UTF-16 identifier, handling surrogate pairs and escaping
        // non-ASCII characters when ascii_only mode is on.
        void printer::print_identifier_utf16(std::span<const uint16_t> name) {
            char temp[4];
            size_t n = name.size();

            for (size_t i = 0; i < n; i++) {
                char32_t c = static_cast<char32_t>(name[i]);

                if (c >= kFirstHighSurrogate && c <= kLastHighSurrogate && i + 1 < n) {
                    char32_t c2 = static_cast<char32_t>(name[i + 1]);
                    if (c2 >= kFirstLowSurrogate && c2 <= kLastLowSurrogate) {
                        c = (c << 10) + c2 + (0x10000 - (static_cast<char32_t>(kFirstHighSurrogate) << 10) - kFirstLowSurrogate);
                        i++;
                    }
                }

                if (options_.ascii_only && c > kLastASCII) {
                    if (c <= 0xFFFF) {
                        AppendHex4(js_, static_cast<uint32_t>(c));
                    } else if (!compat::Has(options_.unsupported_features, compat::JSFeature::kUnicodeEscapes)) {
                        AppendUnicodeEscape(js_, c);
                    } else {
                        throw std::runtime_error("Internal error: Cannot encode identifier: Unicode escapes are unsupported");
                    }
                    continue;
                }

                int width = helpers::EncodeWTF8Rune(temp, c);
                js_.append(temp, static_cast<size_t>(width));
            }
        }

        // -- Number printing --
        // Prints a numeric literal.  Handles NaN, Infinity, negative numbers,
        // and wraps in parentheses when needed for operator precedence.
        // Input: 42.0, L::kLowest -> "42"
        // Input: -1.0, L::kPrefix -> "(-1)"
        // Input: NaN, L::kLowest, with_nesting_=0 -> "NaN"
        // Edge: with_nesting_ != 0 avoids identifier-like output (NaN, Infinity).
        void printer::print_number(double value, L level) {
            double abs_value = std::fabs(value);

            if (std::isnan(value)) {
                print_space_before_identifier();
                if (with_nesting_ != 0) {
                    // "with (x) NaN" really means "x.NaN" so avoid identifiers when
                    // "with" is present
                    bool wrap = level >= L::kMultiply;
                    if (wrap) {
                        print("(");
                    }
                    if (options_.minify_whitespace) {
                        print("0/0");
                    } else {
                        print("0 / 0");
                    }
                    if (wrap) {
                        print(")");
                    }
                } else {
                    print("NaN");
                }
            } else if (value == kPositiveInfinity || value == kNegativeInfinity) {
                // "with (x) Infinity" really means "x.Infinity" so avoid identifiers
                // when "with" is present
                bool wrap = ((options_.minify_syntax || with_nesting_ != 0) && level >= L::kMultiply) ||
                    (value == kNegativeInfinity && level >= L::kPrefix);
                if (wrap) {
                    print("(");
                }
                if (value == kNegativeInfinity) {
                    print_space_before_operator(OpCode::kUnOpNeg);
                    print("-");
                } else {
                    print_space_before_identifier();
                }
                if (!options_.minify_syntax && with_nesting_ == 0) {
                    print("Infinity");
                } else if (options_.minify_whitespace) {
                    print("1/0");
                } else {
                    print("1 / 0");
                }
                if (wrap) {
                    print(")");
                }
            } else {
                if (!std::signbit(value)) {
                    print_space_before_identifier();
                    print_non_negative_float(abs_value);
                } else if (level >= L::kPrefix) {
                    // Expressions such as "(-1).toString" need to wrap negative
                    // numbers. Instead of testing for "value < 0" we test for
                    // "signbit(value)" and "!isNaN(value)" because we need this to
                    // be true for "-0" and "-0 < 0" is false.
                    print("(-");
                    print_non_negative_float(abs_value);
                    print(")");
                } else {
                    print_space_before_operator(OpCode::kUnOpNeg);
                    print("-");
                    print_non_negative_float(abs_value);
                }
            }
        }

        // Prints the absolute value of a float using the shortest representation.
        // Applies post-processing optimizations: strips leading zeros from exponents,
        // uses hex notation for large integers in minified mode, converts trailing
        // zeros to exponent notation when shorter, etc.
        // Input: 123456.0 -> "123456",  0.001 -> "1e-3" or "0.001" (depends on size)
        void printer::print_non_negative_float(double abs_value) {
            // We can avoid the slow call to format the float for integers less than
            // 1000 because we know that exponential notation will always be longer
            // than the integer representation. This is not the case for 1000 which
            // is "1e3".
            if (abs_value < 1000) {
                if (int64_t as_int = static_cast<int64_t>(abs_value); abs_value == static_cast<double>(as_int)) {
                    print_bytes(small_int_to_bytes(static_cast<int>(as_int)));

                    // Integers always need a space before "." to avoid making a
                    // decimal point
                    need_space_before_dot_ = static_cast<int>(js_.size());
                    return;
                }
            }

            // Format this number into a string so we can mutate it in place
            // without further reallocation
            std::string result = FormatFloatGoStyle(abs_value);

            // Simplify the exponent
            // "e+05" => "e5"
            // "e-05" => "e-5"
            if (size_t e = result.find_last_of('e'); e != std::string::npos) {
                size_t from = e + 1;
                size_t to = from;

                switch (result[from]) {
                case '+':
                    // Strip off the leading "+"
                    from++;
                    break;

                case '-':
                    // Skip past the leading "-"
                    to++;
                    from++;
                    break;

                default:
                    break;
                }

                // Strip off leading zeros
                while (from < result.size() && result[from] == '0') {
                    from++;
                }

                result = result.substr(0, to) + result.substr(from);
            }

            size_t dot = result.find('.');

            if (dot == 1 && result[0] == '0') {
                // Simplify numbers starting with "0."
                size_t after_dot = 2;

                // Strip off the leading zero when minifying
                // "0.5" => ".5"
                if (options_.minify_whitespace) {
                    result = result.substr(1);
                    after_dot--;
                }

                // Try using an exponent
                // "0.001" => "1e-3"
                if (result[after_dot] == '0') {
                    size_t i = after_dot + 1;
                    while (result[i] == '0') {
                        i++;
                    }
                    std::string remaining = result.substr(i);

                    std::string_view exponent = small_int_to_bytes(static_cast<int>(after_dot - i - remaining.size()));

                    // Only switch if it's actually shorter
                    if (result.size() > remaining.size() + 1 + exponent.size()) {
                        result = remaining + "e" + std::string(exponent);
                    }
                }
            } else if (dot != std::string::npos) {
                // Try to get rid of a "." and maybe also an "e"
                if (size_t e = result.find_last_of('e'); e != std::string::npos) {
                    std::string integer = result.substr(0, dot);
                    std::string fraction = result.substr(dot + 1, e - (dot + 1));
                    int exponent = ParseSmallInt(result.substr(e + 1)) - static_cast<int>(fraction.size());

                    // Handle small exponents by appending zeros instead
                    if (exponent >= 0 && exponent <= 2) {
                        // "1.2e1" => "12"
                        // "1.2e2" => "120"
                        // "1.2e3" => "1200"
                        if (result.size() >= integer.size() + fraction.size() + static_cast<size_t>(exponent)) {
                            result = integer + fraction;
                            for (int i = 0; i < exponent; i++) {
                                result += '0';
                            }
                        }
                    } else {
                        // "1.2e4" => "12e3"
                        std::string_view exponent_bytes = small_int_to_bytes(exponent);
                        if (result.size() >= integer.size() + fraction.size() + 1 + exponent_bytes.size()) {
                            result = integer + fraction + "e" + std::string(exponent_bytes);
                        }
                    }
                }
            } else if (result[result.size() - 1] == '0') {
                // Simplify numbers ending with "0" by trying to use an exponent
                // "1000" => "1e3"
                size_t i = result.size() - 1;
                while (i > 0 && result[i - 1] == '0') {
                    i--;
                }
                std::string remaining = result.substr(0, i);
                std::string_view exponent = small_int_to_bytes(static_cast<int>(result.size() - i));

                // Only switch if it's actually shorter
                if (result.size() > remaining.size() + 1 + exponent.size()) {
                    result = remaining + "e" + std::string(exponent);
                }
            }

            // Numbers in this range can potentially be printed with one fewer byte
            // as hex. This compares against 0xFFFF_FFFF_FFFF_F800 instead of
            // comparing against 0xFFFF_FFFF_FFFF_FFFF because 0xFFFF_FFFF_FFFF_FFFF
            // when converted to float64 rounds up to 0x1_0000_0000_0000_0180, which
            // can no longer fit into uint64. The result of converting float64
            // to uint64 outside of the uint64 range is implementation-dependent and
            // is different on amd64 vs. arm64. The float64 value 0xFFFF_FFFF_FFFF_F800
            // is the biggest value that is below the float64 value
            // 0x1_0000_0000_0000_0180, so we use that instead.
            if (options_.minify_whitespace && abs_value >= 1000000000000.0 && abs_value <= 0xFFFF'FFFF'FFFF'F800) {
                if (uint64_t as_int = static_cast<uint64_t>(abs_value); abs_value == static_cast<double>(as_int)) {
                    char hex[24];
                    auto hex_result = std::to_chars(hex, hex + sizeof(hex), as_int, 16);
                    std::string_view hex_view(hex, static_cast<size_t>(hex_result.ptr - hex));
                    if (2 + hex_view.size() < result.size()) {
                        result = "0x" + std::string(hex_view);
                    }
                }
            }

            print_bytes(result);

            // We'll need a space before "." if it could be parsed as a decimal
            // point
            if (result.find_first_of(".ex") == std::string::npos) {
                need_space_before_dot_ = static_cast<int>(js_.size());
            }
        }

        // Converts a small integer to its decimal string representation, writing
        // into a reusable stack buffer.  Returns a string_view into that buffer.
        // Used for float exponents and inline integer formatting.
        std::string_view printer::small_int_to_bytes(int n) {
            bool was_negative = n < 0;
            if (was_negative) {
                // This assumes that -INT_MIN isn't a problem. This is fine because
                // these integers are floating-point exponents which never go up that
                // high.
                n = -n;
            }

            size_t start = int_to_bytes_buffer_.size();

            // Write out the number from the end to the front
            for (;;) {
                start--;
                int_to_bytes_buffer_[start] = '0' + static_cast<char>(n % 10);
                n /= 10;
                if (n == 0) {
                    break;
                }
            }

            // Stick a negative sign on the front if needed
            if (was_negative) {
                start--;
                int_to_bytes_buffer_[start] = '-';
            }

            return std::string_view(int_to_bytes_buffer_.data() + start, int_to_bytes_buffer_.size() - start);
        }

        // -- String / template literal printing --
        // Prints a UTF-8 string with appropriate quoting.  Converts to UTF-16 internally
        // and delegates to print_quoted_utf16.
        void printer::print_quoted_utf8(std::string_view text, print_quoted_flags flags) {
            std::u16string utf16 = helpers::StringToUTF16(text);
            print_quoted_utf16(utf16, flags);
        }

        // Prints a UTF-16 string with the optimal quote character (', ", or `).
        // Selects the quote that minimizes escape sequences.  Handles newline
        // wrapping for strings exceeding the line limit.
        void printer::print_quoted_utf16(std::span<const char16_t> data, print_quoted_flags flags) {
            if (compat::Has(options_.unsupported_features, compat::JSFeature::kTemplateLiteral)) {
                flags = static_cast<print_quoted_flags>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(print_quoted_flags::kAllowBacktick));
            }

            int single_cost = 0;
            int double_cost = 0;
            int backtick_cost = 0;

            for (size_t i = 0; i < data.size(); i++) {
                char16_t c = data[i];
                switch (c) {
                case '\n':
                    if (options_.minify_syntax) {
                        // The backslash for the newline costs an extra character for
                        // old-style string literals when compared to a template literal
                        backtick_cost--;
                    }
                    break;

                case '\'':
                    single_cost++;
                    break;

                case '"':
                    double_cost++;
                    break;

                case '`':
                    backtick_cost++;
                    break;

                case '$':
                    // "${" sequences need to be escaped in template literals
                    if (i + 1 < data.size() && data[i + 1] == '{') {
                        backtick_cost++;
                    }
                    break;

                default:
                    break;
                }
            }

            std::string c = "\"";
            if (double_cost > single_cost) {
                c = "'";
                if (single_cost > backtick_cost && Has(flags, print_quoted_flags::kAllowBacktick)) {
                    c = "`";
                }
            } else if (double_cost > backtick_cost && Has(flags, print_quoted_flags::kAllowBacktick)) {
                c = "`";
            }

            print(c);
            print_unquoted_utf16(data, c[0], flags);
            print(c);
        }

        // Core string escaping routine.  Processes each UTF-16 code unit and:
        //   - Escapes control characters (\n, \r, \t, \0, etc.)
        //   - Escapes quote characters when they match the enclosing quote
        //   - Escapes ${ inside template literals
        //   - Escapes </script to prevent HTML injection
        //   - Handles surrogate pairs for code points above U+FFFF
        //   - Encodes as UTF-8 or uses \uXXXX / \u{XXXXX} escapes in ascii-only mode
        //   - Wraps long lines with backslash-newline when a line limit is configured
        void printer::print_unquoted_utf16(std::span<const char16_t> text, char quote, print_quoted_flags flags) {
            char temp[4];
            std::string& js = js_;
            size_t i = 0;
            size_t n = text.size();

            // Only compute the line length if necessary
            int start_line_length = 0;
            bool wrap_long_lines = false;
            if (options_.line_limit > 0 && !Has(flags, print_quoted_flags::kNoWrap)) {
                start_line_length = current_line_length();
                if (start_line_length > options_.line_limit) {
                    start_line_length = options_.line_limit;
                }
                wrap_long_lines = true;
            }

            while (i < n) {
                // Wrap long lines that are over the limit using escaped newlines
                if (wrap_long_lines && start_line_length + static_cast<int>(i) >= options_.line_limit) {
                    js += "\\\n";
                    start_line_length -= options_.line_limit;
                }

                char16_t c = text[i];
                i++;

                switch (c) {
                // Special-case the null character since it may mess with code
                // written in C that treats null characters as the end of the string.
                case 0:
                    // We don't want "\x001" to be written as "\01"
                    if (i < n && text[i] >= '0' && text[i] <= '9') {
                        js += "\\x00";
                    } else {
                        js += "\\0";
                    }
                    break;

                // Special-case the bell character since it may cause dumping this
                // file to the terminal to make a sound, which is undesirable.
                case 0x07:
                    js += "\\x07";
                    break;

                case 0x08:
                    js += "\\b";
                    break;

                case 0x0C:
                    js += "\\f";
                    break;

                case '\n':
                    if (quote == '`') {
                        start_line_length = -static_cast<int>(i); // Printing a real newline resets the line length
                        js += '\n';
                    } else {
                        js += "\\n";
                    }
                    break;

                case '\r':
                    js += "\\r";
                    break;

                case 0x0B:
                    js += "\\v";
                    break;

                case 0x1B:
                    js += "\\x1B";
                    break;

                case '\\':
                    js += "\\\\";
                    break;

                case '/':
                    // Avoid generating the sequence "</script" in JS code
                    if (!compat::Has(options_.unsupported_features, compat::JSFeature::kInlineScript) && i >= 2 &&
                        text[i - 2] == '<' && i + 6 <= n) {
                        static constexpr char script[] = "script";
                        bool matches = true;
                        for (int j = 0; j < 6; j++) {
                            char16_t a = text[i + static_cast<size_t>(j)];
                            uint16_t b = static_cast<uint16_t>(script[j]);
                            if (a >= 'A' && a <= 'Z') {
                                a = static_cast<char16_t>(a + ('a' - 'A'));
                            }
                            if (a != b) {
                                matches = false;
                                break;
                            }
                        }
                        if (matches) {
                            js += '\\';
                        }
                    }
                    js += '/';
                    break;

                case '\'':
                    if (quote == '\'') {
                        js += '\\';
                    }
                    js += '\'';
                    break;

                case '"':
                    if (quote == '"') {
                        js += '\\';
                    }
                    js += '"';
                    break;

                case '`':
                    if (quote == '`') {
                        js += '\\';
                    }
                    js += '`';
                    break;

                case '$':
                    if (quote == '`' && i < n && text[i] == '{') {
                        js += '\\';
                    }
                    js += '$';
                    break;

                case 0x2028:
                    js += "\\u2028";
                    break;

                case 0x2029:
                    js += "\\u2029";
                    break;

                case 0xFEFF:
                    js += "\\uFEFF";
                    break;

                default:
                    // Common case: just append a single byte
                    if (c <= kLastASCII) {
                        js += static_cast<char>(c);

                    // Is this a high surrogate?
                    } else if (c >= kFirstHighSurrogate && c <= kLastHighSurrogate) {
                        // Is there a next character?
                        if (i < n) {
                            char16_t c2 = text[i];

                            // Is it a low surrogate?
                            if (c2 >= kFirstLowSurrogate && c2 <= kLastLowSurrogate) {
                                char32_t r = (static_cast<char32_t>(c) << 10) + static_cast<char32_t>(c2) +
                                    (0x10000 - (static_cast<char32_t>(kFirstHighSurrogate) << 10) - kFirstLowSurrogate);
                                i++;

                                // Escape this character if UTF-8 isn't allowed
                                if (options_.ascii_only) {
                                    if (!compat::Has(options_.unsupported_features, compat::JSFeature::kUnicodeEscapes)) {
                                        AppendUnicodeEscape(js, r);
                                    } else {
                                        AppendHex4(js, static_cast<uint32_t>(c));
                                        AppendHex4(js, static_cast<uint32_t>(c2));
                                    }
                                    continue;
                                }

                                // Otherwise, encode to UTF-8
                                int width = helpers::EncodeWTF8Rune(temp, r);
                                js.append(temp, static_cast<size_t>(width));
                                continue;
                            }
                        }

                        // Write an unpaired high surrogate
                        AppendHex4(js, static_cast<uint32_t>(c));

                    // Is this an unpaired low surrogate or four-digit hex escape?
                    } else if ((c >= kFirstLowSurrogate && c <= kLastLowSurrogate) || (options_.ascii_only && c > 0xFF)) {
                        AppendHex4(js, static_cast<uint32_t>(c));

                    // Can this be a two-digit hex escape?
                    } else if (options_.ascii_only) {
                        js += '\\';
                        js += 'x';
                        js += kHexChars[(c >> 4) & 15];
                        js += kHexChars[c & 15];

                    // Otherwise, just encode to UTF-8
                    } else {
                        int width = helpers::EncodeWTF8Rune(temp, static_cast<char32_t>(c));
                        js.append(temp, static_cast<size_t>(width));
                    }
                    break;
                }
            }
        }

        // Prints a JSX tag name.  Supports member expressions (e.g., Foo.Bar),
        // string tags, and bare identifiers.  JSX tags can't use character escapes,
        // so non-ASCII identifiers are always printed as UTF-8.
        void printer::print_jsx_tag(const Expr& tag_or_nil) {
            if (const auto* str = Get<EString>(tag_or_nil.data)) {
                add_source_mapping(tag_or_nil.loc);
                print(helpers::UTF16ToString(str->value));

            } else if (const auto* ident = Get<EIdentifier>(tag_or_nil.data)) {
                std::string name = renamer_.NameForSymbol(ident->ref);
                add_source_mapping_for_name(tag_or_nil.loc, name, ident->ref);
                print(name);

            } else if (const auto* dot = Get<EDot>(tag_or_nil.data)) {
                print_jsx_tag(dot->target);
                print(".");
                add_source_mapping(dot->name_loc);
                print(dot->name);

            } else {
                if (!IsNil(tag_or_nil.data)) {
                    print_expr(tag_or_nil, L::kLowest, print_expr_flags::kNone);
                }
            }
        }

        // -- Binding printing (destructuring patterns, parameters, etc.) --
        // Recursively prints a binding node, which can be an identifier, missing,
        // array pattern (with optional defaults and rest), or object pattern.
        // Handles single-line vs multi-line formatting based on AST hints.
        void printer::print_binding(const Binding& binding) {
            const B& bdata = binding.data;
            if (const auto* missing = Get<BMissing>(bdata)) {
                (void)missing;
                add_source_mapping(binding.loc);

            } else if (const auto* ident = Get<BIdentifier>(bdata)) {
                std::string name = renamer_.NameForSymbol(ident->ref);
                print_space_before_identifier();
                add_source_mapping_for_name(binding.loc, name, ident->ref);
                print_identifier(name);

            } else if (const auto* arr = Get<BArray>(bdata)) {
                bool is_multi_line =
                    (arr->items.size() > 0 && !arr->is_single_line) || will_print_expr_comments_at_loc(arr->close_bracket_loc);
                if (!options_.minify_whitespace && !is_multi_line) {
                    for (const ArrayBinding& item : arr->items) {
                        if (will_print_expr_comments_at_loc(item.loc)) {
                            is_multi_line = true;
                            break;
                        }
                    }
                }
                add_source_mapping(binding.loc);
                print("[");
                if (arr->items.size() > 0 || is_multi_line) {
                    if (is_multi_line) {
                        indent_++;
                    }

                    for (size_t i = 0; i < arr->items.size(); i++) {
                        if (i != 0) {
                            print(",");
                        }
                        if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                            if (is_multi_line) {
                                print_newline();
                                print_indent();
                            } else if (i != 0) {
                                print_space();
                            }
                        }
                        print_expr_comments_at_loc(arr->items[i].loc);
                        if (arr->has_spread && i + 1 == arr->items.size()) {
                            add_source_mapping(arr->items[i].loc);
                            print("...");
                            print_expr_comments_at_loc(arr->items[i].binding.loc);
                        }
                        print_binding(arr->items[i].binding);

                        if (!IsNil(arr->items[i].default_value_or_nil.data)) {
                            print_space();
                            print("=");
                            print_space();
                            print_expr_without_leading_newline(arr->items[i].default_value_or_nil, L::kComma,
                                                               print_expr_flags::kNone);
                        }

                        // Make sure there's a comma after trailing missing items
                        if (Get<BMissing>(arr->items[i].binding.data) && i == arr->items.size() - 1) {
                            print(",");
                        }
                    }

                    if (is_multi_line) {
                        print_newline();
                        print_expr_comments_after_close_token_at_loc(arr->close_bracket_loc);
                        indent_--;
                        print_indent();
                    }
                }
                add_source_mapping(arr->close_bracket_loc);
                print("]");

            } else if (const auto* obj = Get<BObject>(bdata)) {
                bool is_multi_line = (obj->properties.size() > 0 && !obj->is_single_line) ||
                    will_print_expr_comments_at_loc(obj->close_brace_loc);
                if (!options_.minify_whitespace && !is_multi_line) {
                    for (const PropertyBinding& property : obj->properties) {
                        if (will_print_expr_comments_at_loc(property.loc)) {
                            is_multi_line = true;
                            break;
                        }
                    }
                }
                add_source_mapping(binding.loc);
                print("{");
                if (obj->properties.size() > 0 || is_multi_line) {
                    if (is_multi_line) {
                        indent_++;
                    }

                    for (size_t i = 0; i < obj->properties.size(); i++) {
                        if (i != 0) {
                            print(",");
                        }
                        if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                            if (is_multi_line) {
                                print_newline();
                                print_indent();
                            } else {
                                print_space();
                            }
                        }

                        const PropertyBinding& property = obj->properties[i];
                        print_expr_comments_at_loc(property.loc);

                        if (property.is_spread) {
                            add_source_mapping(property.loc);
                            print("...");
                            print_expr_comments_at_loc(property.value.loc);
                        } else {
                            if (property.is_computed) {
                                add_source_mapping(property.loc);
                                bool inner_multi_line = will_print_expr_comments_at_loc(property.key.loc) ||
                                    will_print_expr_comments_at_loc(property.close_bracket_loc);
                                print("[");
                                if (inner_multi_line) {
                                    print_newline();
                                    indent_++;
                                    print_indent();
                                }
                                print_expr(property.key, L::kComma, print_expr_flags::kNone);
                                if (inner_multi_line) {
                                    print_newline();
                                    print_expr_comments_after_close_token_at_loc(property.close_bracket_loc);
                                    indent_--;
                                    print_indent();
                                }
                                if (property.close_bracket_loc.start > property.loc.start) {
                                    add_source_mapping(property.close_bracket_loc);
                                }
                                print("]:");
                                print_space();
                                print_binding(property.value);

                                if (!IsNil(property.default_value_or_nil.data)) {
                                    print_space();
                                    print("=");
                                    print_space();
                                    print_expr_without_leading_newline(property.default_value_or_nil, L::kComma,
                                                                       print_expr_flags::kNone);
                                }
                                continue;
                            }

                            if (const auto* str = Get<EString>(property.key.data);
                                str && !property.prefer_quoted_key &&
                                can_print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()))) {
                                // Use a shorthand property if the names are the same
                                if (const auto* id = Get<BIdentifier>(property.value.data);
                                    id && !will_print_expr_comments_at_loc(property.value.loc) &&
                                    helpers::UTF16EqualsString(str->value, renamer_.NameForSymbol(id->ref))) {
                                    if (options_.add_source_mappings) {
                                        add_source_mapping_for_name(property.key.loc, helpers::UTF16ToString(str->value),
                                                                    id->ref);
                                    }
                                    print_identifier_utf16(std::span<const uint16_t>(
                                        reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()));
                                    if (!IsNil(property.default_value_or_nil.data)) {
                                        print_space();
                                        print("=");
                                        print_space();
                                        print_expr_without_leading_newline(property.default_value_or_nil, L::kComma,
                                                                           print_expr_flags::kNone);
                                    }
                                    continue;
                                }

                                add_source_mapping(property.key.loc);
                                print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()));
                            } else if (const auto* mangled = Get<ENameOfSymbol>(property.key.data)) {
                                std::string name = mangled_prop_name(mangled->ref);
                                if (can_print_identifier(name)) {
                                    add_source_mapping_for_name(property.key.loc, name, mangled->ref);
                                    print_identifier(name);

                                    // Use a shorthand property if the names are the same
                                    if (const auto* id = Get<BIdentifier>(property.value.data);
                                        id && !will_print_expr_comments_at_loc(property.value.loc) &&
                                        name == renamer_.NameForSymbol(id->ref)) {
                                        if (!IsNil(property.default_value_or_nil.data)) {
                                            print_space();
                                            print("=");
                                            print_space();
                                            print_expr_without_leading_newline(property.default_value_or_nil, L::kComma,
                                                                               print_expr_flags::kNone);
                                        }
                                        continue;
                                    }
                                } else {
                                    add_source_mapping(property.key.loc);
                                    print_quoted_utf8(name, print_quoted_flags::kNone);
                                }
                            } else {
                                print_expr(property.key, L::kLowest, print_expr_flags::kNone);
                            }

                            print(":");
                            print_space();
                        }
                        print_binding(property.value);

                        if (!IsNil(property.default_value_or_nil.data)) {
                            print_space();
                            print("=");
                            print_space();
                            print_expr_without_leading_newline(property.default_value_or_nil, L::kComma,
                                                               print_expr_flags::kNone);
                        }
                    }

                    if (is_multi_line) {
                        print_newline();
                        print_expr_comments_after_close_token_at_loc(obj->close_brace_loc);
                        indent_--;
                        print_indent();
                    } else {
                        // This block is only reached if len(b.Properties) > 0
                        print_space();
                    }
                }
                add_source_mapping(obj->close_brace_loc);
                print("}");
            }
        }

        // Prints function arguments: (arg1, arg2, ...rest).  In minified mode,
        // single-argument arrow functions omit the parentheses.
        void printer::print_fn_args(const std::vector<Arg>& args, const fn_args_opts& opts) {
            bool wrap = true;

            // Minify "(a) => {}" as "a=>{}"
            if (options_.minify_whitespace && !opts.has_rest_arg && opts.is_arrow && args.size() == 1) {
                if (const auto* id = Get<BIdentifier>(args[0].binding.data);
                    id && IsNil(args[0].default_or_nil.data)) {
                    wrap = false;
                }
            }

            if (wrap) {
                if (opts.add_mapping_for_open_paren_loc) {
                    add_source_mapping(opts.open_paren_loc);
                }
                print("(");
            }

            for (size_t i = 0; i < args.size(); i++) {
                if (i != 0) {
                    print(",");
                    print_space();
                }
                print_decorators(args[i].decorators, print_after_decorator::kSpace);
                if (opts.has_rest_arg && i + 1 == args.size()) {
                    print("...");
                }
                print_binding(args[i].binding);

                if (!IsNil(args[i].default_or_nil.data)) {
                    print_space();
                    print("=");
                    print_space();
                    print_expr_without_leading_newline(args[i].default_or_nil, L::kComma, print_expr_flags::kNone);
                }
            }

            if (wrap) {
                print(")");
            }
        }

        // Prints a function's parameter list and body block: (args) { body }
        void printer::print_fn(const Fn& fn) {
            print_fn_args(fn.args, fn_args_opts{/*open_paren_loc=*/{}, /*add_mapping_for_open_paren_loc=*/false,
                                                /*has_rest_arg=*/fn.has_rest_arg, /*is_arrow=*/false});
            print_space();
            print_block(fn.body.loc, fn.body.block);
        }

        // Prints decorator annotations (@decorator).  Determines whether each
        // decorator needs parenthesization (for complex member/index expressions).
        // Returns true if the last decorator uses space-separation (affecting
        // whether the following indent should be omitted).
        bool printer::print_decorators(const std::vector<Decorator>& decorators, print_after_decorator default_mode) {
            print_after_decorator old_mode = default_mode;

            for (const Decorator& decorator : decorators) {
                bool wrap = false;
                bool was_call_target = false;
                Expr expr = decorator.value;
                print_after_decorator mode = default_mode;
                if (decorator.omit_newline_after) {
                    mode = print_after_decorator::kSpace;
                }

                for (;;) {
                    bool is_call_target = was_call_target;
                    was_call_target = false;

                    if (Get<EIdentifier>(expr.data)) {
                        // "@foo"
                        break;

                    } else if (const auto* call = Get<ECall>(expr.data)) {
                        // "@foo()"
                        expr = call->target;
                        was_call_target = true;
                        continue;

                    } else if (const auto* dot = Get<EDot>(expr.data)) {
                        // "@foo.bar"
                        if (can_print_identifier(dot->name)) {
                            expr = dot->target;
                            continue;
                        }

                        // "@foo.\u30FF" => "@(foo['\u30FF'])"
                        wrap = true;
                        break;

                    } else if (const auto* index = Get<EIndex>(expr.data)) {
                        if (Get<EPrivateIdentifier>(index->index.data)) {
                            // "@foo.#bar"
                            expr = index->target;
                            continue;
                        }

                        // "@(foo[bar])"
                        wrap = true;
                        break;

                    } else if (const auto* import_id = Get<EImportIdentifier>(expr.data)) {
                        compiler::Ref ref = compiler::FollowSymbols(symbols_, import_id->ref);
                        const compiler::Symbol* symbol = symbols_.Get(ref);

                        if (symbol->import_item_status == compiler::ImportItemStatus::kMissing) {
                            // "@(void 0)"
                            wrap = true;
                            break;
                        }

                        if (symbol->namespace_alias != nullptr && is_call_target && import_id->was_originally_identifier) {
                            // "@((0, import_ns.fn)())"
                            wrap = true;
                            break;
                        }

                        if (auto it = options_.const_values.find(ref);
                            it != options_.const_values.end() && it->second.kind != ConstValueKind::kNone) {
                            // "@(<inlined constant>)"
                            wrap = true;
                            break;
                        }

                        // "@foo"
                        // "@import_ns.fn"
                        break;

                    } else {
                        // "@(foo + bar)"
                        // "@(() => {})"
                        wrap = true;
                        break;
                    }
                }

                add_source_mapping(decorator.at_loc);
                if (old_mode == print_after_decorator::kNewline) {
                    print_indent();
                }

                print("@");
                if (wrap) {
                    print("(");
                }
                print_expr(decorator.value, L::kLowest, print_expr_flags::kNone);
                if (wrap) {
                    print(")");
                }

                switch (mode) {
                case print_after_decorator::kNewline:
                    print_newline();
                    break;

                case print_after_decorator::kSpace:
                    print_space();
                    break;
                }
                old_mode = mode;
            }

            return old_mode == print_after_decorator::kSpace;
        }

        // Prints a block statement: { stmt1; stmt2; ... }
        // Increments indentation for the body, then decrements.
        void printer::print_block(logger::Loc loc, const SBlock& block) {
            add_source_mapping(loc);
            print("{");
            print_newline();

            indent_++;
            for (const Stmt& stmt : block.stmts) {
                print_semicolon_if_needed();
                print_stmt(stmt, print_stmt_flags::kCanOmitStatement);
            }
            indent_--;
            needs_semicolon_ = false;

            print_indent();
            if (block.close_brace_loc.start > loc.start) {
                add_source_mapping(block.close_brace_loc);
            }
            print("}");
        }

        // Prints a class body: extends Foo { ... }
        // Handles static blocks, method definitions, accessors, and fields.
        void printer::print_class(const Class& class_) {
            if (!IsNil(class_.extends_or_nil.data)) {
                print(" extends");
                print_space();
                print_expr(class_.extends_or_nil, static_cast<L>(static_cast<int>(L::kNew) - 1),
                           print_expr_flags::kNone);
            }
            print_space();

            add_source_mapping(class_.body_loc);
            print("{");
            print_newline();
            indent_++;

            for (const Property& item : class_.properties) {
                print_semicolon_if_needed();
                bool omit_indent = print_decorators(item.decorators, print_after_decorator::kNewline);
                if (!omit_indent) {
                    print_indent();
                }

                if (item.kind == PropertyKind::kClassStaticBlock) {
                    add_source_mapping(item.loc);
                    print("static");
                    print_space();
                    print_block(item.class_static_block->loc, item.class_static_block->block);
                    print_newline();
                    continue;
                }

                print_property(item);

                // Need semicolons after class fields
                if (IsNil(item.value_or_nil.data)) {
                    print_semicolon_after_statement();
                } else {
                    print_newline();
                }
            }

            needs_semicolon_ = false;
            print_expr_comments_after_close_token_at_loc(class_.close_brace_loc);
            indent_--;
            print_indent();
            if (class_.close_brace_loc.start > class_.body_loc.start) {
                add_source_mapping(class_.close_brace_loc);
            }
            print("}");
        }

        // Prints a single class/object property (key-value pair, method, getter/setter,
        // spread, computed property, etc.).  Handles shorthand property optimization,
        // computed property bracket notation, and static/async/generator modifiers.
        void printer::print_property(const Property& property_) {
            print_expr_comments_at_loc(property_.loc);

            if (property_.kind == PropertyKind::kSpread) {
                add_source_mapping(property_.loc);
                print("...");
                print_expr(property_.value_or_nil, L::kComma, print_expr_flags::kNone);
                return;
            }

            // Handle key syntax compression for cross-module constant inlining of enums
            Property property = property_;
            print_expr_flags key_flags{print_expr_flags::kNone};
            if (options_.minify_syntax && Has(property.flags, PropertyFlags::kIsComputed)) {
                property.key = late_constant_fold_unary_or_binary_or_if_expr(property.key);
                key_flags = key_flags | print_expr_flags::kParentWasUnaryOrBinaryOrIfTest;

                if (const auto* inlined = Get<EInlinedEnum>(property.key.data)) {
                    property.key = inlined->value;
                }

                // Remove the computed flag if it's no longer needed
                if (Get<ENumber>(property.key.data)) {
                    property.flags = Clear(property.flags, PropertyFlags::kIsComputed);
                } else if (const auto* str = Get<EString>(property.key.data)) {
                    if (!helpers::UTF16EqualsString(str->value, "__proto__") &&
                        !helpers::UTF16EqualsString(str->value, "constructor") &&
                        !helpers::UTF16EqualsString(str->value, "prototype")) {
                        property.flags = Clear(property.flags, PropertyFlags::kIsComputed);
                    }
                }
            }

            if (Has(property.flags, PropertyFlags::kIsStatic)) {
                print_space_before_identifier();
                add_source_mapping(property.loc);
                print("static");
                print_space();
            }

            switch (property.kind) {
            case PropertyKind::kGetter:
                print_space_before_identifier();
                add_source_mapping(property.loc);
                print("get");
                print_space();
                break;

            case PropertyKind::kSetter:
                print_space_before_identifier();
                add_source_mapping(property.loc);
                print("set");
                print_space();
                break;

            case PropertyKind::kAutoAccessor:
                print_space_before_identifier();
                add_source_mapping(property.loc);
                print("accessor");
                print_space();
                break;

            default:
                break;
            }

            if (const auto* fn = Get<EFunction>(property.value_or_nil.data); IsMethodDefinition(property.kind) && fn) {
                if (fn->fn.is_async) {
                    print_space_before_identifier();
                    add_source_mapping(property.loc);
                    print("async");
                    print_space();
                }
                if (fn->fn.is_generator) {
                    add_source_mapping(property.loc);
                    print("*");
                }
            }

            bool is_computed = Has(property.flags, PropertyFlags::kIsComputed);

            // Automatically print numbers that would cause a syntax error as computed properties
            if (!is_computed) {
                if (const auto* number = Get<ENumber>(property.key.data)) {
                    if (std::signbit(number->value) || (number->value == kPositiveInfinity && options_.minify_syntax)) {
                        // "{ -1: 0 }" must be printed as "{ [-1]: 0 }"
                        // "{ 1/0: 0 }" must be printed as "{ [1/0]: 0 }"
                        is_computed = true;
                    }
                }
            }

            if (is_computed) {
                add_source_mapping(property.loc);
                bool is_multi_line = will_print_expr_comments_at_loc(property.key.loc) ||
                    will_print_expr_comments_at_loc(property.close_bracket_loc);
                print("[");
                if (is_multi_line) {
                    print_newline();
                    indent_++;
                    print_indent();
                }
                print_expr(property.key, L::kComma, key_flags);
                if (is_multi_line) {
                    print_newline();
                    print_expr_comments_after_close_token_at_loc(property.close_bracket_loc);
                    indent_--;
                    print_indent();
                }
                if (property.close_bracket_loc.start > property.loc.start) {
                    add_source_mapping(property.close_bracket_loc);
                }
                print("]");

                if (!IsNil(property.value_or_nil.data)) {
                    if (const auto* fn = Get<EFunction>(property.value_or_nil.data);
                        IsMethodDefinition(property.kind) && fn) {
                        print_fn(fn->fn);
                        return;
                    }

                    print(":");
                    print_space();
                    print_expr_without_leading_newline(property.value_or_nil, L::kComma, print_expr_flags::kNone);
                }

                if (!IsNil(property.initializer_or_nil.data)) {
                    print_space();
                    print("=");
                    print_space();
                    print_expr_without_leading_newline(property.initializer_or_nil, L::kComma,
                                                       print_expr_flags::kNone);
                }
                return;
            }

            if (const auto* private_id = Get<EPrivateIdentifier>(property.key.data)) {
                std::string name = renamer_.NameForSymbol(private_id->ref);
                add_source_mapping_for_name(property.key.loc, name, private_id->ref);
                print_identifier(name);

            } else if (const auto* mangled = Get<ENameOfSymbol>(property.key.data)) {
                std::string name = mangled_prop_name(mangled->ref);
                if (can_print_identifier(name)) {
                    print_space_before_identifier();
                    add_source_mapping_for_name(property.key.loc, name, mangled->ref);
                    print_identifier(name);

                    // Use a shorthand property if the names are the same
                    if (!compat::Has(options_.unsupported_features, compat::JSFeature::kObjectExtensions) &&
                        !IsNil(property.value_or_nil.data) &&
                        !will_print_expr_comments_at_loc(property.value_or_nil.loc)) {
                        if (const auto* id = Get<EIdentifier>(property.value_or_nil.data)) {
                            if (name == renamer_.NameForSymbol(id->ref)) {
                                if (!IsNil(property.initializer_or_nil.data)) {
                                    print_space();
                                    print("=");
                                    print_space();
                                    print_expr_without_leading_newline(property.initializer_or_nil, L::kComma,
                                                                       print_expr_flags::kNone);
                                }
                                return;
                            }
                        } else if (const auto* import_id = Get<EImportIdentifier>(property.value_or_nil.data)) {
                            // Make sure we're not using a property access instead of an identifier
                            compiler::Ref ref = compiler::FollowSymbols(symbols_, import_id->ref);
                            const compiler::Symbol* symbol = symbols_.Get(ref);
                            if (symbol->namespace_alias == nullptr && name == renamer_.NameForSymbol(ref) &&
                                (options_.const_values.find(ref) == options_.const_values.end() ||
                                 options_.const_values.find(ref)->second.kind == ConstValueKind::kNone)) {
                                if (!IsNil(property.initializer_or_nil.data)) {
                                    print_space();
                                    print("=");
                                    print_space();
                                    print_expr_without_leading_newline(property.initializer_or_nil, L::kComma,
                                                                       print_expr_flags::kNone);
                                }
                                return;
                            }
                        }
                    }
                } else {
                    add_source_mapping(property.key.loc);
                    print_quoted_utf8(name, print_quoted_flags::kNone);
                }

            } else if (const auto* str = Get<EString>(property.key.data)) {
                if (!Has(property.flags, PropertyFlags::kPreferQuotedKey) &&
                    can_print_identifier_utf16(std::span<const uint16_t>(reinterpret_cast<const uint16_t*>(
                                                  str->value.data()),
                                                  str->value.size()))) {
                    print_space_before_identifier();

                    // Use a shorthand property if the names are the same
                    if (!compat::Has(options_.unsupported_features, compat::JSFeature::kObjectExtensions) &&
                        !IsNil(property.value_or_nil.data) &&
                        !will_print_expr_comments_at_loc(property.value_or_nil.loc)) {
                        if (const auto* id = Get<EIdentifier>(property.value_or_nil.data)) {
                            if (can_use_shorthand_property(str->value, renamer_.NameForSymbol(id->ref),
                                                           property.flags)) {
                                if (options_.add_source_mappings) {
                                    add_source_mapping_for_name(property.key.loc, helpers::UTF16ToString(str->value),
                                                                id->ref);
                                }
                                print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()));
                                if (!IsNil(property.initializer_or_nil.data)) {
                                    print_space();
                                    print("=");
                                    print_space();
                                    print_expr_without_leading_newline(property.initializer_or_nil, L::kComma,
                                                                       print_expr_flags::kNone);
                                }
                                return;
                            }
                        } else if (const auto* import_id = Get<EImportIdentifier>(property.value_or_nil.data)) {
                            // Make sure we're not using a property access instead of an identifier
                            compiler::Ref ref = compiler::FollowSymbols(symbols_, import_id->ref);
                            const compiler::Symbol* symbol = symbols_.Get(ref);
                            if (symbol->namespace_alias == nullptr &&
                                can_use_shorthand_property(str->value, renamer_.NameForSymbol(ref),
                                                           property.flags) &&
                                (options_.const_values.find(ref) == options_.const_values.end() ||
                                 options_.const_values.find(ref)->second.kind == ConstValueKind::kNone)) {
                                if (options_.add_source_mappings) {
                                    add_source_mapping_for_name(property.key.loc, helpers::UTF16ToString(str->value),
                                                                ref);
                                }
                                print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()));
                                if (!IsNil(property.initializer_or_nil.data)) {
                                    print_space();
                                    print("=");
                                    print_space();
                                    print_expr_without_leading_newline(property.initializer_or_nil, L::kComma,
                                                                       print_expr_flags::kNone);
                                }
                                return;
                            }
                        }
                    }

                    // The JavaScript specification special-cases the property identifier
                    // "__proto__" with a colon after it to set the prototype of the object.
                    if (Has(property.flags, PropertyFlags::kWasShorthand) &&
                        !compat::Has(options_.unsupported_features, compat::JSFeature::kObjectExtensions) &&
                        helpers::UTF16EqualsString(str->value, "__proto__")) {
                        print("[");
                        add_source_mapping(property.key.loc);
                        print_quoted_utf16(str->value, print_quoted_flags::kNone);
                        print("]");
                    } else {
                        add_source_mapping(property.key.loc);
                        print_identifier_utf16(std::span<const uint16_t>(
                            reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()));
                    }
                } else {
                    add_source_mapping(property.key.loc);
                    print_quoted_utf16(str->value, print_quoted_flags::kNone);
                }

            } else {
                print_expr(property.key, L::kLowest, key_flags);
            }

            if (const auto* fn = Get<EFunction>(property.value_or_nil.data); IsMethodDefinition(property.kind) && fn) {
                print_fn(fn->fn);
                return;
            }

            if (!IsNil(property.value_or_nil.data)) {
                print(":");
                print_space();
                print_expr_without_leading_newline(property.value_or_nil, L::kComma, print_expr_flags::kNone);
            }

            if (!IsNil(property.initializer_or_nil.data)) {
                print_space();
                print("=");
                print_space();
                print_expr_without_leading_newline(property.initializer_or_nil, L::kComma, print_expr_flags::kNone);
            }
        }

        bool can_use_shorthand_property(std::span<const char16_t> key, std::string_view name, PropertyFlags flags) {
            // The JavaScript specification special-cases the property identifier
            // "__proto__" with a colon after it to set the prototype of the object.
            if (!helpers::UTF16EqualsString(key, name)) {
                return false;
            }
            return name != "__proto__" || Has(flags, PropertyFlags::kWasShorthand);
        }

        // Prints `.then(() =>` or `.then(function() {` depending on whether
        // arrow functions are supported.  Returns the nesting level for the
        // callback body.
        L printer::print_dot_then_prefix() {
            L result;
            if (compat::Has(options_.unsupported_features, compat::JSFeature::kArrow)) {
                print(".then(function()");
                print_space();
                print("{");
                print_newline();
                indent_++;
                print_indent();
                print("return");
                print_space();
                result = L::kLowest;
            } else {
                print(".then(()");
                print_space();
                print("=>");
                print_space();
                result = L::kComma;
            }
            return result;
        }

        // Closes the `.then(...)` wrapper started by print_dot_then_prefix.
        void printer::print_dot_then_suffix() {
            if (compat::Has(options_.unsupported_features, compat::JSFeature::kArrow)) {
                if (!options_.minify_whitespace) {
                    print(";");
                }
                print_newline();
                indent_--;
                print_indent();
                print("})");
            } else {
                print(")");
            }
        }

        // Prints `undefined` as `void 0` (or `(void 0)` when precedence requires).
        void printer::print_undefined(logger::Loc loc, L level) {
            if (level >= L::kPrefix) {
                add_source_mapping(loc);
                print("(void 0)");
            } else {
                print_space_before_identifier();
                add_source_mapping(loc);
                print("void 0");
            }
        }

        // -- Expression simplification / inlining --
        // Attempts to simplify an unused expression at print time.  Handles:
        //   - Empty function calls: foo() -> (args) when foo is marked empty
        //   - Identity function calls: id(x) -> x
        //   - Comma operator chains containing empty calls
        // Input: an expression node -> simplified expression (or original if no change)
        Expr printer::simplify_unused_expr(const Expr& expr) {
            const E& e = expr.data;
            if (const auto* binary = Get<EBinary>(e)) {
                // Calls to be inlined may be hidden inside a comma operator chain
                if (binary->op == OpCode::kBinOpComma) {
                    Expr left = simplify_unused_expr(binary->left);
                    Expr right = simplify_unused_expr(binary->right);
                    if (left.data != binary->left.data || right.data != binary->right.data) {
                        return javascript::JoinWithComma(left, right);
                    }
                }
            } else if (const auto* call = Get<ECall>(e)) {
                compiler::SymbolFlags symbol_flags{};
                if (const auto* target = Get<EIdentifier>(call->target.data)) {
                    symbol_flags = symbols_.Get(target->ref)->flags;
                } else if (const auto* import_target = Get<EImportIdentifier>(call->target.data)) {
                    compiler::Ref ref = compiler::FollowSymbols(symbols_, import_target->ref);
                    symbol_flags = symbols_.Get(ref)->flags;
                }

                // Replace non-mutated empty functions with their arguments at print time
                if ((symbol_flags &
                        (compiler::SymbolFlags::kIsEmptyFunction | compiler::SymbolFlags::kCouldPotentiallyBeMutated)) ==
                    compiler::SymbolFlags::kIsEmptyFunction) {
                    Expr replacement{};
                    for (const Expr& arg : call->args) {
                        if (!Get<ESpread>(arg.data)) {
                            replacement = javascript::JoinWithComma(
                                replacement, ast_helpers_.SimplifyUnusedExpr(simplify_unused_expr(arg),
                                                                             options_.unsupported_features));
                        } else {
                            Expr wrapped_arg = arg;
                            wrapped_arg.data = std::make_shared<EArray>(
                                EArray{/*items=*/{arg}, /*comma_after_spread=*/{}, /*close_bracket_loc=*/{},
                                       /*is_single_line=*/true});
                            replacement = javascript::JoinWithComma(replacement, ast_helpers_.SimplifyUnusedExpr(
                                                                                     simplify_unused_expr(wrapped_arg),
                                                                                     options_.unsupported_features));
                        }
                    }
                    return replacement; // Don't add "undefined" here because the result isn't used
                }

                // Inline non-mutated identity functions at print time
                if ((symbol_flags &
                        (compiler::SymbolFlags::kIsIdentityFunction | compiler::SymbolFlags::kCouldPotentiallyBeMutated)) ==
                        compiler::SymbolFlags::kIsIdentityFunction &&
                    call->args.size() == 1) {
                    const Expr& arg = call->args[0];
                    if (!Get<ESpread>(arg.data)) {
                        return ast_helpers_.SimplifyUnusedExpr(simplify_unused_expr(arg), options_.unsupported_features);
                    }
                }
            }
            return expr;
        }

        // Wraps an expression with `(0, expr)` to prevent behavior changes when
        // the expression has been substituted in place of another.  Needed for:
        //   - delete id(x) must not become delete x
        //   - id(x.y)() must not become x.y()
        Expr printer::guard_against_behavior_change_due_to_substitution(Expr expr, print_expr_flags flags) {
            bool wrap = false;

            if (Has(flags, print_expr_flags::kIsDeleteTarget)) {
                // "delete id(x)" must not become "delete x"
                // "delete (empty(), x)" must not become "delete x"
                if (const auto* binary = Get<EBinary>(expr.data); !binary || binary->op != OpCode::kBinOpComma) {
                    wrap = true;
                }
            } else if (Has(flags, print_expr_flags::kIsCallTargetOrTemplateTag)) {
                // "id(x.y)()" must not become "x.y()"
                // "id(x.y)``" must not become "x.y``"
                // "(empty(), x.y)()" must not become "x.y()"
                // "(empty(), eval)()" must not become "eval()"
                if (Get<EDot>(expr.data) || Get<EIndex>(expr.data)) {
                    wrap = true;
                } else if (Get<EIdentifier>(expr.data)) {
                    if (is_unbound_eval_identifier(expr)) {
                        wrap = true;
                    }
                }
            }

            if (wrap) {
                expr.data = std::make_shared<EBinary>(EBinary{
                    /*left=*/Expr{std::make_shared<ENumber>(ENumber{0.0}), expr.loc},
                    /*right=*/expr,
                    /*op=*/OpCode::kBinOpComma});
            }

            return expr;
        }

        // Performs late constant folding on unary/binary/if expressions.  This is a
        // second pass of constant folding (the first is in the parser) that cleans up
        // cross-module TypeScript enum references and bitwise operations on inlined
        // constants.  Operates recursively on sub-expressions.
        Expr printer::late_constant_fold_unary_or_binary_or_if_expr(Expr expr) {
            const E& e = expr.data;
            if (const auto* import_id = Get<EImportIdentifier>(e)) {
                compiler::Ref ref = compiler::FollowSymbols(symbols_, import_id->ref);
                auto it = options_.const_values.find(ref);
                if (it != options_.const_values.end() && it->second.kind != ConstValueKind::kNone) {
                    return ConstValueToExpr(expr.loc, it->second);
                }
            } else if (const auto* dot = Get<EDot>(e)) {
                if (auto [value, ok] = try_to_get_imported_enum_value(dot->target, dot->name); ok) {
                    Expr inlined_value;
                    if (value.is_string) {
                        inlined_value = Expr{std::make_shared<EString>(EString{value.string, {}}), expr.loc};
                    } else {
                        inlined_value = Expr{std::make_shared<ENumber>(ENumber{value.number}), expr.loc};
                    }

                    if (dot->name.find("*/") != std::string::npos) {
                        // Don't wrap with a comment
                        return inlined_value;
                    }

                    // Wrap with a comment
                    return Expr{std::make_shared<EInlinedEnum>(EInlinedEnum{inlined_value, dot->name}),
                                inlined_value.loc};
                }
            } else if (const auto* unary = Get<EUnary>(e)) {
                Expr value = late_constant_fold_unary_or_binary_or_if_expr(unary->value);

                // Only fold again if something chained
                if (value.data != unary->value.data) {
                    // Only fold certain operations (just like the parser)
                    if (auto v = javascript::ToNumberWithoutSideEffects(value.data)) {
                        switch (unary->op) {
                        case OpCode::kUnOpPos:
                            return Expr{std::make_shared<ENumber>(ENumber{*v}), expr.loc};
                        case OpCode::kUnOpNeg:
                            return Expr{std::make_shared<ENumber>(ENumber{-*v}), expr.loc};
                        case OpCode::kUnOpCpl:
                            return Expr{std::make_shared<ENumber>(
                                           ENumber{static_cast<double>(~javascript::ToInt32(*v))}),
                                       expr.loc};
                        default:
                            break;
                        }
                    }

                    // Don't mutate the original AST
                    expr.data = std::make_shared<EUnary>(EUnary{/*value=*/value, /*op=*/unary->op});
                }
            } else if (const auto* binary = Get<EBinary>(e)) {
                Expr left = late_constant_fold_unary_or_binary_or_if_expr(binary->left);
                Expr right = late_constant_fold_unary_or_binary_or_if_expr(binary->right);

                // Only fold again if something changed
                if (left.data != binary->left.data || right.data != binary->right.data) {
                    std::shared_ptr<EBinary> new_binary =
                        std::make_shared<EBinary>(EBinary{left, right, binary->op});

                    // Only fold certain operations (just like the parser)
                    if (javascript::ShouldFoldBinaryOperatorWhenMinifying(*new_binary)) {
                        if (auto result = javascript::FoldBinaryOperator(expr.loc, *new_binary)) {
                            return *result;
                        }
                    }

                    // Don't mutate the original AST
                    expr.data = new_binary;
                }
            } else if (const auto* if_expr = Get<EIf>(e)) {
                Expr test = late_constant_fold_unary_or_binary_or_if_expr(if_expr->test);

                // Only fold again if something changed
                if (test.data != if_expr->test.data) {
                    if (auto result = javascript::ToBooleanWithSideEffects(test.data);
                        result && result->second == SideEffects::kNoSideEffects) {
                        if (result->first) {
                            return late_constant_fold_unary_or_binary_or_if_expr(if_expr->yes);
                        } else {
                            return late_constant_fold_unary_or_binary_or_if_expr(if_expr->no);
                        }
                    }

                    // Don't mutate the original AST
                    expr.data = std::make_shared<EIf>(EIf{/*test=*/test, /*yes=*/if_expr->yes, /*no=*/if_expr->no});
                }
            }

            return expr;
        }

        // -- Expression predicate helpers --
        // Returns true for expressions that are safe to use as a "new" target or
        // in positions where a numeric constant or property access is syntactically
        // equivalent (e.g., for wrapping decisions).
        bool printer::is_identifier_or_numeric_constant_or_property_access(const Expr& expr) {
            const E& e = expr.data;
            if (Get<EIdentifier>(e) || Get<EDot>(e) || Get<EIndex>(e)) {
                return true;
            }
            if (const auto* number = Get<ENumber>(e)) {
                return (std::isinf(number->value) && number->value > 0) || std::isnan(number->value);
            }
            return false;
        }

        // Returns true if the expression is an unbound (global) identifier.
        bool printer::is_unbound_identifier(const Expr& expr) {
            if (const auto* id = Get<EIdentifier>(expr.data)) {
                compiler::Ref ref = compiler::FollowSymbols(symbols_, id->ref);
                return symbols_.Get(ref)->kind == compiler::SymbolKind::kUnbound;
            }
            return false;
        }

        // Returns true if the expression is specifically an unbound "eval" identifier.
        // Used to detect direct eval calls which need special wrapping.
        bool printer::is_unbound_eval_identifier(const Expr& value) {
            if (const auto* id = Get<EIdentifier>(value.data)) {
                compiler::Ref ref = compiler::FollowSymbols(symbols_, id->ref);
                const compiler::Symbol* symbol = symbols_.Get(ref);
                return symbol->kind == compiler::SymbolKind::kUnbound && symbol->original_name == "eval";
            }
            return false;
        }

        // -- Expression start position tracking --
        // These methods save/restore the positions of statement starts, export default
        // starts, arrow expression starts, and for-of init starts.  Used to determine
        // whether an expression needs parentheses at statement start.
        expr_start_flags printer::save_expr_start_flags() {
            expr_start_flags flags{expr_start_flags::kNone};
            int n = static_cast<int>(js_.size());
            if (stmt_start_ == n) {
                flags = flags | expr_start_flags::kStmtStart;
            }
            if (export_default_start_ == n) {
                flags = flags | expr_start_flags::kExportDefaultStart;
            }
            if (arrow_expr_start_ == n) {
                flags = flags | expr_start_flags::kArrowExprStart;
            }
            if (for_of_init_start_ == n) {
                flags = flags | expr_start_flags::kForOfInitStart;
            }
            return flags;
        }

        void printer::restore_expr_start_flags(expr_start_flags flags) {
            if (flags != expr_start_flags::kNone) {
                int n = static_cast<int>(js_.size());
                if (Has(flags, expr_start_flags::kStmtStart)) {
                    stmt_start_ = n;
                }
                if (Has(flags, expr_start_flags::kExportDefaultStart)) {
                    export_default_start_ = n;
                }
                if (Has(flags, expr_start_flags::kArrowExprStart)) {
                    arrow_expr_start_ = n;
                }
                if (Has(flags, expr_start_flags::kForOfInitStart)) {
                    for_of_init_start_ = n;
                }
            }
        }

        // -- Comment printing --
        // Prints an indented comment block.  For multi-line comments, re-indents
        // each line.  For single-line comments, appends a mandatory newline.
        // Escapes </script sequences to prevent HTML injection.
        void printer::print_indented_comment(std::string_view text) {
            // Avoid generating a comment containing the character sequence "</script"
            std::string escaped;
            if (!compat::Has(options_.unsupported_features, compat::JSFeature::kInlineScript)) {
                escaped = helpers::EscapeClosingTag(text, "script");
                text = escaped;
            }

            if (text.size() >= 2 && text[0] == '/' && text[1] == '*') {
                // Re-indent multi-line comments
                for (;;) {
                    size_t newline = text.find('\n');
                    if (newline == std::string_view::npos) {
                        break;
                    }
                    print(text.substr(0, newline + 1));
                    print_indent();
                    text = text.substr(newline + 1);
                }
                print(text);
                print_newline();
            } else {
                // Print a mandatory newline after single-line comments
                print(text);
                print("\n");
            }
        }

        // Returns true if there are unprinted expression comments at the given AST location.
        bool printer::will_print_expr_comments_at_loc(logger::Loc loc) {
            if (options_.minify_whitespace) {
                return false;
            }
            auto it = expr_comments_.find(loc);
            return it != expr_comments_.end() && !it->second.empty() &&
                printed_expr_comments_.find(loc) == printed_expr_comments_.end();
        }

        // Returns true if any of the given expressions has unprinted expression comments.
        bool printer::will_print_expr_comments_for_any_of(std::span<const Expr> exprs) {
            for (const Expr& expr : exprs) {
                if (will_print_expr_comments_at_loc(expr.loc)) {
                    return true;
                }
            }
            return false;
        }

        // Prints expression comments associated with a given AST location.
        // When no_leading_newline_here_ is set (e.g., after `return`), comments
        // are inlined to avoid ASI issues.  Otherwise, comments are printed on
        // their own indented lines.
        void printer::print_expr_comments_at_loc(logger::Loc loc) {
            if (options_.minify_whitespace) {
                return;
            }
            auto it = expr_comments_.find(loc);
            if (it == expr_comments_.end() || it->second.empty() ||
                printed_expr_comments_.find(loc) != printed_expr_comments_.end()) {
                return;
            }

            expr_start_flags flags = save_expr_start_flags();

            // We must never generate a newline before certain expressions. For
            // example, generating a newline before the expression in a "return"
            // statement will cause a semicolon to be inserted, which would
            // change the code's behavior.
            if (no_leading_newline_here_ == static_cast<int>(js_.size())) {
                for (const std::string& comment : it->second) {
                    if (comment.rfind("//", 0) == 0) {
                        print("/*");
                        print(std::string_view(comment).substr(2));
                        if (comment.rfind("// ", 0) == 0) {
                            print(" ");
                        }
                        print("*/");
                    } else {
                        std::string merged;
                        merged.reserve(comment.size());
                        for (char c : comment) {
                            if (c != '\n') {
                                merged.push_back(c);
                            }
                        }
                        print(merged);
                    }
                    print_space();
                }
            } else {
                for (const std::string& comment : it->second) {
                    print_indented_comment(comment);
                    print_indent();
                }
            }

            // Mark these comments as printed so we don't print them again
            printed_expr_comments_[loc] = true;

            restore_expr_start_flags(flags);
        }

        // Prints expression comments that appear after a closing token (e.g., after
        // a closing bracket or brace).  Used for comments that were positioned after
        // a token but are semantically associated with the enclosing expression.
        void printer::print_expr_comments_after_close_token_at_loc(logger::Loc loc) {
            auto it = expr_comments_.find(loc);
            if (it == expr_comments_.end() || it->second.empty() ||
                printed_expr_comments_.find(loc) != printed_expr_comments_.end()) {
                return;
            }

            expr_start_flags flags = save_expr_start_flags();

            for (const std::string& comment : it->second) {
                print_indent();
                print_indented_comment(comment);
            }

            // Mark these comments as printed so we don't print them again
            printed_expr_comments_[loc] = true;

            restore_expr_start_flags(flags);
        }

        // Prints an expression, but wraps it in parentheses if it has expression
        // comments that would otherwise introduce a leading newline (which could
        // change code behavior via ASI).
        void printer::print_expr_without_leading_newline(const Expr& expr, L level, print_expr_flags flags) {
            if (!options_.minify_whitespace && will_print_expr_comments_at_loc(expr.loc)) {
                print("(");
                print_newline();
                indent_++;
                print_indent();
                print_expr(expr, level, flags);
                print_newline();
                indent_--;
                print_indent();
                print(")");
                return;
            }

            no_leading_newline_here_ = static_cast<int>(js_.size());
            print_expr(expr, level, flags);
        }

        // -- Import/require printing helpers --

        // Prints the `assert` or `with` clause of an import statement:
        // { key: "value", ... }
        void printer::print_import_assert_or_with_clause(const compiler::ImportAssertOrWith& assert_or_with) {
            bool is_multi_line = will_print_expr_comments_at_loc(assert_or_with.inner_close_brace_loc);
            if (!is_multi_line) {
                for (const compiler::AssertOrWithEntry& entry : assert_or_with.entries) {
                    if (will_print_expr_comments_at_loc(entry.key_loc) ||
                        will_print_expr_comments_at_loc(entry.value_loc)) {
                        is_multi_line = true;
                        break;
                    }
                }
            }

            add_source_mapping(assert_or_with.inner_open_brace_loc);
            print("{");
            if (is_multi_line) {
                indent_++;
            }

            for (size_t i = 0; i < assert_or_with.entries.size(); i++) {
                if (i > 0) {
                    print(",");
                }
                if (is_multi_line) {
                    print_newline();
                    print_indent();
                } else {
                    print_space();
                }

                const compiler::AssertOrWithEntry& entry = assert_or_with.entries[i];

                print_expr_comments_at_loc(entry.key_loc);
                add_source_mapping(entry.key_loc);
                if (!entry.prefer_quoted_key &&
                    can_print_identifier_utf16(std::span<const uint16_t>(reinterpret_cast<const uint16_t*>(entry.key.data()),
                                                                         entry.key.size()))) {
                    print_space_before_identifier();
                    print_identifier_utf16(std::span<const uint16_t>(
                        reinterpret_cast<const uint16_t*>(entry.key.data()), entry.key.size()));
                } else {
                    print_quoted_utf16(entry.key, print_quoted_flags::kNone);
                }

                print(":");

                if (will_print_expr_comments_at_loc(entry.value_loc)) {
                    print_newline();
                    indent_++;
                    print_indent();
                    print_expr_comments_at_loc(entry.value_loc);
                    add_source_mapping(entry.value_loc);
                    print_quoted_utf16(entry.value, print_quoted_flags::kNone);
                    indent_--;
                } else {
                    print_space();
                    add_source_mapping(entry.value_loc);
                    print_quoted_utf16(entry.value, print_quoted_flags::kNone);
                }
            }

            if (is_multi_line) {
                print_newline();
                print_expr_comments_after_close_token_at_loc(assert_or_with.inner_close_brace_loc);
                indent_--;
                print_indent();
            } else if (assert_or_with.entries.size() > 0) {
                print_space();
            }

            add_source_mapping(assert_or_with.inner_close_brace_loc);
            print("}");
        }

        // Prints the second argument to dynamic import() for import assertions/attributes.
        // Omits the clause entirely if the feature is unsupported.
        void printer::print_import_call_assert_or_with(const std::shared_ptr<compiler::ImportAssertOrWith>& assert_or_with,
                                                       bool outer_is_multi_line) {
            // Omit import assertions/attributes if we know the "import()" syntax
            // doesn't support a second argument (i.e. both import assertions and
            // import attributes aren't supported) and doing so would cause a
            // syntax error
            if (assert_or_with == nullptr ||
                (compat::Has(options_.unsupported_features, compat::JSFeature::kImportAssertions) &&
                 compat::Has(options_.unsupported_features, compat::JSFeature::kImportAttributes))) {
                return;
            }

            bool is_multi_line = will_print_expr_comments_at_loc(assert_or_with->keyword_loc) ||
                will_print_expr_comments_at_loc(assert_or_with->inner_open_brace_loc) ||
                will_print_expr_comments_at_loc(assert_or_with->outer_close_brace_loc);

            print(",");
            if (outer_is_multi_line) {
                print_newline();
                print_indent();
            } else {
                print_space();
            }
            print_expr_comments_at_loc(assert_or_with->outer_open_brace_loc);
            add_source_mapping(assert_or_with->outer_open_brace_loc);
            print("{");

            if (is_multi_line) {
                print_newline();
                indent_++;
                print_indent();
            } else {
                print_space();
            }

            print_expr_comments_at_loc(assert_or_with->keyword_loc);
            add_source_mapping(assert_or_with->keyword_loc);
            print(compiler::AssertOrWithKeywordToString(assert_or_with->keyword));
            print(":");

            if (will_print_expr_comments_at_loc(assert_or_with->inner_open_brace_loc)) {
                print_newline();
                indent_++;
                print_indent();
                print_expr_comments_at_loc(assert_or_with->inner_open_brace_loc);
                print_import_assert_or_with_clause(*assert_or_with);
                indent_--;
            } else {
                print_space();
                print_import_assert_or_with_clause(*assert_or_with);
            }

            if (is_multi_line) {
                print_newline();
                print_expr_comments_after_close_token_at_loc(assert_or_with->outer_close_brace_loc);
                indent_--;
                print_indent();
            } else {
                print_space();
            }

            add_source_mapping(assert_or_with->outer_close_brace_loc);
            print("}");
        }

        // Returns true if the runtime import should be omitted (test mode only).
        bool printer::omit_runtime_import(uint32_t import_record_index) const {
            if (!options_.omit_runtime_for_tests) {
                return false;
            }
            const compiler::ImportRecord& record = import_records_[import_record_index];
            return record.source_index.IsValid() && record.source_index.GetIndex() == kRuntimeSourceIndex;
        }

        // Prints the path string for an import/require statement.
        // Optionally records the import in the metafile metadata.
        void printer::print_path(uint32_t import_record_index, compiler::ImportKind import_kind) {
            const compiler::ImportRecord& record = import_records_[import_record_index];
            add_source_mapping(record.range.loc);
            print_quoted_utf8(record.path.text, print_quoted_flags::kNoWrap);

            if (options_.needs_metafile) {
                std::string external;
                if (!compiler::Has(record.flags, compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile)) {
                    external = config::MaybeRemoveWhitespace(options_.metafile_format, ",\n          \"external\": true");
                }

                std::string entry_fmt = config::MaybeRemoveWhitespace(
                    options_.metafile_format, "\n        {\n          \"path\": %s,\n          \"kind\": %s%s\n        }");
                std::string path_json = helpers::QuoteForJSON(record.path.text, options_.ascii_only);
                std::string kind_json =
                    helpers::QuoteForJSON(compiler::ImportKindToStringForMetafile(import_kind), options_.ascii_only);

                std::string entry;
                entry.reserve(entry_fmt.size() + path_json.size() + kind_json.size() + external.size());
                std::vector<std::string_view> args{path_json, kind_json, external};
                size_t argi = 0;
                size_t pos = 0;
                for (;;) {
                    size_t pct = entry_fmt.find('%', pos);
                    if (pct == std::string::npos) {
                        entry.append(entry_fmt, pos, std::string::npos);
                        break;
                    }
                    entry.append(entry_fmt, pos, pct - pos);
                    if (pct + 1 < entry_fmt.size() && entry_fmt[pct + 1] == 's') {
                        if (argi < args.size()) {
                            entry += args[argi];
                        }
                        argi++;
                        pos = pct + 2;
                    } else {
                        entry += '%';
                        pos = pct + 1;
                    }
                }
                json_metadata_imports_.push_back(std::move(entry));
            }

            if (record.assert_or_with != nullptr && import_kind == compiler::ImportKind::kStmt) {
                compat::JSFeature feature = compat::JSFeature::kImportAttributes;
                if (record.assert_or_with->keyword == compiler::AssertOrWithKeyword::kAssert) {
                    feature = compat::JSFeature::kImportAssertions;
                }

                // Omit import assertions/attributes on this import statement if
                // they would cause a syntax error
                if (compat::Has(options_.unsupported_features, feature)) {
                    return;
                }

                print_space();
                add_source_mapping(record.assert_or_with->keyword_loc);
                print(compiler::AssertOrWithKeywordToString(record.assert_or_with->keyword));
                print_space();
                print_import_assert_or_with_clause(*record.assert_or_with);
            }
        }

        // Prints a require() or import() expression, handling both external and
        // internal (bundled) modules.  Applies __toESM / __toCommonJS wrappers
        // as needed.  Falls back to Promise.resolve().then() when dynamic import
        // is unsupported.
        void printer::print_require_or_import_expr(uint32_t import_record_index, L level, print_expr_flags flags,
                                                   logger::Loc close_paren_loc, compiler::ImportPhase phase) {
            const compiler::ImportRecord& record = import_records_[import_record_index];

            bool wrap_in_parens = level >= L::kNew || Has(flags, print_expr_flags::kIsNewTarget);
            if (wrap_in_parens) {
                print("(");
                level = L::kLowest;
            }

            if (!record.source_index.IsValid()) {
                // External "require()"
                if (record.kind != compiler::ImportKind::kDynamic) {
                    // Wrap this with a call to "__toESM()" if this is a CommonJS file
                    bool wrap_with_to_esm = compiler::Has(record.flags, compiler::ImportRecordFlags::kWrapWithToESM);
                    if (wrap_with_to_esm) {
                        print_space_before_identifier();
                        print_identifier(renamer_.NameForSymbol(options_.to_esm_ref));
                        print("(");
                    }

                    // Potentially substitute our own "__require" stub for "require"
                    print_space_before_identifier();
                    if (compiler::Has(record.flags, compiler::ImportRecordFlags::kCallRuntimeRequire)) {
                        print_identifier(renamer_.NameForSymbol(options_.runtime_require_ref));
                    } else {
                        print("require");
                    }

                    bool is_multi_line = will_print_expr_comments_at_loc(record.range.loc) ||
                        will_print_expr_comments_at_loc(close_paren_loc);
                    print("(");
                    if (is_multi_line) {
                        print_newline();
                        indent_++;
                        print_indent();
                    }
                    print_expr_comments_at_loc(record.range.loc);
                    print_path(import_record_index, compiler::ImportKind::kRequire);
                    if (is_multi_line) {
                        print_newline();
                        print_expr_comments_after_close_token_at_loc(close_paren_loc);
                        indent_--;
                        print_indent();
                    }
                    if (close_paren_loc.start > record.range.loc.start) {
                        add_source_mapping(close_paren_loc);
                    }
                    print(")");

                    // Finish the call to "__toESM()"
                    if (wrap_with_to_esm) {
                        if (IsESM(module_type_)) {
                            print(",");
                            print_space();
                            print("1");
                        }
                        print(")");
                    }
                    if (wrap_in_parens) {
                        print(")");
                    }
                    return;
                }

                // External "import()"
                compiler::ImportKind kind = compiler::ImportKind::kDynamic;
                bool wrap_with_to_esm = false;
                if (!compat::Has(options_.unsupported_features, compat::JSFeature::kDynamicImport)) {
                    print_space_before_identifier();
                    switch (phase) {
                    case compiler::ImportPhase::kDefer:
                        print("import.defer(");
                        break;
                    case compiler::ImportPhase::kSource:
                        print("import.source(");
                        break;
                    default:
                        print("import(");
                        break;
                    }
                } else {
                    kind = compiler::ImportKind::kRequire;
                    print_space_before_identifier();
                    print("Promise.resolve()");
                    print_dot_then_prefix();

                    // Wrap this with a call to "__toESM()" if this is a CommonJS file
                    wrap_with_to_esm = compiler::Has(record.flags, compiler::ImportRecordFlags::kWrapWithToESM);
                    if (wrap_with_to_esm) {
                        print_space_before_identifier();
                        print_identifier(renamer_.NameForSymbol(options_.to_esm_ref));
                        print("(");
                    }

                    // Potentially substitute our own "__require" stub for "require"
                    print_space_before_identifier();
                    if (compiler::Has(record.flags, compiler::ImportRecordFlags::kCallRuntimeRequire)) {
                        print_identifier(renamer_.NameForSymbol(options_.runtime_require_ref));
                    } else {
                        print("require");
                    }

                    print("(");
                }

                bool is_multi_line = will_print_expr_comments_at_loc(record.range.loc) ||
                    will_print_expr_comments_at_loc(close_paren_loc) ||
                    (record.assert_or_with != nullptr &&
                     !compat::Has(options_.unsupported_features, compat::JSFeature::kDynamicImport) &&
                     (!compat::Has(options_.unsupported_features, compat::JSFeature::kImportAssertions) ||
                      !compat::Has(options_.unsupported_features, compat::JSFeature::kImportAttributes)) &&
                     will_print_expr_comments_at_loc(record.assert_or_with->outer_open_brace_loc));
                if (is_multi_line) {
                    print_newline();
                    indent_++;
                    print_indent();
                }
                print_expr_comments_at_loc(record.range.loc);
                print_path(import_record_index, kind);
                if (!compat::Has(options_.unsupported_features, compat::JSFeature::kDynamicImport)) {
                    print_import_call_assert_or_with(record.assert_or_with, is_multi_line);
                }
                if (is_multi_line) {
                    print_newline();
                    print_expr_comments_after_close_token_at_loc(close_paren_loc);
                    indent_--;
                    print_indent();
                }
                if (close_paren_loc.start > record.range.loc.start) {
                    add_source_mapping(close_paren_loc);
                }
                print(")");

                if (compat::Has(options_.unsupported_features, compat::JSFeature::kDynamicImport)) {
                    // Finish the call to "__toESM()"
                    if (wrap_with_to_esm) {
                        if (IsESM(module_type_)) {
                            print(",");
                            print_space();
                            print("1");
                        }
                        print(")");
                    }
                    print_dot_then_suffix();
                }
                if (wrap_in_parens) {
                    print(")");
                }
                return;
            }

            RequireOrImportMeta meta = options_.require_or_import_meta_for_source(record.source_index.GetIndex());

            // Don't need the namespace object if the result is unused anyway
            if (Has(flags, print_expr_flags::kExprResultIsUnused)) {
                meta.exports_ref = compiler::Ref{};
            }

            // Internal "import()" of async ESM
            if (record.kind == compiler::ImportKind::kDynamic && meta.is_wrapper_async) {
                print_space_before_identifier();
                print_identifier(renamer_.NameForSymbol(meta.wrapper_ref));
                print("()");
                if (meta.exports_ref != compiler::Ref{}) {
                    print_dot_then_prefix();
                    print_space_before_identifier();
                    print_identifier(renamer_.NameForSymbol(meta.exports_ref));
                    print_dot_then_suffix();
                }
                if (wrap_in_parens) {
                    print(")");
                }
                return;
            }

            // Internal "require()" or "import()"
            if (record.kind == compiler::ImportKind::kDynamic) {
                print_space_before_identifier();
                print("Promise.resolve()");
                level = print_dot_then_prefix();
            }

            // Make sure the comma operator is properly wrapped
            bool wrap_comma = meta.exports_ref != compiler::Ref{} && level >= L::kComma;
            if (wrap_comma) {
                print("(");
            }

            // Wrap this with a call to "__toESM()" if this is a CommonJS file
            bool wrap_with_to_esm = compiler::Has(record.flags, compiler::ImportRecordFlags::kWrapWithToESM);
            if (wrap_with_to_esm) {
                print_space_before_identifier();
                print_identifier(renamer_.NameForSymbol(options_.to_esm_ref));
                print("(");
            }

            // Call the wrapper
            print_space_before_identifier();
            print_identifier(renamer_.NameForSymbol(meta.wrapper_ref));
            print("()");

            // Return the namespace object if this is an ESM file
            if (meta.exports_ref != compiler::Ref{}) {
                print(",");
                print_space();

                // Wrap this with a call to "__toCommonJS()" if this is an ESM file
                bool wrap_with_to_cjs = compiler::Has(record.flags, compiler::ImportRecordFlags::kWrapWithToCJS);
                if (wrap_with_to_cjs) {
                    print_identifier(renamer_.NameForSymbol(options_.to_commonjs_ref));
                    print("(");
                }
                print_identifier(renamer_.NameForSymbol(meta.exports_ref));
                if (wrap_with_to_cjs) {
                    print(")");
                }
            }

            // Finish the call to "__toESM()"
            if (wrap_with_to_esm) {
                if (IsESM(module_type_)) {
                    print(",");
                    print_space();
                    print("1");
                }
                print(")");
            }

            if (wrap_comma) {
                print(")");
            }

            if (record.kind == compiler::ImportKind::kDynamic) {
                print_dot_then_suffix();
            }

            if (wrap_in_parens) {
                print(")");
            }
        }

        // -- Binary expression printing (iterative) --
        // Prepares a binary expression visitor for printing.  Handles:
        //   - Comma operator simplification
        //   - Parenthesization based on precedence levels
        //   - Destructuring assignment wrapping
        //   - Nullish coalescing / power operator edge cases
        //   - Private identifier "in" operator special case
        // Returns true if normal left/right printing should proceed, false if
        // the expression was handled as a special case.
        bool printer::check_and_prepare(binary_expr_visitor& v) {
            const EBinary* e = v.e;

            // If this is a comma operator then either the result is unused (and
            // we should have already simplified unused expressions), or the
            // result is used (and we can still simplify unused expressions
            // inside the left operand)
            if (e->op == OpCode::kBinOpComma) {
                if (!Has(v.flags, print_expr_flags::kDidAlreadySimplifyUnusedExprs)) {
                    Expr left = simplify_unused_expr(e->left);
                    Expr right = e->right;
                    if (Has(v.flags, print_expr_flags::kExprResultIsUnused)) {
                        right = simplify_unused_expr(right);
                    }
                    if (left.data != e->left.data || right.data != e->right.data) {
                        // Pass a flag so we don't needlessly re-simplify the same expression
                        print_expr(guard_against_behavior_change_due_to_substitution(
                                         javascript::JoinWithComma(left, right), v.flags),
                                     v.level, v.flags | print_expr_flags::kDidAlreadySimplifyUnusedExprs);
                        return false;
                    }
                } else {
                    // Pass a flag so we don't needlessly re-simplify the same expression
                    v.flags = v.flags | print_expr_flags::kDidAlreadySimplifyUnusedExprs;
                }
            }

            v.entry = kOpTable[static_cast<size_t>(e->op)];
            v.wrap = v.level >= v.entry.level ||
                (e->op == OpCode::kBinOpIn && Has(v.flags, print_expr_flags::kForbidIn));

            // Destructuring assignments must be parenthesized
            if (int n = static_cast<int>(js_.size()); stmt_start_ == n || arrow_expr_start_ == n) {
                if (Get<EObject>(e->left.data)) {
                    v.wrap = true;
                }
            }

            if (v.wrap) {
                print("(");
                v.flags = Clear(v.flags, print_expr_flags::kForbidIn);
            }

            v.left_level = static_cast<L>(static_cast<int>(v.entry.level) - 1);
            v.right_level = static_cast<L>(static_cast<int>(v.entry.level) - 1);

            if (IsRightAssociative(e->op)) {
                v.left_level = v.entry.level;
            }
            if (IsLeftAssociative(e->op)) {
                v.right_level = v.entry.level;
            }

            switch (e->op) {
            case OpCode::kBinOpNullishCoalescing:
                // "??" can't directly contain "||" or "&&" without being wrapped
                // in parentheses
                if (const auto* left = Get<EBinary>(e->left.data);
                    left && (left->op == OpCode::kBinOpLogicalOr || left->op == OpCode::kBinOpLogicalAnd)) {
                    v.left_level = L::kPrefix;
                }
                if (const auto* right = Get<EBinary>(e->right.data);
                    right && (right->op == OpCode::kBinOpLogicalOr || right->op == OpCode::kBinOpLogicalAnd)) {
                    v.right_level = L::kPrefix;
                }
                break;

            case OpCode::kBinOpPow:
                // "**" can't contain certain unary expressions
                if (const auto* left = Get<EUnary>(e->left.data);
                    left && UnaryAssignTarget(left->op) == AssignTarget::kNone) {
                    v.left_level = L::kCall;
                } else if (Get<EAwait>(e->left.data)) {
                    v.left_level = L::kCall;
                } else if (Get<EUndefined>(e->left.data)) {
                    // Undefined is printed as "void 0"
                    v.left_level = L::kCall;
                } else if (Get<ENumber>(e->left.data)) {
                    // Negative numbers are printed using a unary operator
                    v.left_level = L::kCall;
                } else if (options_.minify_syntax) {
                    // When minifying, booleans are printed as "!0 and "!1"
                    if (Get<EBoolean>(e->left.data)) {
                        v.left_level = L::kCall;
                    }
                }
                break;

            default:
                break;
            }

            // Special-case "#foo in bar"
            if (const auto* private_id = Get<EPrivateIdentifier>(e->left.data);
                private_id && e->op == OpCode::kBinOpIn) {
                std::string name = renamer_.NameForSymbol(private_id->ref);
                add_source_mapping_for_name(e->left.loc, name, private_id->ref);
                print_identifier(name);
                visit_right_and_finish(v);
                return false;
            }

            if (e->op == OpCode::kBinOpComma) {
                // The result of the left operand of the comma operator is unused
                v.left_flags = (v.flags & print_expr_flags::kForbidIn) | print_expr_flags::kExprResultIsUnused |
                    print_expr_flags::kParentWasUnaryOrBinaryOrIfTest;
            } else {
                v.left_flags = (v.flags & print_expr_flags::kForbidIn) |
                    print_expr_flags::kParentWasUnaryOrBinaryOrIfTest;
            }
            return true;
        }

        // Prints the operator and right-hand side of a binary expression,
        // then closes any wrapping parentheses.  Called after the left side
        // has been printed (either by check_and_prepare's caller or by the
        // iterative binary expression loop).
        void printer::visit_right_and_finish(binary_expr_visitor& v) {
            const EBinary* e = v.e;

            if (e->op != OpCode::kBinOpComma) {
                print_space();
            }

            if (v.entry.is_keyword) {
                print_space_before_identifier();
                print(v.entry.text);
            } else {
                print_space_before_operator(e->op);
                print(v.entry.text);
                prev_op_ = e->op;
                prev_op_end_ = static_cast<int>(js_.size());
            }

            if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                print_space();
            }

            if (e->op == OpCode::kBinOpComma) {
                // The result of the right operand of the comma operator is unused
                // if the caller doesn't use it
                print_expr(e->right, v.right_level,
                             (v.flags & (print_expr_flags::kForbidIn | print_expr_flags::kExprResultIsUnused)) |
                                 print_expr_flags::kParentWasUnaryOrBinaryOrIfTest);
            } else {
                print_expr(e->right, v.right_level,
                             (v.flags & print_expr_flags::kForbidIn) |
                                 print_expr_flags::kParentWasUnaryOrBinaryOrIfTest);
            }

            if (v.wrap) {
                print(")");
            }
        }

        // =============================================================================
        // print_expr — the main expression serialization dispatcher
        // =============================================================================
        // This is the heart of the printer.  It takes an expression AST node,
        // a precedence level, and flags, then emits the JavaScript source text.
        //
        // The `level` parameter controls parenthesization: if the current operator
        // precedence is lower than `level`, the expression is wrapped in parentheses.
        //
        // Handles all expression types:
        //   Literals:  numbers, strings, booleans, null, bigint, regexp, template literals
        //   Identifiers: local, import, eval, new.target, import.meta
        //   Operators: unary, binary (iterative), ternary, assignment, yield, await
        //   Calls:     function calls, new expressions, tagged templates
        //   Access:    dot, index, optional chaining
        //   Structures: arrow functions, classes, arrays, objects, JSX
        //   Special:   spread, inlined enums, require/import expressions
        void printer::print_expr(Expr expr, L level, print_expr_flags flags) {
            // If syntax compression is enabled, do a pre-pass over unary and
            // binary operators to inline bitwise operations of cross-module
            // inlined constants.
            if (options_.minify_syntax && !Has(flags, print_expr_flags::kParentWasUnaryOrBinaryOrIfTest)) {
                const E& ed = expr.data;
                if (Get<EUnary>(ed) || Get<EBinary>(ed) || Get<EIf>(ed)) {
                    expr = late_constant_fold_unary_or_binary_or_if_expr(expr);
                }
            }

            print_expr_comments_at_loc(expr.loc);

            const E& e = expr.data;
            if (Get<EMissing>(e)) {
                add_source_mapping(expr.loc);

            } else if (const auto* annotation = Get<EAnnotation>(e)) {
                print_expr(annotation->value, level, flags);

            } else if (Get<EUndefined>(e)) {
                print_undefined(expr.loc, level);

            } else if (Get<ESuper>(e)) {
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("super");

            } else if (Get<ENull>(e)) {
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("null");

            } else if (Get<EThis>(e)) {
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("this");

            } else if (const auto* spread = Get<ESpread>(e)) {
                add_source_mapping(expr.loc);
                print("...");
                print_expr(spread->value, L::kComma, print_expr_flags::kNone);

            } else if (Get<ENewTarget>(e)) {
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("new.target");

            } else if (Get<EImportMeta>(e)) {
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("import.meta");

            } else if (const auto* name_of_symbol = Get<ENameOfSymbol>(e)) {
                std::string name = mangled_prop_name(name_of_symbol->ref);
                add_source_mapping_for_name(expr.loc, name, name_of_symbol->ref);

                if (!options_.minify_whitespace && name_of_symbol->has_property_key_comment) {
                    print("/* @__KEY__ */ ");
                }

                print_quoted_utf8(name, print_quoted_flags::kAllowBacktick);

            } else if (const auto* jsx_element = Get<EJSXElement>(e)) {
                // Start the opening tag
                add_source_mapping(expr.loc);
                print("<");
                print_jsx_tag(jsx_element->tag_or_nil);
                if (!jsx_element->is_tag_single_line) {
                    indent_++;
                }

                // Print the attributes
                for (const Property& property : jsx_element->properties) {
                    if (jsx_element->is_tag_single_line) {
                        print_space();
                    } else {
                        print_newline();
                        print_indent();
                    }

                    if (property.kind == PropertyKind::kSpread) {
                        if (will_print_expr_comments_at_loc(property.loc)) {
                            print("{");
                            print_newline();
                            indent_++;
                            print_indent();
                            print_expr_comments_at_loc(property.loc);
                            print("...");
                            print_expr(property.value_or_nil, L::kComma, print_expr_flags::kNone);
                            print_newline();
                            indent_--;
                            print_indent();
                            print("}");
                        } else {
                            print("{...");
                            print_expr(property.value_or_nil, L::kComma, print_expr_flags::kNone);
                            print("}");
                        }
                        continue;
                    }

                    print_space_before_identifier();
                    if (const auto* mangled = Get<ENameOfSymbol>(property.key.data)) {
                        std::string name = mangled_prop_name(mangled->ref);
                        add_source_mapping_for_name(property.key.loc, name, mangled->ref);
                        print_identifier(name);
                    } else if (const auto* str = Get<EString>(property.key.data)) {
                        add_source_mapping(property.key.loc);
                        print(helpers::UTF16ToString(str->value));
                    } else {
                        print("{...{");
                        print_space();
                        print("[");
                        print_expr(property.key, L::kComma, print_expr_flags::kNone);
                        print("]:");
                        print_space();
                        print_expr(property.value_or_nil, L::kComma, print_expr_flags::kNone);
                        print_space();
                        print("}}");
                        continue;
                    }

                    bool is_multi_line = will_print_expr_comments_at_loc(property.value_or_nil.loc);

                    if (Has(property.flags, PropertyFlags::kWasShorthand)) {
                        // Implicit "true" value
                        if (const auto* boolean = Get<EBoolean>(property.value_or_nil.data); boolean && boolean->value) {
                            continue;
                        }

                        // JSX element as JSX attribute value
                        if (Get<EJSXElement>(property.value_or_nil.data)) {
                            print("=");
                            print_expr(property.value_or_nil, L::kLowest, print_expr_flags::kNone);
                            continue;
                        }
                    }

                    // Special-case raw text
                    if (const auto* text = Get<EJSXText>(property.value_or_nil.data)) {
                        print("=");
                        add_source_mapping(property.value_or_nil.loc);
                        print(text->raw);
                        continue;
                    }

                    // Generic JS value
                    print("={");
                    if (is_multi_line) {
                        print_newline();
                        indent_++;
                        print_indent();
                    }
                    print_expr(property.value_or_nil, L::kComma, print_expr_flags::kNone);
                    if (is_multi_line) {
                        print_newline();
                        indent_--;
                        print_indent();
                    }
                    print("}");
                }

                // End the opening tag
                if (!jsx_element->is_tag_single_line) {
                    indent_--;
                    if (jsx_element->properties.size() > 0) {
                        print_newline();
                        print_indent();
                    }
                }
                if (!IsNil(jsx_element->tag_or_nil.data) && jsx_element->nullable_children.size() == 0) {
                    if (jsx_element->is_tag_single_line || jsx_element->properties.size() == 0) {
                        print_space();
                    }
                    add_source_mapping(jsx_element->close_loc);
                    print("/>");
                    return;
                }
                print(">");

                // Print the children
                for (const Expr& child_or_nil : jsx_element->nullable_children) {
                    if (Get<EJSXElement>(child_or_nil.data)) {
                        print_expr(child_or_nil, L::kLowest, print_expr_flags::kNone);
                    } else if (const auto* text = Get<EJSXText>(child_or_nil.data)) {
                        add_source_mapping(child_or_nil.loc);
                        print(text->raw);
                    } else if (!IsNil(child_or_nil.data)) {
                        bool is_multi_line = will_print_expr_comments_at_loc(child_or_nil.loc);
                        print("{");
                        if (is_multi_line) {
                            print_newline();
                            indent_++;
                            print_indent();
                        }
                        print_expr(child_or_nil, L::kComma, print_expr_flags::kNone);
                        if (is_multi_line) {
                            print_newline();
                            indent_--;
                            print_indent();
                        }
                        print("}");
                    } else {
                        print("{");
                        if (will_print_expr_comments_at_loc(child_or_nil.loc)) {
                            // Note: Some people use these comments for AST transformations
                            print_newline();
                            indent_++;
                            print_expr_comments_after_close_token_at_loc(child_or_nil.loc);
                            indent_--;
                            print_indent();
                        }
                        print("}");
                    }
                }

                // Print the closing tag
                add_source_mapping(jsx_element->close_loc);
                print("</");
                print_jsx_tag(jsx_element->tag_or_nil);
                print(">");

            } else if (const auto* new_expr = Get<ENew>(e)) {
                bool wrap = level >= L::kCall;

                bool has_pure_comment = !options_.minify_whitespace && new_expr->can_be_unwrapped_if_unused;
                if (has_pure_comment && level >= L::kPostfix) {
                    wrap = true;
                }

                if (wrap) {
                    print("(");
                }

                if (has_pure_comment) {
                    add_source_mapping(expr.loc);
                    print("/* @__PURE__ */ ");
                }

                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("new");
                print_space();
                print_expr(new_expr->target, L::kNew, print_expr_flags::kIsNewTarget);

                // Omit the "()" when minifying, but only when safe to do so
                bool is_multi_line = !options_.minify_whitespace &&
                    ((new_expr->is_multi_line && new_expr->args.size() > 0) ||
                     will_print_expr_comments_for_any_of(new_expr->args) ||
                     will_print_expr_comments_at_loc(new_expr->close_paren_loc));
                if (!options_.minify_whitespace || new_expr->args.size() > 0 || level >= L::kPostfix || is_multi_line) {
                    bool needs_newline = true;
                    print("(");
                    if (is_multi_line) {
                        indent_++;
                    }
                    for (size_t i = 0; i < new_expr->args.size(); i++) {
                        if (i != 0) {
                            print(",");
                        }
                        if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                            if (is_multi_line) {
                                if (needs_newline) {
                                    print_newline();
                                }
                                print_indent();
                            } else if (i != 0) {
                                print_space();
                            }
                        }
                        print_expr(new_expr->args[i], L::kComma, print_expr_flags::kNone);
                        needs_newline = true;
                    }
                    if (is_multi_line) {
                        if (needs_newline || will_print_expr_comments_at_loc(new_expr->close_paren_loc)) {
                            print_newline();
                        }
                        print_expr_comments_after_close_token_at_loc(new_expr->close_paren_loc);
                        indent_--;
                        print_indent();
                    }
                    if (new_expr->close_paren_loc.start > expr.loc.start) {
                        add_source_mapping(new_expr->close_paren_loc);
                    }
                    print(")");
                }

                if (wrap) {
                    print(")");
                }

            } else if (const auto* call = Get<ECall>(e)) {
                if (options_.minify_syntax) {
                    compiler::SymbolFlags symbol_flags{};
                    if (const auto* target = Get<EIdentifier>(call->target.data)) {
                        symbol_flags = symbols_.Get(target->ref)->flags;
                    } else if (const auto* import_target = Get<EImportIdentifier>(call->target.data)) {
                        compiler::Ref ref = compiler::FollowSymbols(symbols_, import_target->ref);
                        symbol_flags = symbols_.Get(ref)->flags;
                    }

                    // Replace non-mutated empty functions with their arguments at print time
                    if ((symbol_flags &
                            (compiler::SymbolFlags::kIsEmptyFunction | compiler::SymbolFlags::kCouldPotentiallyBeMutated)) ==
                        compiler::SymbolFlags::kIsEmptyFunction) {
                        Expr replacement{};
                        for (const Expr& arg : call->args) {
                            if (!Get<ESpread>(arg.data)) {
                                replacement = javascript::JoinWithComma(
                                    replacement, ast_helpers_.SimplifyUnusedExpr(arg, options_.unsupported_features));
                            } else {
                                Expr wrapped_arg = arg;
                                wrapped_arg.data = std::make_shared<EArray>(
                                    EArray{/*items=*/{arg}, /*comma_after_spread=*/{}, /*close_bracket_loc=*/{},
                                           /*is_single_line=*/true});
                                replacement = javascript::JoinWithComma(
                                    replacement, ast_helpers_.SimplifyUnusedExpr(wrapped_arg,
                                                                                 options_.unsupported_features));
                            }
                        }
                        if (IsNil(replacement.data) || !Has(flags, print_expr_flags::kExprResultIsUnused)) {
                            replacement = javascript::JoinWithComma(
                                replacement, Expr{kEUndefinedShared, expr.loc});
                        }
                        print_expr(guard_against_behavior_change_due_to_substitution(replacement, flags), level, flags);
                        return;
                    }

                    // Inline non-mutated identity functions at print time
                    if ((symbol_flags &
                            (compiler::SymbolFlags::kIsIdentityFunction | compiler::SymbolFlags::kCouldPotentiallyBeMutated)) ==
                            compiler::SymbolFlags::kIsIdentityFunction &&
                        call->args.size() == 1) {
                        Expr arg = call->args[0];
                        if (!Get<ESpread>(arg.data)) {
                            if (Has(flags, print_expr_flags::kExprResultIsUnused)) {
                                arg = ast_helpers_.SimplifyUnusedExpr(arg, options_.unsupported_features);
                                if (IsNil(arg.data)) {
                                    arg.data = kEUndefinedShared;
                                }
                            }
                            print_expr(guard_against_behavior_change_due_to_substitution(arg, flags), level, flags);
                            return;
                        }
                    }

                    // Inline IIFEs that return expressions at print time
                    if (call->args.size() == 0) {
                        // Note: Do not inline async arrow functions as they are
                        // not IIFEs.
                        if (const auto* arrow = Get<EArrow>(call->target.data);
                            arrow && arrow->args.size() == 0 && !arrow->is_async) {
                            const std::vector<Stmt>& stmts = arrow->body.block.stmts;

                            // "(() => {})()" => "void 0"
                            if (stmts.size() == 0) {
                                Expr value{kEUndefinedShared, expr.loc};
                                print_expr(guard_against_behavior_change_due_to_substitution(value, flags), level,
                                           flags);
                                return;
                            }

                            // "(() => 123)()" => "123"
                            if (stmts.size() == 1) {
                                if (const auto* stmt = Get<SReturn>(stmts[0].data)) {
                                    Expr value = stmt->value_or_nil;
                                    if (IsNil(value.data)) {
                                        value.data = kEUndefinedShared;
                                    }
                                    print_expr(guard_against_behavior_change_due_to_substitution(value, flags), level,
                                               flags);
                                    return;
                                }
                            }
                        }
                    }
                }

                bool wrap = level >= L::kNew || Has(flags, print_expr_flags::kIsNewTarget);
                print_expr_flags target_flags{print_expr_flags::kNone};
                if (call->optional_chain == OptionalChain::kNone) {
                    target_flags = print_expr_flags::kHasNonOptionalChainParent;
                } else if (Has(flags, print_expr_flags::kHasNonOptionalChainParent)) {
                    wrap = true;
                }

                bool has_pure_comment = !options_.minify_whitespace && call->can_be_unwrapped_if_unused;
                if (has_pure_comment && level >= L::kPostfix) {
                    wrap = true;
                }

                if (wrap) {
                    print("(");
                }

                if (has_pure_comment) {
                    expr_start_flags saved = save_expr_start_flags();
                    add_source_mapping(expr.loc);
                    print("/* @__PURE__ */ ");
                    restore_expr_start_flags(saved);
                }

                // We don't ever want to accidentally generate a direct eval expression here
                call_target_ = Get<EImportIdentifier>(call->target.data);
                if ((call->kind != CallKind::kDirectEval && is_unbound_eval_identifier(call->target) &&
                     call->optional_chain == OptionalChain::kNone) ||
                    (call->kind != CallKind::kTargetWasOriginallyPropertyAccess &&
                     javascript::IsPropertyAccess(call->target))) {
                    print("(0,");
                    print_space();
                    print_expr(call->target, L::kPostfix, print_expr_flags::kIsCallTargetOrTemplateTag);
                    print(")");
                } else {
                    print_expr(call->target, L::kPostfix,
                               print_expr_flags::kIsCallTargetOrTemplateTag | target_flags);
                }

                if (call->optional_chain == OptionalChain::kStart) {
                    print("?.");
                }

                bool is_multi_line = !options_.minify_whitespace &&
                    ((call->is_multi_line && call->args.size() > 0) ||
                     will_print_expr_comments_for_any_of(call->args) ||
                     will_print_expr_comments_at_loc(call->close_paren_loc));
                print("(");
                if (is_multi_line) {
                    indent_++;
                }
                for (size_t i = 0; i < call->args.size(); i++) {
                    if (i != 0) {
                        print(",");
                    }
                    if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                        if (is_multi_line) {
                            print_newline();
                            print_indent();
                        } else if (i != 0) {
                            print_space();
                        }
                    }
                    print_expr(call->args[i], L::kComma, print_expr_flags::kNone);
                }
                if (is_multi_line) {
                    print_newline();
                    print_expr_comments_after_close_token_at_loc(call->close_paren_loc);
                    indent_--;
                    print_indent();
                }
                if (call->close_paren_loc.start > expr.loc.start) {
                    add_source_mapping(call->close_paren_loc);
                }
                print(")");

                if (wrap) {
                    print(")");
                }

            } else if (const auto* require_string = Get<ERequireString>(e)) {
                add_source_mapping(expr.loc);
                print_require_or_import_expr(require_string->import_record_index, level, flags,
                                             require_string->close_paren_loc, compiler::ImportPhase::kEvaluation);

            } else if (const auto* require_resolve_string = Get<ERequireResolveString>(e)) {
                logger::Loc record_loc = import_records_[require_resolve_string->import_record_index].range.loc;
                bool is_multi_line = will_print_expr_comments_at_loc(record_loc) ||
                    will_print_expr_comments_at_loc(require_resolve_string->close_paren_loc);
                bool wrap = level >= L::kNew || Has(flags, print_expr_flags::kIsNewTarget);
                if (wrap) {
                    print("(");
                }
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("require.resolve(");
                if (is_multi_line) {
                    print_newline();
                    indent_++;
                    print_indent();
                    print_expr_comments_at_loc(record_loc);
                }
                print_path(require_resolve_string->import_record_index, compiler::ImportKind::kRequireResolve);
                if (is_multi_line) {
                    print_newline();
                    print_expr_comments_after_close_token_at_loc(require_resolve_string->close_paren_loc);
                    indent_--;
                    print_indent();
                }
                if (require_resolve_string->close_paren_loc.start > expr.loc.start) {
                    add_source_mapping(require_resolve_string->close_paren_loc);
                }
                print(")");
                if (wrap) {
                    print(")");
                }

            } else if (const auto* import_string = Get<EImportString>(e)) {
                add_source_mapping(expr.loc);
                print_require_or_import_expr(import_string->import_record_index, level, flags,
                                             import_string->close_paren_loc,
                                             import_records_[import_string->import_record_index].phase);

            } else if (const auto* import_call = Get<EImportCall>(e)) {
                // Only print the second argument if either import assertions or
                // import attributes are supported
                bool print_import_assert_or_with =
                    !IsNil(import_call->options_or_nil.data) &&
                    (!compat::Has(options_.unsupported_features, compat::JSFeature::kImportAssertions) ||
                     !compat::Has(options_.unsupported_features, compat::JSFeature::kImportAttributes));
                bool is_multi_line = !options_.minify_whitespace &&
                    (will_print_expr_comments_at_loc(import_call->expr.loc) ||
                     (print_import_assert_or_with && will_print_expr_comments_at_loc(import_call->options_or_nil.loc)) ||
                     will_print_expr_comments_at_loc(import_call->close_paren_loc));
                bool wrap = level >= L::kNew || Has(flags, print_expr_flags::kIsNewTarget);
                if (wrap) {
                    print("(");
                }
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                switch (import_call->phase) {
                case compiler::ImportPhase::kDefer:
                    print("import.defer(");
                    break;
                case compiler::ImportPhase::kSource:
                    print("import.source(");
                    break;
                default:
                    print("import(");
                    break;
                }
                if (is_multi_line) {
                    print_newline();
                    indent_++;
                    print_indent();
                }
                print_expr(import_call->expr, L::kComma, print_expr_flags::kNone);

                if (print_import_assert_or_with) {
                    print(",");
                    if (is_multi_line) {
                        print_newline();
                        print_indent();
                    } else {
                        print_space();
                    }
                    print_expr(import_call->options_or_nil, L::kComma, print_expr_flags::kNone);
                }

                if (is_multi_line) {
                    print_newline();
                    print_expr_comments_after_close_token_at_loc(import_call->close_paren_loc);
                    indent_--;
                    print_indent();
                }
                print(")");
                if (wrap) {
                    print(")");
                }

            } else if (const auto* dot = Get<EDot>(e)) {
                bool wrap = false;
                if (dot->optional_chain == OptionalChain::kNone) {
                    flags = flags | print_expr_flags::kHasNonOptionalChainParent;

                    // Inline cross-module TypeScript enum references here
                    if (auto [value, ok] = try_to_get_imported_enum_value(dot->target, dot->name); ok) {
                        if (value.is_string) {
                            print_quoted_utf16(value.string, print_quoted_flags::kAllowBacktick);
                        } else {
                            print_number(value.number, level);
                        }
                        if (!options_.minify_whitespace && !options_.minify_identifiers &&
                            dot->name.find("*/") == std::string::npos) {
                            print(" /* ");
                            print(dot->name);
                            print(" */");
                        }
                        return;
                    }
                } else {
                    if (Has(flags, print_expr_flags::kIsNewTarget | print_expr_flags::kHasNonOptionalChainParent)) {
                        wrap = true;
                        print("(");
                    }
                    flags = Clear(flags, print_expr_flags::kIsNewTarget | print_expr_flags::kHasNonOptionalChainParent);
                }
                print_expr(dot->target, L::kPostfix,
                           (flags & (print_expr_flags::kIsNewTarget | print_expr_flags::kHasNonOptionalChainParent)) |
                               print_expr_flags::kIsPropertyAccessTarget);
                if (can_print_identifier(dot->name)) {
                    if (dot->optional_chain != OptionalChain::kStart && need_space_before_dot_ == static_cast<int>(js_.size())) {
                        // "1.toString" is a syntax error, so print "1 .toString" instead
                        print(" ");
                    }
                    if (dot->optional_chain == OptionalChain::kStart) {
                        print("?.");
                    } else {
                        print(".");
                    }
                    if (options_.line_limit > 0) {
                        print_newline_past_line_limit();
                    }
                    add_source_mapping(dot->name_loc);
                    print_identifier(dot->name);
                } else {
                    if (dot->optional_chain == OptionalChain::kStart) {
                        print("?.");
                    }
                    print("[");
                    add_source_mapping(dot->name_loc);
                    print_quoted_utf8(dot->name, print_quoted_flags::kAllowBacktick);
                    print("]");
                }
                if (wrap) {
                    print(")");
                }

            } else if (const auto* index = Get<EIndex>(e)) {
                bool wrap = false;
                if (index->optional_chain == OptionalChain::kNone) {
                    flags = flags | print_expr_flags::kHasNonOptionalChainParent;

                    // Inline cross-module TypeScript enum references here
                    if (const auto* index_string = Get<EString>(index->index.data)) {
                        if (auto result = try_to_get_imported_enum_value_utf16(
                                index->target, std::span<const char16_t>(index_string->value.data(),
                                                                         index_string->value.size()));
                            result.found) {
                            if (result.value.is_string) {
                                print_quoted_utf16(result.value.string, print_quoted_flags::kAllowBacktick);
                            } else {
                                print_number(result.value.number, level);
                            }
                            if (!options_.minify_whitespace && !options_.minify_identifiers &&
                                result.name.find("*/") == std::string::npos) {
                                print(" /* ");
                                print(result.name);
                                print(" */");
                            }
                            return;
                        }
                    }
                } else {
                    if (Has(flags, print_expr_flags::kIsNewTarget | print_expr_flags::kHasNonOptionalChainParent)) {
                        wrap = true;
                        print("(");
                    }
                    flags = Clear(flags, print_expr_flags::kIsNewTarget | print_expr_flags::kHasNonOptionalChainParent);
                }
                print_expr(index->target, L::kPostfix,
                           (flags & (print_expr_flags::kIsNewTarget | print_expr_flags::kHasNonOptionalChainParent)) |
                               print_expr_flags::kIsPropertyAccessTarget);
                if (index->optional_chain == OptionalChain::kStart) {
                    print("?.");
                }

                const E& index_data = index->index.data;
                bool handled_index = false;
                if (const auto* private_id = Get<EPrivateIdentifier>(index_data)) {
                    if (index->optional_chain != OptionalChain::kStart) {
                        print(".");
                    }
                    std::string name = renamer_.NameForSymbol(private_id->ref);
                    add_source_mapping_for_name(index->index.loc, name, private_id->ref);
                    print_identifier(name);
                    handled_index = true;
                } else if (const auto* mangled = Get<ENameOfSymbol>(index_data)) {
                    if (std::string name = mangled_prop_name(mangled->ref); can_print_identifier(name)) {
                        if (index->optional_chain != OptionalChain::kStart) {
                            print(".");
                        }
                        add_source_mapping_for_name(index->index.loc, name, mangled->ref);
                        print_identifier(name);
                        handled_index = true;
                    }
                }
                if (!handled_index) {
                    if (const auto* inlined = Get<EInlinedEnum>(index_data)) {
                        if (options_.minify_syntax) {
                            if (const auto* str = Get<EString>(inlined->value.data);
                                str && can_print_identifier_utf16(std::span<const uint16_t>(
                                          reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()))) {
                                if (index->optional_chain != OptionalChain::kStart) {
                                    print(".");
                                }
                                add_source_mapping(index->index.loc);
                                print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(str->value.data()), str->value.size()));
                                if (wrap) {
                                    print(")");
                                }
                                return;
                            }
                        }
                    } else if (const auto* inner_dot = Get<EDot>(index_data)) {
                        if (options_.minify_syntax) {
                            if (auto [value, ok] = try_to_get_imported_enum_value(inner_dot->target, inner_dot->name);
                                ok && value.is_string &&
                                can_print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(value.string.data()), value.string.size()))) {
                                if (index->optional_chain != OptionalChain::kStart) {
                                    print(".");
                                }
                                add_source_mapping(index->index.loc);
                                print_identifier_utf16(std::span<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(value.string.data()), value.string.size()));
                                if (wrap) {
                                    print(")");
                                }
                                return;
                            }
                        }
                    }
                }

                if (!handled_index) {
                    bool is_multi_line = will_print_expr_comments_at_loc(index->index.loc) ||
                        will_print_expr_comments_at_loc(index->close_bracket_loc);
                    print("[");
                    if (is_multi_line) {
                        print_newline();
                        indent_++;
                        print_indent();
                    }
                    print_expr(index->index, L::kLowest, print_expr_flags::kNone);
                    if (is_multi_line) {
                        print_newline();
                        print_expr_comments_after_close_token_at_loc(index->close_bracket_loc);
                        indent_--;
                        print_indent();
                    }
                    if (index->close_bracket_loc.start > expr.loc.start) {
                        add_source_mapping(index->close_bracket_loc);
                    }
                    print("]");
                }
                if (wrap) {
                    print(")");
                }

            } else if (const auto* if_expr = Get<EIf>(e)) {
                bool wrap = level >= L::kConditional;
                if (wrap) {
                    print("(");
                    flags = Clear(flags, print_expr_flags::kForbidIn);
                }
                print_expr(if_expr->test, L::kConditional,
                           (flags & print_expr_flags::kForbidIn) | print_expr_flags::kParentWasUnaryOrBinaryOrIfTest);
                print_space();
                print("?");
                if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                    print_space();
                }
                print_expr_without_leading_newline(if_expr->yes, L::kYield, print_expr_flags::kNone);
                print_space();
                print(":");
                if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                    print_space();
                }
                print_expr_without_leading_newline(if_expr->no, L::kYield, flags & print_expr_flags::kForbidIn);
                if (wrap) {
                    print(")");
                }

            } else if (const auto* arrow = Get<EArrow>(e)) {
                bool wrap = arrow->is_parenthesized || level >= L::kAssign;

                if (wrap) {
                    print("(");
                }
                if (!options_.minify_whitespace && arrow->has_no_side_effects_comment) {
                    print("/* @__NO_SIDE_EFFECTS__ */ ");
                }
                if (arrow->is_async) {
                    add_source_mapping(expr.loc);
                    print_space_before_identifier();
                    print("async");
                    print_space();
                }

                print_fn_args(arrow->args,
                              fn_args_opts{/*open_paren_loc=*/expr.loc, /*add_mapping_for_open_paren_loc=*/!arrow->is_async,
                                           /*has_rest_arg=*/arrow->has_rest_arg, /*is_arrow=*/true});
                print_space();
                print("=>");
                print_space();

                bool was_printed = false;
                if (arrow->body.block.stmts.size() == 1 && arrow->prefer_expr) {
                    if (const auto* s = Get<SReturn>(arrow->body.block.stmts[0].data);
                        s && !IsNil(s->value_or_nil.data)) {
                        print_expr_flags nested_flags{print_expr_flags::kNone};
                        if (Has(flags, print_expr_flags::kForbidIn) && !wrap) {
                            nested_flags = nested_flags | print_expr_flags::kForbidIn;
                        }
                        arrow_expr_start_ = static_cast<int>(js_.size());
                        print_expr_without_leading_newline(s->value_or_nil, L::kComma, nested_flags);
                        was_printed = true;
                    }
                }
                if (!was_printed) {
                    print_block(arrow->body.loc, arrow->body.block);
                }
                if (wrap) {
                    print(")");
                }

            } else if (const auto* function = Get<EFunction>(e)) {
                int n = static_cast<int>(js_.size());
                bool wrap = function->is_parenthesized || stmt_start_ == n || export_default_start_ == n ||
                    (Has(flags, print_expr_flags::kIsPropertyAccessTarget) &&
                     compat::Has(options_.unsupported_features, compat::JSFeature::kFunctionOrClassPropertyAccess));
                if (wrap) {
                    print("(");
                }
                if (!options_.minify_whitespace && function->fn.has_no_side_effects_comment) {
                    print("/* @__NO_SIDE_EFFECTS__ */ ");
                }
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                if (function->fn.is_async) {
                    print("async ");
                }
                print("function");
                if (function->fn.is_generator) {
                    print("*");
                    print_space();
                }
                if (function->fn.name != nullptr) {
                    print_space_before_identifier();
                    std::string name = renamer_.NameForSymbol(function->fn.name->ref);
                    add_source_mapping_for_name(function->fn.name->loc, name, function->fn.name->ref);
                    print_identifier(name);
                }
                print_fn(function->fn);
                if (wrap) {
                    print(")");
                }

            } else if (const auto* class_expr = Get<EClass>(e)) {
                int n = static_cast<int>(js_.size());
                bool wrap = stmt_start_ == n || export_default_start_ == n ||
                    (Has(flags, print_expr_flags::kIsPropertyAccessTarget) &&
                     compat::Has(options_.unsupported_features, compat::JSFeature::kFunctionOrClassPropertyAccess));
                if (wrap) {
                    print("(");
                }
                print_decorators(class_expr->class_.decorators, print_after_decorator::kSpace);
                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("class");
                if (class_expr->class_.name != nullptr) {
                    print(" ");
                    std::string name = renamer_.NameForSymbol(class_expr->class_.name->ref);
                    add_source_mapping_for_name(class_expr->class_.name->loc, name, class_expr->class_.name->ref);
                    print_identifier(name);
                }
                print_class(class_expr->class_);
                if (wrap) {
                    print(")");
                }

            } else if (const auto* array = Get<EArray>(e)) {
                bool is_multi_line = (array->items.size() > 0 && !array->is_single_line) ||
                    will_print_expr_comments_for_any_of(array->items) ||
                    will_print_expr_comments_at_loc(array->close_bracket_loc);
                add_source_mapping(expr.loc);
                print("[");
                if (array->items.size() > 0 || is_multi_line) {
                    if (is_multi_line) {
                        indent_++;
                    }

                    for (size_t i = 0; i < array->items.size(); i++) {
                        if (i != 0) {
                            print(",");
                        }
                        if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                            if (is_multi_line) {
                                print_newline();
                                print_indent();
                            } else if (i != 0) {
                                print_space();
                            }
                        }
                        print_expr(array->items[i], L::kComma, print_expr_flags::kNone);

                        // Make sure there's a comma after trailing missing items
                        if (Get<EMissing>(array->items[i].data) && i == array->items.size() - 1) {
                            print(",");
                        }
                    }

                    if (is_multi_line) {
                        print_newline();
                        print_expr_comments_after_close_token_at_loc(array->close_bracket_loc);
                        indent_--;
                        print_indent();
                    }
                }
                if (array->close_bracket_loc.start > expr.loc.start) {
                    add_source_mapping(array->close_bracket_loc);
                }
                print("]");

            } else if (const auto* object = Get<EObject>(e)) {
                bool is_multi_line = (object->properties.size() > 0 && !object->is_single_line) ||
                    will_print_expr_comments_at_loc(object->close_brace_loc);
                if (!options_.minify_whitespace && !is_multi_line) {
                    for (const Property& property : object->properties) {
                        if (will_print_expr_comments_at_loc(property.loc)) {
                            is_multi_line = true;
                            break;
                        }
                    }
                }
                int n = static_cast<int>(js_.size());
                bool wrap = stmt_start_ == n || arrow_expr_start_ == n;
                if (wrap) {
                    print("(");
                }
                add_source_mapping(expr.loc);
                print("{");
                if (object->properties.size() > 0 || is_multi_line) {
                    if (is_multi_line) {
                        indent_++;
                    }

                    for (size_t i = 0; i < object->properties.size(); i++) {
                        if (i != 0) {
                            print(",");
                        }
                        if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                            if (is_multi_line) {
                                print_newline();
                                print_indent();
                            } else {
                                print_space();
                            }
                        }
                        print_property(object->properties[i]);
                    }

                    if (is_multi_line) {
                        print_newline();
                        print_expr_comments_after_close_token_at_loc(object->close_brace_loc);
                        indent_--;
                        print_indent();
                    } else if (object->properties.size() > 0) {
                        print_space();
                    }
                }
                if (object->close_brace_loc.start > expr.loc.start) {
                    add_source_mapping(object->close_brace_loc);
                }
                print("}");
                if (wrap) {
                    print(")");
                }

            } else if (const auto* boolean = Get<EBoolean>(e)) {
                add_source_mapping(expr.loc);
                if (options_.minify_syntax) {
                    if (level >= L::kPrefix) {
                        if (boolean->value) {
                            print("(!0)");
                        } else {
                            print("(!1)");
                        }
                    } else {
                        if (boolean->value) {
                            print("!0");
                        } else {
                            print("!1");
                        }
                    }
                } else {
                    print_space_before_identifier();
                    if (boolean->value) {
                        print("true");
                    } else {
                        print("false");
                    }
                }

            } else if (const auto* string = Get<EString>(e)) {
                print_quoted_flags quoted_flags{print_quoted_flags::kNone};
                if (string->contains_unique_key) {
                    quoted_flags = print_quoted_flags::kNoWrap;

                    if (options_.needs_metafile) {
                        // Record this reference to an output file in the metafile
                        std::string entry_fmt = config::MaybeRemoveWhitespace(
                            options_.metafile_format, "\n        {\n          \"path\": %s,\n          \"kind\": %s\n        }");
                        std::string path_json =
                            helpers::QuoteForJSON(helpers::UTF16ToString(string->value), options_.ascii_only);
                        std::string entry;
                        entry.reserve(entry_fmt.size() + path_json.size() + sizeof("\"file-loader\""));
                        std::vector<std::string_view> args{path_json, "\"file-loader\""};
                        size_t argi = 0;
                        size_t pos = 0;
                        for (;;) {
                            size_t pct = entry_fmt.find('%', pos);
                            if (pct == std::string::npos) {
                                entry.append(entry_fmt, pos, std::string::npos);
                                break;
                            }
                            entry.append(entry_fmt, pos, pct - pos);
                            if (pct + 1 < entry_fmt.size() && entry_fmt[pct + 1] == 's') {
                                if (argi < args.size()) entry += args[argi];
                                argi++;
                                pos = pct + 2;
                            } else {
                                entry += '%';
                                pos = pct + 1;
                            }
                        }
                        json_metadata_imports_.push_back(std::move(entry));
                    }
                }
                add_source_mapping(expr.loc);

                if (!options_.minify_whitespace && string->has_property_key_comment) {
                    print("/* @__KEY__ */ ");
                }

                // If this was originally a template literal, print it as one as
                // long as we're not minifying
                if (string->prefer_template && !options_.minify_syntax &&
                    !compat::Has(options_.unsupported_features, compat::JSFeature::kTemplateLiteral)) {
                    print("`");
                    print_unquoted_utf16(string->value, '`', quoted_flags);
                    print("`");
                    return;
                }

                print_quoted_utf16(string->value, quoted_flags | print_quoted_flags::kAllowBacktick);

            } else if (Get<ETemplate>(e)) {
                const ETemplate* templ = Get<ETemplate>(e);
                Expr inlined;
                if (IsNil(templ->tag_or_nil.data) && (options_.minify_syntax || was_lazy_export_)) {
                    // Inline enums and mangled properties when minifying
                    std::vector<TemplatePart> replaced;
                    bool has_replaced = false;
                    for (size_t i = 0; i < templ->parts.size(); i++) {
                        std::shared_ptr<E> inlined_value;
                        const Expr& part_value = templ->parts[i].value;
                        if (const auto* mangled_name_sym = Get<ENameOfSymbol>(part_value.data)) {
                            inlined_value = std::make_shared<E>(E{std::make_shared<EString>(EString{
                                /*value=*/helpers::StringToUTF16(mangled_prop_name(mangled_name_sym->ref)),
                                /*legacy_octal_loc=*/{}, /*prefer_template=*/false,
                                /*has_property_key_comment=*/mangled_name_sym->has_property_key_comment,
                                /*contains_unique_key=*/false})});
                        } else if (const auto* enum_dot = Get<EDot>(part_value.data)) {
                            if (auto [value, ok] = try_to_get_imported_enum_value(enum_dot->target, enum_dot->name); ok) {
                                if (value.is_string) {
                                    inlined_value = std::make_shared<E>(
                                        E{std::make_shared<EString>(EString{value.string, {}})});
                                } else {
                                    inlined_value =
                                        std::make_shared<E>(E{std::make_shared<ENumber>(ENumber{value.number})});
                                }
                            }
                        }
                        if (inlined_value != nullptr) {
                            if (!has_replaced) {
                                replaced.assign(templ->parts.begin(), templ->parts.begin() + static_cast<ptrdiff_t>(i));
                                has_replaced = true;
                            }
                            TemplatePart part = templ->parts[i];
                            part.value.data = *inlined_value;
                            replaced.push_back(part);
                        } else if (has_replaced) {
                            replaced.push_back(templ->parts[i]);
                        }
                    }
                    if (has_replaced) {
                        ETemplate copy = *templ;
                        copy.parts = std::move(replaced);
                        inlined = javascript::InlinePrimitivesIntoTemplate(logger::Loc{}, copy);
                        if (const auto* str = Get<EString>(inlined.data)) {
                            print_quoted_utf16(str->value, print_quoted_flags::kAllowBacktick);
                            return;
                        } else if (const auto* templ2 = Get<ETemplate>(inlined.data)) {
                            templ = templ2;
                        }
                    }

                    // Convert no-substitution template literals into strings if it's smaller
                    if (templ->parts.size() == 0) {
                        add_source_mapping(expr.loc);
                        print_quoted_utf16(templ->head_cooked, print_quoted_flags::kAllowBacktick);
                        return;
                    }
                }

                if (!IsNil(templ->tag_or_nil.data)) {
                    bool tag_is_property_access = false;
                    const E& tag_data = templ->tag_or_nil.data;
                    if (Get<EDot>(tag_data) || Get<EIndex>(tag_data)) {
                        tag_is_property_access = true;
                    }
                    if (!templ->tag_was_originally_property_access && tag_is_property_access) {
                        // Prevent "x``" from becoming "y.z``"
                        print("(0,");
                        print_space();
                        print_expr(templ->tag_or_nil, L::kLowest, print_expr_flags::kIsCallTargetOrTemplateTag);
                        print(")");
                    } else if (javascript::IsOptionalChain(templ->tag_or_nil)) {
                        // Optional chains are forbidden in template tags
                        print("(");
                        print_expr(templ->tag_or_nil, L::kLowest, print_expr_flags::kIsCallTargetOrTemplateTag);
                        print(")");
                    } else {
                        print_expr(templ->tag_or_nil, L::kPostfix,
                                   print_expr_flags::kIsCallTargetOrTemplateTag |
                                       (flags & print_expr_flags::kIsNewTarget));
                    }
                } else {
                    add_source_mapping(expr.loc);
                }
                print("`");
                if (!IsNil(templ->tag_or_nil.data)) {
                    print(templ->head_raw);
                } else {
                    print_unquoted_utf16(templ->head_cooked, '`', print_quoted_flags::kNone);
                }
                for (const TemplatePart& part : templ->parts) {
                    print("${");
                    print_expr(part.value, L::kLowest, print_expr_flags::kNone);
                    add_source_mapping(part.tail_loc);
                    print("}");
                    if (!IsNil(templ->tag_or_nil.data)) {
                        print(part.tail_raw);
                    } else {
                        print_unquoted_utf16(part.tail_cooked, '`', print_quoted_flags::kNone);
                    }
                }
                print("`");

            } else if (const auto* regexp = Get<ERegExp>(e)) {
                // Avoid forming a single-line comment or "</script" sequence
                if (!compat::Has(options_.unsupported_features, compat::JSFeature::kInlineScript) && js_.size() > 0) {
                    char last = js_.back();
                    bool script_prefix = regexp->value.size() >= 7;
                    if (script_prefix) {
                        for (int i = 0; i < 7; i++) {
                            char a = regexp->value[static_cast<size_t>(i)];
                            if (a >= 'A' && a <= 'Z') {
                                a = static_cast<char>(a - 'A' + 'a');
                            }
                            if (a != "/script"[i]) {
                                script_prefix = false;
                                break;
                            }
                        }
                    }
                    if (last == '/' || (last == '<' && script_prefix)) {
                        print(" ");
                    }
                }

                add_source_mapping(expr.loc);
                print(regexp->value);

                // Need a space before the next identifier to avoid it turning into flags
                prev_regexp_end_ = static_cast<int>(js_.size());

            } else if (const auto* inlined_enum = Get<EInlinedEnum>(e)) {
                print_expr(inlined_enum->value, level, flags);

                if (!options_.minify_whitespace && !options_.minify_identifiers) {
                    print(" /* ");
                    print(inlined_enum->comment);
                    print(" */");
                }

            } else if (const auto* bigint = Get<EBigInt>(e)) {
                if (!compat::Has(options_.unsupported_features, compat::JSFeature::kBigint)) {
                    print_space_before_identifier();
                    add_source_mapping(expr.loc);
                    print(bigint->value);
                    print("n");
                    return;
                }

                bool wrap = level >= L::kNew || Has(flags, print_expr_flags::kIsNewTarget);
                bool has_pure_comment = !options_.minify_whitespace;

                if (has_pure_comment && level >= L::kPostfix) {
                    wrap = true;
                }

                if (wrap) {
                    print("(");
                }

                if (has_pure_comment) {
                    expr_start_flags saved = save_expr_start_flags();
                    add_source_mapping(expr.loc);
                    print("/* @__PURE__ */ ");
                    restore_expr_start_flags(saved);
                }

                std::string value = bigint->value;

                // When minifying, try to convert to a shorter form
                if (options_.minify_syntax) {
                    std::string str = BigIntToDecimalString(value);

                    // Print without quotes if it can be converted exactly
                    bool use_quotes = true;
                    {
                        char* end = nullptr;
                        double num = std::strtod(str.c_str(), &end);
                        if (end != str.c_str() && *end == '\0' && !std::isinf(num)) {
                            char buf[160];
                            snprintf(buf, sizeof(buf), "%.0f", num);
                            if (str == buf) {
                                use_quotes = false;
                            }
                        }
                    }

                    // Print the converted form if it's shorter (long hex strings may not be shorter)
                    if (str.size() < value.size()) {
                        value = str;
                    }

                    print_space_before_identifier();
                    add_source_mapping(expr.loc);
                    if (use_quotes) {
                        print("BigInt(\"");
                    } else {
                        print("BigInt(");
                    }
                    print(value);
                    if (use_quotes) {
                        print("\")");
                    } else {
                        print(")");
                    }
                } else {
                    print_space_before_identifier();
                    add_source_mapping(expr.loc);
                    print("BigInt(\"");
                    print(value);
                    print("\")");
                }

                if (wrap) {
                    print(")");
                }

            } else if (const auto* number = Get<ENumber>(e)) {
                add_source_mapping(expr.loc);
                print_number(number->value, level);

            } else if (const auto* identifier = Get<EIdentifier>(e)) {
                    
                // const auto ref = compiler::FollowSymbols(symbols_, identifier->ref);
                // const auto* symbol = symbols_.Get(ref);

                // fprintf(stderr,
                //     "[PRINT IDENT] name=[%s] original=[%s]\n",
                //     renamer_.NameForSymbol(identifier->ref).c_str(),
                //     symbol->original_name.c_str());

                std::string name = renamer_.NameForSymbol(identifier->ref);
                bool wrap = static_cast<int>(js_.size()) == for_of_init_start_ &&
                    (name == "let" ||
                     (Has(flags, print_expr_flags::kIsFollowedByOf) &&
                      !Has(flags, print_expr_flags::kIsInsideForAwait) && name == "async"));

                if (wrap) {
                    print("(");
                }

                print_space_before_identifier();
                add_source_mapping_for_name(expr.loc, name, identifier->ref);
                print_identifier(name);

                if (wrap) {
                    print(")");
                }

            } else if (const auto* import_id = Get<EImportIdentifier>(e)) {
                // Potentially use a property access instead of an identifier
                compiler::Ref ref = compiler::FollowSymbols(symbols_, import_id->ref);
                const compiler::Symbol* symbol = symbols_.Get(ref);

                if (symbol->import_item_status == compiler::ImportItemStatus::kMissing) {
                    print_undefined(expr.loc, level);
                } else if (symbol->namespace_alias != nullptr) {
                    bool wrap = call_target_ == import_id && import_id->was_originally_identifier;
                    if (wrap) {
                        print("(0,");
                        print_space();
                    }
                    print_space_before_identifier();
                    add_source_mapping(expr.loc);
                    print_identifier(renamer_.NameForSymbol(symbol->namespace_alias->namespace_ref));
                    const std::string& alias = symbol->namespace_alias->alias;
                    if (!import_id->prefer_quoted_key && can_print_identifier(alias)) {
                        print(".");
                        add_source_mapping_for_name(expr.loc, alias, ref);
                        print_identifier(alias);
                    } else {
                        print("[");
                        add_source_mapping_for_name(expr.loc, alias, ref);
                        print_quoted_utf8(alias, print_quoted_flags::kAllowBacktick);
                        print("]");
                    }
                    if (wrap) {
                        print(")");
                    }
                } else if (auto it = options_.const_values.find(ref);
                           it != options_.const_values.end() && it->second.kind != ConstValueKind::kNone) {
                    // Handle inlined constants
                    print_expr(ConstValueToExpr(expr.loc, it->second), level, flags);
                } else {
                    print_space_before_identifier();
                    std::string name = renamer_.NameForSymbol(ref);
                    add_source_mapping_for_name(expr.loc, name, ref);
                    print_identifier(name);
                }

            } else if (const auto* await_expr = Get<EAwait>(e)) {
                bool wrap = level >= L::kPrefix;

                if (wrap) {
                    print("(");
                }

                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("await");
                print_space();
                print_expr(await_expr->value, static_cast<L>(static_cast<int>(L::kPrefix) - 1),
                           print_expr_flags::kNone);

                if (wrap) {
                    print(")");
                }

            } else if (const auto* yield_expr = Get<EYield>(e)) {
                bool wrap = level >= L::kAssign;

                if (wrap) {
                    print("(");
                }

                print_space_before_identifier();
                add_source_mapping(expr.loc);
                print("yield");

                if (!IsNil(yield_expr->value_or_nil.data)) {
                    if (yield_expr->is_star) {
                        print("*");
                    }
                    print_space();
                    print_expr_without_leading_newline(yield_expr->value_or_nil, L::kYield, print_expr_flags::kNone);
                }

                if (wrap) {
                    print(")");
                }

            } else if (const auto* unary = Get<EUnary>(e)) {
                const OpTableEntry& entry = kOpTable[static_cast<size_t>(unary->op)];
                bool wrap = level >= entry.level;

                if (wrap) {
                    print("(");
                }

                if (!IsPrefix(unary->op)) {
                    print_expr(unary->value, static_cast<L>(static_cast<int>(L::kPostfix) - 1),
                               print_expr_flags::kParentWasUnaryOrBinaryOrIfTest);
                }

                if (entry.is_keyword) {
                    print_space_before_identifier();
                    if (IsPrefix(unary->op)) {
                        add_source_mapping(expr.loc);
                    }
                    print(entry.text);
                    print_space();
                } else {
                    print_space_before_operator(unary->op);
                    if (IsPrefix(unary->op)) {
                        add_source_mapping(expr.loc);
                    }
                    print(entry.text);
                    prev_op_ = unary->op;
                    prev_op_end_ = static_cast<int>(js_.size());
                }

                if (IsPrefix(unary->op)) {
                    print_expr_flags value_flags = print_expr_flags::kParentWasUnaryOrBinaryOrIfTest;
                    if (unary->op == OpCode::kUnOpDelete) {
                        value_flags = value_flags | print_expr_flags::kIsDeleteTarget;
                    }

                    // Never turn "typeof (0, x)" into "typeof x" or
                    // "delete (0, x)" into "delete x"
                    if ((unary->op == OpCode::kUnOpTypeof && !unary->was_originally_typeof_identifier &&
                         is_unbound_identifier(unary->value)) ||
                        (unary->op == OpCode::kUnOpDelete && !unary->was_originally_delete_of_identifier_or_property_access &&
                         is_identifier_or_numeric_constant_or_property_access(unary->value))) {
                        print("(0,");
                        print_space();
                        print_expr(unary->value, static_cast<L>(static_cast<int>(L::kPrefix) - 1), value_flags);
                        print(")");
                    } else {
                        print_expr(unary->value, static_cast<L>(static_cast<int>(L::kPrefix) - 1), value_flags);
                    }
                }

                if (wrap) {
                    print(")");
                }

            } else if (const auto* binary = Get<EBinary>(e)) {
                // The handling of binary expressions is convoluted because we're
                // using iteration on the heap instead of recursion on the call
                // stack to avoid stack overflow for deeply-nested ASTs.
                binary_expr_visitor v;
                v.e = binary;
                v.level = level;
                v.flags = flags;

                // Use a single stack to reduce allocation overhead
                size_t stack_bottom = binary_expr_stack_.size();

                for (;;) {
                    // Check whether this node is a special case, and stop if it is
                    if (!check_and_prepare(v)) {
                        break;
                    }

                    const Expr& left = v.e->left;
                    const auto* left_binary = Get<EBinary>(left.data);

                    // Stop iterating if iteration doesn't apply to the left node
                    if (left_binary == nullptr) {
                        print_expr(left, v.left_level, v.left_flags);
                        visit_right_and_finish(v);
                        break;
                    }

                    // Manually run the code at the start of "print_expr"
                    print_expr_comments_at_loc(left.loc);

                    // Only allocate heap memory on the stack for nested binary expressions
                    binary_expr_stack_.push_back(v);
                    v.e = left_binary;
                    v.level = v.left_level;
                    v.flags = v.left_flags;
                }

                // Process all binary operations from the deepest-visited node back
                // toward our original top-level binary operation
                while (binary_expr_stack_.size() > stack_bottom) {
                    v = binary_expr_stack_.back();
                    binary_expr_stack_.pop_back();
                    visit_right_and_finish(v);
                }

            } else {
                throw std::runtime_error("js_printer.cpp: Unexpected expression while printing");
            }
        }

        // -- Statement printing helpers --

        // Prints a declaration statement: `export? keyword binding = value, ...;`
        void printer::print_decl_stmt(bool is_export, std::string_view keyword, const std::vector<Decl>& decls) {
            print_indent();
            print_space_before_identifier();
            if (is_export) {
                print("export ");
            }
            print_decls(keyword, decls, print_expr_flags::kNone);
            print_semicolon_after_statement();
        }

        // Prints the initializer of a for-loop: either an expression statement
        // or a declaration (var/let/const/using/await using).
        void printer::print_for_loop_init(const Stmt& init, print_expr_flags flags) {
            if (const SExpr* expr_stmt = Get<SExpr>(init.data); expr_stmt) {
                print_expr(expr_stmt->value, L::kLowest, flags | print_expr_flags::kExprResultIsUnused);
            } else if (const SLocal* local = Get<SLocal>(init.data); local) {
                switch (local->kind) {
                case LocalKind::kAwaitUsing:
                    print_decls("await using", local->decls, flags);
                    break;
                case LocalKind::kConst:
                    print_decls("const", local->decls, flags);
                    break;
                case LocalKind::kLet:
                    print_decls("let", local->decls, flags);
                    break;
                case LocalKind::kUsing:
                    print_decls("using", local->decls, flags);
                    break;
                case LocalKind::kVar:
                    print_decls("var", local->decls, flags);
                    break;
                }
            } else {
                throw std::runtime_error("Internal error");
            }
        }

        // Prints a comma-separated list of declarations with a keyword prefix:
        // "let a = 1, b = 2"
        void printer::print_decls(std::string_view keyword, const std::vector<Decl>& decls, print_expr_flags flags) {
            print(keyword);
            print_space();

            for (size_t i = 0; i < decls.size(); i++) {
                if (i != 0) {
                    print(",");
                    if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                        print_space();
                    }
                }
                const Decl& decl = decls[i];
                print_binding(decl.binding);

                if (!IsNil(decl.value_or_nil.data)) {
                    print_space();
                    print("=");
                    print_space();
                    print_expr_without_leading_newline(decl.value_or_nil, L::kComma, flags);
                }
            }
        }

        // Prints a statement body in one of three forms:
        //   - Block: ` { ... }`
        //   - Single-line: ` stmt` (on same line, e.g., `if (x) return;`)
        //   - Multi-line: newline + indented stmt
        void printer::print_body(const Stmt& body, bool is_single_line) {
            if (const SBlock* block = Get<SBlock>(body.data); block) {
                print_space();
                print_block(body.loc, *block);
                print_newline();
            } else if (is_single_line) {
                print_next_indent_as_space_ = true;
                print_stmt(body, print_stmt_flags::kNone);
            } else {
                print_newline();
                indent_++;
                print_stmt(body, print_stmt_flags::kNone);
                indent_--;
            }
        }

        // Returns true if wrapping the statement in braces is needed to avoid
        // the "dangling else" ambiguity (e.g., `if (a) if (b) x; else y;`).
        bool wrap_to_avoid_ambiguous_else(const Stmt& stmt) {
            const Stmt* s = &stmt;
            for (;;) {
                if (const SIf* if_stmt = Get<SIf>(s->data); if_stmt) {
                    if (if_stmt->no_or_nil == nullptr) {
                        return true;
                    }
                    s = if_stmt->no_or_nil.get();
                } else if (const SFor* for_stmt = Get<SFor>(s->data); for_stmt) {
                    s = &for_stmt->body;
                } else if (const SForIn* for_in = Get<SForIn>(s->data); for_in) {
                    s = &for_in->body;
                } else if (const SForOf* for_of = Get<SForOf>(s->data); for_of) {
                    s = &for_of->body;
                } else if (const SWhile* while_stmt = Get<SWhile>(s->data); while_stmt) {
                    s = &while_stmt->body;
                } else if (const SWith* with_stmt = Get<SWith>(s->data); with_stmt) {
                    s = &with_stmt->body;
                } else if (const SLabel* label = Get<SLabel>(s->data); label) {
                    s = &label->stmt;
                } else {
                    return false;
                }
            }
        }

        // Prints an if/else statement.  Handles:
        //   - Simplification of else branches (empty function calls)
        //   - Block vs single-line body formatting
        //   - "dangling else" wrapping when the consequent is itself an if-statement
        void printer::print_if(const SIf& s) {
            print_space_before_identifier();
            print("if");
            print_space();
            print("(");
            if (will_print_expr_comments_at_loc(s.test.loc)) {
                print_newline();
                indent_++;
                print_indent();
                print_expr(s.test, L::kLowest, print_expr_flags::kNone);
                print_newline();
                indent_--;
                print_indent();
            } else {
                print_expr(s.test, L::kLowest, print_expr_flags::kNone);
            }
            print(")");

            // Simplify the else branch, which may disappear entirely
            std::shared_ptr<Stmt> no = s.no_or_nil;
            if (no) {
                if (const SExpr* expr_stmt = Get<SExpr>(no->data); expr_stmt) {
                    Expr value = simplify_unused_expr(expr_stmt->value);
                    if (IsNil(value.data)) {
                        no = nullptr;
                    } else if (value.data != expr_stmt->value.data) {
                        auto new_expr_stmt = std::make_shared<SExpr>();
                        new_expr_stmt->value = value;
                        auto new_no = std::make_shared<Stmt>();
                        new_no->data = new_expr_stmt;
                        new_no->loc = no->loc;
                        no = new_no;
                    }
                }
            }

            if (const SBlock* yes_block = Get<SBlock>(s.yes.data); yes_block) {
                print_space();
                print_block(s.yes.loc, *yes_block);

                if (no != nullptr) {
                    print_space();
                } else {
                    print_newline();
                }
            } else if (wrap_to_avoid_ambiguous_else(s.yes)) {
                print_space();
                print("{");
                print_newline();

                indent_++;
                print_stmt(s.yes, print_stmt_flags::kCanOmitStatement);
                indent_--;
                needs_semicolon_ = false;

                print_indent();
                print("}");

                if (no != nullptr) {
                    print_space();
                } else {
                    print_newline();
                }
            } else {
                print_body(s.yes, s.is_single_line_yes);

                if (no != nullptr) {
                    print_indent();
                }
            }

            if (no != nullptr) {
                print_semicolon_if_needed();
                print_space_before_identifier();
                print("else");

                if (const SBlock* block = Get<SBlock>(no->data); block) {
                    print_space();
                    print_block(no->loc, *block);
                    print_newline();
                } else if (const SIf* if_stmt = Get<SIf>(no->data); if_stmt) {
                    print_if(*if_stmt);
                } else {
                    print_body(*no, s.is_single_line_no);
                }
            }
        }

        // =============================================================================
        // print_stmt — the main statement serialization dispatcher
        // =============================================================================
        // Walks every statement type in the AST and emits the corresponding
        // JavaScript source.  Handles:
        //   - Comments (legal vs expression comments)
        //   - Functions, classes, and their export forms
        //   - Variable declarations (var/let/const/using/await using)
        //   - Control flow: if, for, for-in, for-of, while, do-while, switch
        //   - Try/catch/finally
        //   - Import/export statements (all forms)
        //   - Expression statements (with empty-function-call optimization)
        //   - Break, continue, return, throw, debugger
        //   - Labeled statements, blocks, directives
        void printer::print_stmt(const Stmt& stmt, print_stmt_flags flags) {
            if (options_.line_limit > 0) {
                print_newline_past_line_limit();
            }

            if (const SImport* import_stmt = Get<SImport>(stmt.data);
                    import_stmt != nullptr && omit_runtime_import(import_stmt->import_record_index)) {
                return;
            }

            if (const SComment* comment = Get<SComment>(stmt.data); comment) {
                const std::string& text = comment->text;

                if (comment->is_legal_comment) {
                    switch (options_.legal_comments) {
                    case config::LegalComments::kInline:
                        // Fall through and print the comment inline
                        break;

                    case config::LegalComments::kNone:
                        return;

                    case config::LegalComments::kEndOfFile:
                    case config::LegalComments::kLinkedWithComment:
                    case config::LegalComments::kExternalWithoutComment:
                        // Don't record the same legal comment more than once per file
                        if (has_legal_comment_.find(text) != has_legal_comment_.end()) {
                            return;
                        }
                        has_legal_comment_.insert(text);
                        extracted_legal_comments_.push_back(text);
                        return;
                    }
                }

                print_indent();
                add_source_mapping(stmt.loc);
                print_indented_comment(text);

            } else if (const SFunction* function = Get<SFunction>(stmt.data); function) {
                if (!options_.minify_whitespace && function->fn.has_no_side_effects_comment) {
                    print_indent();
                    print("// @__NO_SIDE_EFFECTS__\n");
                }
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                if (function->is_export) {
                    print("export ");
                }
                if (function->fn.is_async) {
                    print("async ");
                }
                print("function");
                if (function->fn.is_generator) {
                    print("*");
                    print_space();
                }
                print_space_before_identifier();
                std::string name = renamer_.NameForSymbol(function->fn.name->ref);
                add_source_mapping_for_name(function->fn.name->loc, name, function->fn.name->ref);
                print_identifier(name);
                print_fn(function->fn);
                print_newline();

            } else if (const SClass* class_stmt = Get<SClass>(stmt.data); class_stmt) {
                bool omit_indent = print_decorators(class_stmt->class_.decorators, print_after_decorator::kNewline);
                if (!omit_indent) {
                    print_indent();
                }
                print_space_before_identifier();
                add_source_mapping(stmt.loc);
                if (class_stmt->is_export) {
                    print("export ");
                }
                print("class ");
                std::string name = renamer_.NameForSymbol(class_stmt->class_.name->ref);
                add_source_mapping_for_name(class_stmt->class_.name->loc, name, class_stmt->class_.name->ref);
                print_identifier(name);
                print_class(class_stmt->class_);
                print_newline();

            } else if (Get<SEmpty>(stmt.data)) {
                add_source_mapping(stmt.loc);
                print_indent();
                print(";");
                print_newline();

            } else if (const SExportDefault* export_default = Get<SExportDefault>(stmt.data); export_default) {
                if (!options_.minify_whitespace) {
                    if (const SFunction* fn = Get<SFunction>(export_default->value.data); fn && fn->fn.has_no_side_effects_comment) {
                        print_indent();
                        print("// @__NO_SIDE_EFFECTS__\n");
                    }
                }
                bool omit_indent = false;
                if (const SClass* export_class_stmt = Get<SClass>(export_default->value.data); export_class_stmt) {
                    omit_indent = print_decorators(export_class_stmt->class_.decorators, print_after_decorator::kNewline);
                }
                add_source_mapping(stmt.loc);
                if (!omit_indent) {
                    print_indent();
                }
                print_space_before_identifier();
                print("export default");
                print_space();

                if (const SExpr* expr_stmt = Get<SExpr>(export_default->value.data); expr_stmt) {
                    // Functions and classes must be wrapped to avoid confusion with their statement forms
                    export_default_start_ = static_cast<int>(js_.size());

                    print_expr_without_leading_newline(expr_stmt->value, L::kComma, print_expr_flags::kNone);
                    print_semicolon_after_statement();

                } else if (const SFunction* fn = Get<SFunction>(export_default->value.data); fn) {
                    print_space_before_identifier();
                    if (fn->fn.is_async) {
                        print("async ");
                    }
                    print("function");
                    if (fn->fn.is_generator) {
                        print("*");
                        print_space();
                    }
                    if (fn->fn.name != nullptr) {
                        print_space_before_identifier();
                        std::string name = renamer_.NameForSymbol(fn->fn.name->ref);
                        add_source_mapping_for_name(fn->fn.name->loc, name, fn->fn.name->ref);
                        print_identifier(name);
                    }
                    print_fn(fn->fn);
                    print_newline();

                } else if (const SClass* export_class_stmt = Get<SClass>(export_default->value.data); export_class_stmt) {
                    print_space_before_identifier();
                    print("class");
                    if (export_class_stmt->class_.name != nullptr) {
                        print(" ");
                        std::string name = renamer_.NameForSymbol(export_class_stmt->class_.name->ref);
                        add_source_mapping_for_name(export_class_stmt->class_.name->loc, name, export_class_stmt->class_.name->ref);
                        print_identifier(name);
                    }
                    print_class(export_class_stmt->class_);
                    print_newline();

                } else {
                    throw std::runtime_error("Internal error");
                }

            } else if (const SExportStar* export_star = Get<SExportStar>(stmt.data); export_star) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("export");
                print_space();
                print("*");
                print_space();
                if (export_star->alias != nullptr) {
                    print("as");
                    print_space();
                    print_clause_alias(export_star->alias->loc, export_star->alias->original_name);
                    print_space();
                    print_space_before_identifier();
                }
                print("from");
                print_space();
                print_path(export_star->import_record_index, compiler::ImportKind::kStmt);
                print_semicolon_after_statement();

            } else if (const SExportClause* export_clause = Get<SExportClause>(stmt.data); export_clause) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("export");
                print_space();
                print("{");

                if (!export_clause->is_single_line) {
                    indent_++;
                }

                for (size_t i = 0; i < export_clause->items.size(); i++) {
                    if (i != 0) {
                        print(",");
                    }

                    if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                        if (export_clause->is_single_line) {
                            print_space();
                        } else {
                            print_newline();
                            print_indent();
                        }
                    }

                    const ClauseItem& item = export_clause->items[i];
                    std::string name = renamer_.NameForSymbol(item.name.ref);
                    add_source_mapping_for_name(item.name.loc, name, item.name.ref);
                    print_identifier(name);
                    if (name != item.alias) {
                        print(" as");
                        print_space();
                        print_clause_alias(item.alias_loc, item.alias);
                    }
                }

                if (!export_clause->is_single_line) {
                    indent_--;
                    print_newline();
                    print_indent();
                } else if (export_clause->items.size() > 0) {
                    print_space();
                }

                print("}");
                print_semicolon_after_statement();

            } else if (const SExportFrom* export_from = Get<SExportFrom>(stmt.data); export_from) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("export");
                print_space();
                print("{");

                if (!export_from->is_single_line) {
                    indent_++;
                }

                for (size_t i = 0; i < export_from->items.size(); i++) {
                    if (i != 0) {
                        print(",");
                    }

                    if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                        if (export_from->is_single_line) {
                            print_space();
                        } else {
                            print_newline();
                            print_indent();
                        }
                    }

                    const ClauseItem& item = export_from->items[i];
                    print_clause_alias(item.name.loc, item.original_name);
                    if (item.original_name != item.alias) {
                        print_space();
                        print_space_before_identifier();
                        print("as");
                        print_space();
                        print_clause_alias(item.alias_loc, item.alias);
                    }
                }

                if (!export_from->is_single_line) {
                    indent_--;
                    print_newline();
                    print_indent();
                } else if (export_from->items.size() > 0) {
                    print_space();
                }

                print("}");
                print_space();
                print("from");
                print_space();
                print_path(export_from->import_record_index, compiler::ImportKind::kStmt);
                print_semicolon_after_statement();

            } else if (const SLocal* local = Get<SLocal>(stmt.data); local) {
                add_source_mapping(stmt.loc);
                switch (local->kind) {
                case LocalKind::kAwaitUsing:
                    print_decl_stmt(local->is_export, "await using", local->decls);
                    break;
                case LocalKind::kConst:
                    print_decl_stmt(local->is_export, "const", local->decls);
                    break;
                case LocalKind::kLet:
                    print_decl_stmt(local->is_export, "let", local->decls);
                    break;
                case LocalKind::kUsing:
                    print_decl_stmt(local->is_export, "using", local->decls);
                    break;
                case LocalKind::kVar:
                    print_decl_stmt(local->is_export, "var", local->decls);
                    break;
                }

            } else if (const SIf* if_stmt = Get<SIf>(stmt.data); if_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_if(*if_stmt);

            } else if (const SDoWhile* do_while = Get<SDoWhile>(stmt.data); do_while) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("do");
                if (const SBlock* block = Get<SBlock>(do_while->body.data); block) {
                    print_space();
                    print_block(do_while->body.loc, *block);
                    print_space();
                } else {
                    print_newline();
                    indent_++;
                    print_stmt(do_while->body, print_stmt_flags::kNone);
                    print_semicolon_if_needed();
                    indent_--;
                    print_indent();
                }
                print("while");
                print_space();
                print("(");
                if (will_print_expr_comments_at_loc(do_while->test.loc)) {
                    print_newline();
                    indent_++;
                    print_indent();
                    print_expr(do_while->test, L::kLowest, print_expr_flags::kNone);
                    print_newline();
                    indent_--;
                    print_indent();
                } else {
                    print_expr(do_while->test, L::kLowest, print_expr_flags::kNone);
                }
                print(")");
                print_semicolon_after_statement();

            } else if (const SForIn* for_in = Get<SForIn>(stmt.data); for_in) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("for");
                print_space();
                print("(");
                bool has_init_comment = will_print_expr_comments_at_loc(for_in->init->loc);
                bool has_value_comment = will_print_expr_comments_at_loc(for_in->value.loc);
                if (has_init_comment || has_value_comment) {
                    print_newline();
                    indent_++;
                    print_indent();
                }
                print_for_loop_init(*for_in->init, print_expr_flags::kForbidIn);
                print_space();
                print_space_before_identifier();
                print("in");
                if (has_value_comment) {
                    print_newline();
                    print_indent();
                } else {
                    print_space();
                }
                print_expr(for_in->value, L::kLowest, print_expr_flags::kNone);
                if (has_init_comment || has_value_comment) {
                    print_newline();
                    indent_--;
                    print_indent();
                }
                print(")");
                print_body(for_in->body, for_in->is_single_line_body);

            } else if (const SForOf* for_of = Get<SForOf>(stmt.data); for_of) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("for");
                if (for_of->await.len > 0) {
                    print(" await");
                }
                print_space();
                print("(");
                bool has_init_comment = will_print_expr_comments_at_loc(for_of->init->loc);
                bool has_value_comment = will_print_expr_comments_at_loc(for_of->value.loc);
                print_expr_flags for_of_flags =
                    print_expr_flags::kForbidIn | print_expr_flags::kIsFollowedByOf;
                if (for_of->await.len > 0) {
                    for_of_flags = for_of_flags | print_expr_flags::kIsInsideForAwait;
                }
                if (has_init_comment || has_value_comment) {
                    print_newline();
                    indent_++;
                    print_indent();
                }
                for_of_init_start_ = static_cast<int>(js_.size());
                print_for_loop_init(*for_of->init, for_of_flags);
                print_space();
                print_space_before_identifier();
                print("of");
                if (has_value_comment) {
                    print_newline();
                    print_indent();
                } else {
                    print_space();
                }
                print_expr(for_of->value, L::kComma, print_expr_flags::kNone);
                if (has_init_comment || has_value_comment) {
                    print_newline();
                    indent_--;
                    print_indent();
                }
                print(")");
                print_body(for_of->body, for_of->is_single_line_body);

            } else if (const SWhile* while_stmt = Get<SWhile>(stmt.data); while_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("while");
                print_space();
                print("(");
                if (will_print_expr_comments_at_loc(while_stmt->test.loc)) {
                    print_newline();
                    indent_++;
                    print_indent();
                    print_expr(while_stmt->test, L::kLowest, print_expr_flags::kNone);
                    print_newline();
                    indent_--;
                    print_indent();
                } else {
                    print_expr(while_stmt->test, L::kLowest, print_expr_flags::kNone);
                }
                print(")");
                print_body(while_stmt->body, while_stmt->is_single_line_body);

            } else if (const SWith* with_stmt = Get<SWith>(stmt.data); with_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("with");
                print_space();
                print("(");
                if (will_print_expr_comments_at_loc(with_stmt->value.loc)) {
                    print_newline();
                    indent_++;
                    print_indent();
                    print_expr(with_stmt->value, L::kLowest, print_expr_flags::kNone);
                    print_newline();
                    indent_--;
                    print_indent();
                } else {
                    print_expr(with_stmt->value, L::kLowest, print_expr_flags::kNone);
                }
                print(")");
                with_nesting_++;
                print_body(with_stmt->body, with_stmt->is_single_line_body);
                with_nesting_--;

            } else if (const SLabel* label = Get<SLabel>(stmt.data); label) {
                // Avoid printing a source mapping that masks the one from the label
                if (!options_.minify_whitespace && (indent_ > 0 || print_next_indent_as_space_)) {
                    add_source_mapping(stmt.loc);
                    print_indent();
                }

                print_space_before_identifier();
                std::string name = renamer_.NameForSymbol(label->name.ref);
                add_source_mapping_for_name(label->name.loc, name, label->name.ref);
                print_identifier(name);
                print(":");
                print_body(label->stmt, label->is_single_line_stmt);

            } else if (const STry* try_stmt = Get<STry>(stmt.data); try_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("try");
                print_space();
                print_block(try_stmt->block_loc, try_stmt->block);

                if (try_stmt->catch_block != nullptr) {
                    print_space();
                    print("catch");
                    if (!IsNil(try_stmt->catch_block->binding_or_nil.data)) {
                        print_space();
                        print("(");
                        print_binding(try_stmt->catch_block->binding_or_nil);
                        print(")");
                    }
                    print_space();
                    print_block(try_stmt->catch_block->block_loc, try_stmt->catch_block->block);
                }

                if (try_stmt->finally_block != nullptr) {
                    print_space();
                    print("finally");
                    print_space();
                    print_block(try_stmt->finally_block->loc, try_stmt->finally_block->block);
                }

                print_newline();

            } else if (const SFor* for_stmt = Get<SFor>(stmt.data); for_stmt) {
                std::shared_ptr<Stmt> init = for_stmt->init_or_nil;
                Expr update = for_stmt->update_or_nil;

                // Omit calls to empty functions from the output completely
                if (options_.minify_syntax) {
                    if (init) {
                        if (const SExpr* expr_stmt = Get<SExpr>(init->data); expr_stmt) {
                            Expr value = simplify_unused_expr(expr_stmt->value);
                            if (IsNil(value.data)) {
                                init = nullptr;
                            } else if (value.data != expr_stmt->value.data) {
                                auto new_expr_stmt = std::make_shared<SExpr>();
                                new_expr_stmt->value = value;
                                auto new_init = std::make_shared<Stmt>();
                                new_init->data = new_expr_stmt;
                                new_init->loc = init->loc;
                                init = new_init;
                            }
                        }
                    }
                    if (!IsNil(update.data)) {
                        update = simplify_unused_expr(update);
                    }
                }

                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("for");
                print_space();
                print("(");
                bool is_multi_line =
                    (init != nullptr && will_print_expr_comments_at_loc(init->loc)) ||
                    (!IsNil(for_stmt->test_or_nil.data) && will_print_expr_comments_at_loc(for_stmt->test_or_nil.loc)) ||
                    (!IsNil(update.data) && will_print_expr_comments_at_loc(update.loc));
                if (is_multi_line) {
                    print_newline();
                    indent_++;
                    print_indent();
                }
                if (init != nullptr) {
                    print_for_loop_init(*init, print_expr_flags::kForbidIn);
                }
                print(";");
                if (is_multi_line) {
                    print_newline();
                    print_indent();
                } else {
                    print_space();
                }
                if (!IsNil(for_stmt->test_or_nil.data)) {
                    print_expr(for_stmt->test_or_nil, L::kLowest, print_expr_flags::kNone);
                }
                print(";");
                if (!is_multi_line) {
                    print_space();
                } else if (!IsNil(update.data)) {
                    print_newline();
                    print_indent();
                }
                if (!IsNil(update.data)) {
                    print_expr(update, L::kLowest, print_expr_flags::kExprResultIsUnused);
                }
                if (is_multi_line) {
                    print_newline();
                    indent_--;
                    print_indent();
                }
                print(")");
                print_body(for_stmt->body, for_stmt->is_single_line_body);

            } else if (const SSwitch* switch_stmt = Get<SSwitch>(stmt.data); switch_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("switch");
                print_space();
                print("(");
                if (will_print_expr_comments_at_loc(switch_stmt->test.loc)) {
                    print_newline();
                    indent_++;
                    print_indent();
                    print_expr(switch_stmt->test, L::kLowest, print_expr_flags::kNone);
                    print_newline();
                    indent_--;
                    print_indent();
                } else {
                    print_expr(switch_stmt->test, L::kLowest, print_expr_flags::kNone);
                }
                print(")");
                print_space();
                add_source_mapping(switch_stmt->body_loc);
                print("{");
                print_newline();
                indent_++;

                for (const Case& case_ : switch_stmt->cases) {
                    print_semicolon_if_needed();
                    print_indent();
                    print_expr_comments_at_loc(case_.loc);
                    add_source_mapping(case_.loc);

                    if (!IsNil(case_.value_or_nil.data)) {
                        print("case");
                        print_space();
                        print_expr(case_.value_or_nil, L::kLogicalAnd, print_expr_flags::kNone);
                    } else {
                        print("default");
                    }
                    print(":");

                    if (case_.body.size() == 1) {
                        if (const SBlock* block = Get<SBlock>(case_.body[0].data); block) {
                            print_space();
                            print_block(case_.body[0].loc, *block);
                            print_newline();
                            continue;
                        }
                    }

                    print_newline();
                    indent_++;
                    for (const Stmt& case_stmt : case_.body) {
                        print_semicolon_if_needed();
                        print_stmt(case_stmt, print_stmt_flags::kCanOmitStatement);
                    }
                    indent_--;
                }

                indent_--;
                print_indent();
                add_source_mapping(switch_stmt->close_brace_loc);
                print("}");
                print_newline();
                needs_semicolon_ = false;

            } else if (const SImport* import_stmt = Get<SImport>(stmt.data); import_stmt) {
                int item_count = 0;

                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                switch (import_records_[import_stmt->import_record_index].phase) {
                case compiler::ImportPhase::kDefer:
                    print("import defer");
                    break;
                case compiler::ImportPhase::kSource:
                    print("import source");
                    break;
                default:
                    print("import");
                    break;
                }
                print_space();

                if (import_stmt->default_name != nullptr) {
                    print_space_before_identifier();
                    std::string name = renamer_.NameForSymbol(import_stmt->default_name->ref);
                    add_source_mapping_for_name(import_stmt->default_name->loc, name, import_stmt->default_name->ref);
                    print_identifier(name);
                    item_count++;
                }

                if (import_stmt->items != nullptr) {
                    if (item_count > 0) {
                        print(",");
                        print_space();
                    }

                    print("{");
                    if (!import_stmt->is_single_line) {
                        indent_++;
                    }

                    const std::vector<ClauseItem>& items = *import_stmt->items;
                    for (size_t i = 0; i < items.size(); i++) {
                        if (i != 0) {
                            print(",");
                        }

                        if (options_.line_limit <= 0 || !print_newline_past_line_limit()) {
                            if (import_stmt->is_single_line) {
                                print_space();
                            } else {
                                print_newline();
                                print_indent();
                            }
                        }

                        const ClauseItem& item = items[i];
                        print_clause_alias(item.alias_loc, item.alias);

                        std::string name = renamer_.NameForSymbol(item.name.ref);
                        if (name != item.alias) {
                            print_space();
                            print_space_before_identifier();
                            print("as ");
                            add_source_mapping_for_name(item.name.loc, name, item.name.ref);
                            print_identifier(name);
                        }
                    }

                    if (!import_stmt->is_single_line) {
                        indent_--;
                        print_newline();
                        print_indent();
                    } else if (items.size() > 0) {
                        print_space();
                    }

                    print("}");
                    item_count++;
                }

                if (import_stmt->star_name_loc != nullptr) {
                    if (item_count > 0) {
                        print(",");
                        print_space();
                    }

                    print("*");
                    print_space();
                    print("as ");
                    std::string name = renamer_.NameForSymbol(import_stmt->namespace_ref);
                    add_source_mapping_for_name(*import_stmt->star_name_loc, name, import_stmt->namespace_ref);
                    print_identifier(name);
                    item_count++;
                }

                if (item_count > 0) {
                    print_space();
                    print_space_before_identifier();
                    print("from");
                    print_space();
                }

                print_path(import_stmt->import_record_index, compiler::ImportKind::kStmt);
                print_semicolon_after_statement();

            } else if (const SBlock* block = Get<SBlock>(stmt.data); block) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_block(stmt.loc, *block);
                print_newline();

            } else if (Get<SDebugger>(stmt.data)) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("debugger");
                print_semicolon_after_statement();

            } else if (const SDirective* directive = Get<SDirective>(stmt.data); directive) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print_quoted_utf16(directive->value, print_quoted_flags::kNone);
                print_semicolon_after_statement();

            } else if (const SBreak* break_stmt = Get<SBreak>(stmt.data); break_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("break");
                if (break_stmt->label != nullptr) {
                    print(" ");
                    std::string name = renamer_.NameForSymbol(break_stmt->label->ref);
                    add_source_mapping_for_name(break_stmt->label->loc, name, break_stmt->label->ref);
                    print_identifier(name);
                }
                print_semicolon_after_statement();

            } else if (const SContinue* continue_stmt = Get<SContinue>(stmt.data); continue_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("continue");
                if (continue_stmt->label != nullptr) {
                    print(" ");
                    std::string name = renamer_.NameForSymbol(continue_stmt->label->ref);
                    add_source_mapping_for_name(continue_stmt->label->loc, name, continue_stmt->label->ref);
                    print_identifier(name);
                }
                print_semicolon_after_statement();

            } else if (const SReturn* return_stmt = Get<SReturn>(stmt.data); return_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("return");
                if (!IsNil(return_stmt->value_or_nil.data)) {
                    print_space();
                    print_expr_without_leading_newline(return_stmt->value_or_nil, L::kLowest, print_expr_flags::kNone);
                }
                print_semicolon_after_statement();

            } else if (const SThrow* throw_stmt = Get<SThrow>(stmt.data); throw_stmt) {
                add_source_mapping(stmt.loc);
                print_indent();
                print_space_before_identifier();
                print("throw");
                print_space();
                print_expr_without_leading_newline(throw_stmt->value, L::kLowest, print_expr_flags::kNone);
                print_semicolon_after_statement();

            } else if (const SExpr* expr_stmt = Get<SExpr>(stmt.data); expr_stmt) {
                Expr value = expr_stmt->value;

                // Omit calls to empty functions from the output completely
                if (options_.minify_syntax) {
                    value = simplify_unused_expr(value);
                    if (IsNil(value.data)) {
                        // If this statement is not in a block, then we still need to emit something
                        if (!Has(flags, print_stmt_flags::kCanOmitStatement)) {
                            // "if (x) empty();" => "if (x) ;"
                            add_source_mapping(stmt.loc);
                            print_indent();
                            print(";");
                            print_newline();
                        }
                        // "if (x) { empty(); }" => "if (x) {}"
                        return;
                    }
                }

                // Avoid printing a source mapping when the expression would print one in
                // the same spot. We don't want to accidentally mask the mapping it emits.
                if (!options_.minify_whitespace && (indent_ > 0 || print_next_indent_as_space_)) {
                    add_source_mapping(stmt.loc);
                    print_indent();
                }

                stmt_start_ = static_cast<int>(js_.size());
                print_expr(value, L::kLowest, print_expr_flags::kExprResultIsUnused);
                print_semicolon_after_statement();

            } else {
                throw std::runtime_error("js_printer.cpp: Unexpected statement while printing");
            }
        }

    } // namespace

} // namespace guchho::javascript