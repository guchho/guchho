#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_printer.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/helpers.hpp"
#include "guchho/compiler.hpp"


namespace guchho::css {

    namespace {

        constexpr char kQuoteForURL = 0;

        enum class PrintQuotedFlags : uint8_t {
            kNone = 0,
            kNoWrap = 1 << 0,
        };

        inline PrintQuotedFlags operator|(PrintQuotedFlags a, PrintQuotedFlags b) {
            return static_cast<PrintQuotedFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
        }

        enum class EscapeKind : uint8_t {
            kNone,
            kBackslash,
            kHex,
        };

        enum class IdentMode : uint8_t {
            kNormal,
            kHash,
            kDimensionUnit,
            kDimensionUnitAfterExponent,
        };

        enum class TrailingWhitespace : uint8_t {
            kMayNeedWhitespaceAfter,
            kCanDiscardWhitespaceAfter,
        };

        enum class SelectorLayout : uint8_t {
            kMultiLine,
            kSingleLine,
        };

        enum class MQFlags : uint8_t {
            kNone = 0,
            kNeedsParens = 1 << 0,
            kAfterIdentifier = 1 << 1,
        };

        struct PrintTokensOpts {
            int32_t indent{};
            uint8_t multi_line_comma_period{};
            bool is_declaration{};
        };

        // Returns the number of UTF-8 bytes needed to encode a Unicode code point.
        // Used to advance through string data by the correct number of bytes.
        // Example: RuneLen('A') => 1, RuneLen(0x00E9) => 2, RuneLen(0x1F600) => 4
        int RuneLen(char32_t c) {
            if (c < 0x80) return 1;
            if (c < 0x800) return 2;
            if (c < 0x10000) return 3;
            return 4;
        }

        // Converts ASCII characters to lowercase. Non-ASCII bytes are passed through
        // unchanged. Used for case-insensitive CSS property and keyword comparisons.
        // Example: ToLowerASCII("COLOR") => "color"
        // Example: ToLowerASCII("Media") => "media"
        std::string ToLowerASCII(std::string_view text) {
            std::string result;
            result.reserve(text.size());
            for (char c : text) {
                result.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
            }
            return result;
        }

        // Performs case-insensitive ASCII string equality comparison. Both strings
        // must have the same length and match character-by-character after folding
        // to lowercase. Returns false if lengths differ.
        // Example: EqualFoldASCII("SCREEN", "screen") => true
        // Example: EqualFoldASCII("Screen", "SCREEN") => true
        // Example: EqualFoldASCII("screen", "print") => false
        bool EqualFoldASCII(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                char ca = a[i];
                char cb = b[i];
                if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
                if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
                if (ca != cb) {
                    return false;
                }
            }
            return true;
        }

        // Encodes a Unicode code point as UTF-8 into the provided buffer. Returns
        // the number of bytes written (1-4). The buffer must have at least 4 bytes
        // of space. This is the inverse of DecodeRune.
        // Example: EncodeRune(buf, 'A') => 1, buf = "A"
        // Example: EncodeRune(buf, 0x00E9) => 2, buf = {0xC3, 0xA9}
        int EncodeRune(char* buf, char32_t c) {
            if (c < 0x80) {
                buf[0] = static_cast<char>(c);
                return 1;
            }
            if (c < 0x800) {
                buf[0] = static_cast<char>(0xC0 | (c >> 6));
                buf[1] = static_cast<char>(0x80 | (c & 0x3F));
                return 2;
            }
            if (c < 0x10000) {
                buf[0] = static_cast<char>(0xE0 | (c >> 12));
                buf[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                buf[2] = static_cast<char>(0x80 | (c & 0x3F));
                return 3;
            }
            buf[0] = static_cast<char>(0xF0 | (c >> 18));
            buf[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            buf[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            buf[3] = static_cast<char>(0x80 | (c & 0x3F));
            return 4;
        }

        // Decodes a single UTF-8 code point starting at byte offset i in string s.
        // Returns the decoded code point and sets width to the number of bytes consumed.
        // Malformed sequences decode to U+FFFD with width 1, ensuring the parser
        // always makes progress even through invalid data.
        // Example: DecodeRune("hello", 0, w) => ('h', 1)
        // Example: DecodeRune("\xC3\xA9", 0, w) => (0x00E9, 2)  -- 'e' with acute accent
        // Example: DecodeRune("\xFF", 0, w) => (0xFFFD, 1)      -- invalid => replacement char
        char32_t DecodeRune(std::string_view s, size_t i, size_t& width) {
            unsigned char b0 = static_cast<unsigned char>(s[i]);
            if (b0 < 0x80) {
                width = 1;
                return b0;
            }
            int n;
            char32_t cp;
            if ((b0 & 0xE0) == 0xC0) {
                n = 2;
                cp = b0 & 0x1F;
            } else if ((b0 & 0xF0) == 0xE0) {
                n = 3;
                cp = b0 & 0x0F;
            } else if ((b0 & 0xF8) == 0xF0) {
                n = 4;
                cp = b0 & 0x07;
            } else {
                width = 1;
                return 0xFFFD;
            }
            if (i + static_cast<size_t>(n) > s.size()) {
                width = 1;
                return 0xFFFD;
            }
            for (int k = 1; k < n; k++) {
                unsigned char bc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
                if ((bc & 0xC0) != 0x80) {
                    width = 1;
                    return 0xFFFD;
                }
                cp = (cp << 6) | (bc & 0x3F);
            }
            width = static_cast<size_t>(n);
            return cp;
        }

    }

    // The Printer class converts a parsed CSS AST back into textual CSS output.
    // It handles all formatting concerns: indentation, whitespace, line wrapping,
    // source map generation, identifier escaping, string quoting, and symbol
    // renaming. The printer supports both minified and pretty-printed output.
    class Printer {
    public:
        Printer(const PrinterOptions& options, compiler::SymbolMap& symbols, const std::vector< compiler::ImportRecord>& import_records)
            : options_(options),
            symbols_(symbols),
            import_records_(import_records),
            builder_(sourcemap::MakeChunkBuilder(options.input_source_map, options.line_offset_tables, options.ascii_only)) {}

        PrintResult Run(const AST& tree);

    private:
        PrinterOptions options_;
        compiler::SymbolMap& symbols_;
        const std::vector< compiler::ImportRecord>& import_records_;
        sourcemap::ChunkBuilder builder_;
        std::string css_;
        std::unordered_set<std::string> has_legal_comment_;
        std::vector<std::string> extracted_legal_comments_;
        std::vector<std::string> json_metadata_imports_;
        int old_line_start_{};
        int old_line_end_{};

        void Print(std::string_view text);
        void Print(char c);
        void PrintIndent(int32_t indent);
        void PrintIdent(const std::string& text, IdentMode mode, TrailingWhitespace whitespace);
        void PrintSymbol(logger::Loc loc, compiler::Ref ref, IdentMode mode, TrailingWhitespace whitespace);
        void PrintWithEscape(char32_t c, EscapeKind escape, std::string_view remaining_text, bool may_need_whitespace_after);
        void PrintQuoted(const std::string& text, PrintQuotedFlags flags);
        void PrintQuotedWithQuote(const std::string& text, char quote, PrintQuotedFlags flags);
        char BestQuoteCharForString(const std::string& text, bool for_url);
        int CurrentLineLength();
        bool PrintNewlinePastLineLimit(int32_t indent);

        void PrintRule(const Rule& rule, int32_t indent, bool omit_trailing_semicolon);
        void PrintRuleBlock(const std::vector<Rule>& rules, int32_t indent, logger::Loc close_brace_loc);
        void PrintIndentedComment(int32_t indent, std::string text);

        void PrintComplexSelectors(const std::vector<ComplexSelector>& selectors, int32_t indent, SelectorLayout layout);
        void PrintCompoundSelector(const CompoundSelector& sel, bool is_first, int32_t indent);
        void PrintNthIndex(const NthIndex& index);
        void PrintNamespacedName(const NamespacedName& ns_name, TrailingWhitespace whitespace);
        void PrintPseudoClassSelector(const SSPseudoClass& pseudo, TrailingWhitespace whitespace);

        bool PrintTokens(const std::vector<Token>& tokens, PrintTokensOpts opts);
        uint8_t FunctionMultiLineCommaPeriod(const Token& token);

        void PrintMediaQuery(const MediaQuery& query, MQFlags flags);

        void RecordImportPathForMetafile(uint32_t import_record_index);
    };

    // Top-level entry point: creates a Printer and runs it over the AST.
    // Returns the printed CSS string, extracted legal comments, and metadata imports.
    // Example: Print(tree, symbols, options) => PrintResult{css: "a{color:red}", ...}
    PrintResult Print(const AST& tree, compiler::SymbolMap& symbols, const PrinterOptions& options) {
        Printer printer(options, symbols, tree.import_records);
        return printer.Run(tree);
    }

    // Runs the printer over all top-level rules in the AST, producing the final
    // CSS output string. After printing, the result includes the CSS text,
    // extracted legal comments (for external comment modes), JSON metadata imports,
    // and an optional source map chunk.
    PrintResult Printer::Run(const AST& tree) {
        for (const Rule& rule : tree.rules) {
            PrintRule(rule, 0, false);
        }
        PrintResult result;
        result.css = std::move(css_);
        result.extracted_legal_comments = std::move(extracted_legal_comments_);
        result.json_metadata_imports = std::move(json_metadata_imports_);
        if (options_.source_map != config::SourceMap::kNone) {
            result.source_map_chunk = builder_.GenerateChunk(result.css);
        }
        return result;
    }

    // Records an import path in the JSON metadata for the metafile output.
    // This is called whenever a url() or @import is printed, so the metafile
    // accurately reflects all external dependencies.
    void Printer::RecordImportPathForMetafile(uint32_t import_record_index) {
        if (options_.needs_metafile) {
            const  compiler::ImportRecord& record = import_records_[import_record_index];
            std::string external;
            if (!compiler::Has(record.flags,  compiler::ImportRecordFlags::kShouldNotBeExternalInMetafile)) {
                external = config::MaybeRemoveWhitespace(options_.metafile_format, ",\n          \"external\": true");
            }
            std::string fmt = config::MaybeRemoveWhitespace(options_.metafile_format,
                                                        "\n        {\n          \"path\": " +
                                                        helpers::QuoteForJSON(record.path.text, options_.ascii_only) +
                                                        ",\n          \"kind\": " +
                                                        helpers::QuoteForJSON(compiler::ImportKindToStringForMetafile(record.kind), options_.ascii_only) +
                                                        external +
                                                        "\n        }");
            json_metadata_imports_.push_back(std::move(fmt));
        }
    }

    // Prints a single CSS rule. This is the main dispatch function that handles
    // every rule type in the AST: at-rules (@charset, @import, @keyframes, @media,
    // @scope, @layer, etc.), selector rules, qualified rules, declarations, bad
    // declarations, comments, and unknown at-rules. Legal comments are extracted
    // rather than printed depending on the legal_comments mode. Source mappings
    // are emitted for each rule when add_source_mappings is enabled. When
    // minify_whitespace is off, indentation and newlines are inserted.
    //
    // The omit_trailing_semicolon parameter is used when printing the last
    // declaration in a minified block, where the trailing semicolon can be
    // dropped to save one byte.
    void Printer::PrintRule(const Rule& rule, int32_t indent, bool omit_trailing_semicolon) {
        if (auto* r_legal_comment = dynamic_cast<const RComment*>(rule.data.get()); r_legal_comment != nullptr) {
            switch (options_.legal_comments) {
            case config::LegalComments::kNone:
                return;

            case config::LegalComments::kEndOfFile:
            case config::LegalComments::kLinkedWithComment:
            case config::LegalComments::kExternalWithoutComment:
                if (has_legal_comment_.count(r_legal_comment->text) != 0) {
                    return;
                }
                has_legal_comment_.insert(r_legal_comment->text);
                extracted_legal_comments_.push_back(r_legal_comment->text);
                return;

            default:
                break;
            }
        }

        if (options_.line_limit > 0) {
            PrintNewlinePastLineLimit(indent);
        }

        if (options_.add_source_mappings) {
            bool should_print_mapping = true;
            if (indent == 0 || options_.minify_whitespace) {
                if (dynamic_cast<const RSelector*>(rule.data.get()) != nullptr ||
                    dynamic_cast<const RQualified*>(rule.data.get()) != nullptr ||
                    dynamic_cast<const RBadDeclaration*>(rule.data.get()) != nullptr) {
                    should_print_mapping = false;
                }
            }
            if (should_print_mapping) {
                builder_.AddSourceMapping(rule.loc, "", css_);
            }
        }

        if (!options_.minify_whitespace) {
            PrintIndent(indent);
        }

        if (auto* r_charset = dynamic_cast<const RAtCharset*>(rule.data.get()); r_charset != nullptr) {
            Print("@charset ");
            PrintQuotedWithQuote(r_charset->encoding, '"', PrintQuotedFlags::kNone);
            Print(";");
        } else if (auto* r_import = dynamic_cast<const RAtImport*>(rule.data.get()); r_import != nullptr) {
            if (options_.minify_whitespace) {
                Print("@import");
            } else {
                Print("@import ");
            }
            const  compiler::ImportRecord& record = import_records_[r_import->import_record_index];
            PrintQuotedFlags flags = PrintQuotedFlags::kNone;
            if (compiler::Has(record.flags,  compiler::ImportRecordFlags::kContainsUniqueKey)) {
                flags = flags | PrintQuotedFlags::kNoWrap;
            }
            PrintQuoted(record.path.text, flags);
            RecordImportPathForMetafile(r_import->import_record_index);
            if (r_import->import_conditions != nullptr) {
                bool space = !options_.minify_whitespace;
                if (!r_import->import_conditions->layers.empty()) {
                    if (space) {
                        Print(" ");
                    }
                    PrintTokens(r_import->import_conditions->layers, PrintTokensOpts{});
                    space = true;
                }
                if (!r_import->import_conditions->supports.empty()) {
                    if (space) {
                        Print(" ");
                    }
                    PrintTokens(r_import->import_conditions->supports, PrintTokensOpts{});
                    space = true;
                }
                if (!r_import->import_conditions->queries.empty()) {
                    if (space) {
                        Print(" ");
                    }
                    for (size_t i = 0; i < r_import->import_conditions->queries.size(); i++) {
                        if (i > 0) {
                            if (options_.minify_whitespace) {
                                Print(",");
                            } else {
                                Print(", ");
                            }
                        }
                        PrintMediaQuery(r_import->import_conditions->queries[i], MQFlags::kNone);
                    }
                }
            }
            Print(";");
        } else if (auto* r_keyframes = dynamic_cast<const RAtKeyframes*>(rule.data.get()); r_keyframes != nullptr) {
            Print("@");
            PrintIdent(r_keyframes->at_token, IdentMode::kNormal, TrailingWhitespace::kMayNeedWhitespaceAfter);
            Print(" ");
            PrintSymbol(r_keyframes->name.loc, r_keyframes->name.ref, IdentMode::kNormal, TrailingWhitespace::kCanDiscardWhitespaceAfter);
            if (!options_.minify_whitespace) {
                Print(" ");
            }
            if (options_.minify_whitespace) {
                Print("{");
            } else {
                Print("{\n");
            }
            indent++;
            for (const KeyframeBlock& block : r_keyframes->blocks) {
                if (options_.add_source_mappings) {
                    builder_.AddSourceMapping(block.loc, "", css_);
                }
                if (!options_.minify_whitespace) {
                    PrintIndent(indent);
                }
                for (size_t i = 0; i < block.selectors.size(); i++) {
                    if (i > 0) {
                        if (options_.minify_whitespace) {
                            Print(",");
                        } else {
                            Print(", ");
                        }
                    }
                    Print(block.selectors[i]);
                }
                if (!options_.minify_whitespace) {
                    Print(" ");
                }
                PrintRuleBlock(block.rules, indent, block.close_brace_loc);
                if (!options_.minify_whitespace) {
                    Print("\n");
                }
            }
            indent--;
            if (options_.add_source_mappings && r_keyframes->close_brace_loc.start != 0) {
                builder_.AddSourceMapping(r_keyframes->close_brace_loc, "", css_);
            }
            if (!options_.minify_whitespace) {
                PrintIndent(indent);
            }
            Print("}");
        } else if (auto* r_known_at = dynamic_cast<const RKnownAt*>(rule.data.get()); r_known_at != nullptr) {
            Print("@");
            TrailingWhitespace whitespace = TrailingWhitespace::kMayNeedWhitespaceAfter;
            if (r_known_at->prelude.empty()) {
                whitespace = TrailingWhitespace::kCanDiscardWhitespaceAfter;
            }
            PrintIdent(r_known_at->at_token, IdentMode::kNormal, whitespace);
            if ((!options_.minify_whitespace && !r_known_at->rules.empty()) || !r_known_at->prelude.empty()) {
                Print(" ");
            }
            PrintTokens(r_known_at->prelude, PrintTokensOpts{});
            if (r_known_at->rules.empty()) {
                Print(";");
            } else {
                if (!options_.minify_whitespace && !r_known_at->prelude.empty()) {
                    Print(" ");
                }
                PrintRuleBlock(r_known_at->rules, indent, r_known_at->close_brace_loc);
            }
        } else if (auto* r_unknown_at = dynamic_cast<const RUnknownAt*>(rule.data.get()); r_unknown_at != nullptr) {
            Print("@");
            TrailingWhitespace whitespace = TrailingWhitespace::kMayNeedWhitespaceAfter;
            if (r_unknown_at->prelude.empty()) {
                whitespace = TrailingWhitespace::kCanDiscardWhitespaceAfter;
            }
            PrintIdent(r_unknown_at->at_token, IdentMode::kNormal, whitespace);
            if ((!options_.minify_whitespace && !r_unknown_at->block.empty()) || !r_unknown_at->prelude.empty()) {
                Print(" ");
            }
            PrintTokens(r_unknown_at->prelude, PrintTokensOpts{});
            if (!options_.minify_whitespace && !r_unknown_at->block.empty() && !r_unknown_at->prelude.empty()) {
                Print(" ");
            }
            if (r_unknown_at->block.empty()) {
                Print(";");
            } else {
                PrintTokens(r_unknown_at->block, PrintTokensOpts{});
            }
        } else if (auto* r_selector = dynamic_cast<const RSelector*>(rule.data.get()); r_selector != nullptr) {
            PrintComplexSelectors(r_selector->selectors, indent, SelectorLayout::kMultiLine);
            if (!options_.minify_whitespace) {
                Print(" ");
            }
            PrintRuleBlock(r_selector->rules, indent, r_selector->close_brace_loc);
        } else if (auto* r_qualified = dynamic_cast<const RQualified*>(rule.data.get()); r_qualified != nullptr) {
            bool has_whitespace_after = PrintTokens(r_qualified->prelude, PrintTokensOpts{});
            if (!has_whitespace_after && !options_.minify_whitespace) {
                Print(" ");
            }
            PrintRuleBlock(r_qualified->rules, indent, r_qualified->close_brace_loc);
        } else if (auto* r_decl = dynamic_cast<const RDeclaration*>(rule.data.get()); r_decl != nullptr) {
            PrintIdent(r_decl->key_text, IdentMode::kNormal, TrailingWhitespace::kCanDiscardWhitespaceAfter);
            Print(":");
            PrintTokensOpts opts;
            opts.indent = indent;
            opts.is_declaration = true;
            bool has_whitespace_after = PrintTokens(r_decl->value, opts);
            if (r_decl->important) {
                if (!has_whitespace_after && !options_.minify_whitespace && !r_decl->value.empty()) {
                    Print(" ");
                }
                Print("!important");
            }
            if (!omit_trailing_semicolon) {
                Print(";");
            }
        } else if (auto* r_bad_decl = dynamic_cast<const RBadDeclaration*>(rule.data.get()); r_bad_decl != nullptr) {
            PrintTokens(r_bad_decl->tokens, PrintTokensOpts{});
            if (!omit_trailing_semicolon) {
                Print(";");
            }
        } else if (auto* r_comment = dynamic_cast<const RComment*>(rule.data.get()); r_comment != nullptr) {
            PrintIndentedComment(indent, r_comment->text);
        } else if (auto* r_layer = dynamic_cast<const RAtLayer*>(rule.data.get()); r_layer != nullptr) {
            Print("@layer");
            for (size_t i = 0; i < r_layer->names.size(); i++) {
                if (i == 0) {
                    Print(" ");
                } else if (!options_.minify_whitespace) {
                    Print(", ");
                } else {
                    Print(",");
                }
                std::string joined;
                for (size_t j = 0; j < r_layer->names[i].size(); j++) {
                    if (j > 0) {
                        joined += ".";
                    }
                    joined += r_layer->names[i][j];
                }
                Print(joined);
            }
            if (!r_layer->has_block) {
                Print(";");
            } else {
                if (!options_.minify_whitespace) {
                    Print(" ");
                }
                PrintRuleBlock(r_layer->rules, indent, r_layer->close_brace_loc);
            }
        } else if (auto* r_media = dynamic_cast<const RAtMedia*>(rule.data.get()); r_media != nullptr) {
            Print("@media");
            MQFlags flags = MQFlags::kNone;
            if (options_.minify_whitespace) {
                flags = MQFlags::kAfterIdentifier;
            } else {
                Print(" ");
            }
            for (size_t i = 0; i < r_media->queries.size(); i++) {
                if (i > 0) {
                    if (options_.minify_whitespace) {
                        Print(",");
                    } else {
                        Print(", ");
                    }
                }
                PrintMediaQuery(r_media->queries[i], flags);
                flags = MQFlags::kNone;
            }
            if (!options_.minify_whitespace && !r_media->queries.empty()) {
                Print(" ");
            }
            PrintRuleBlock(r_media->rules, indent, r_media->close_brace_loc);
        } else if (auto* r_scope = dynamic_cast<const RAtScope*>(rule.data.get()); r_scope != nullptr) {
            Print("@scope");
            if (!r_scope->start.empty()) {
                if (options_.minify_whitespace) {
                    Print("(");
                } else {
                    Print(" (");
                }
                PrintComplexSelectors(r_scope->start, indent, SelectorLayout::kSingleLine);
                Print(")");
            }
            if (!r_scope->end.empty()) {
                if (options_.minify_whitespace) {
                    Print("to (");
                } else {
                    Print(" to (");
                }
                PrintComplexSelectors(r_scope->end, indent, SelectorLayout::kSingleLine);
                Print(")");
            }
            if (!options_.minify_whitespace) {
                Print(" ");
            }
            PrintRuleBlock(r_scope->rules, indent, r_scope->close_brace_loc);
        } else {
            std::abort();
        }

        if (!options_.minify_whitespace) {
            Print("\n");
        }
    }

    // Recursively prints a media query AST node. Handles all media query types:
    // arbitrary token sequences, typed queries (screen, print), negated queries,
    // binary combinations (and/or), plain feature queries (min-width: 600px),
    // and range queries (400px <= width <= 800px). The flags parameter controls
    // parenthesization and spacing based on the parent context.
    //
    // Example: PrintMediaQuery(MQType("screen", and=MQPlain("min-width", "600px")))
    //   => "screen and (min-width: 600px)"
    // Example: PrintMediaQuery(MQNot(MQPlain("print")))
    //   => "not print"
    void Printer::PrintMediaQuery(const MediaQuery& query, MQFlags flags) {
        if (auto q = std::dynamic_pointer_cast<const MQArbitraryTokens>(query.data); q != nullptr) {
            if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kAfterIdentifier)) != 0) {
                Print(" ");
            }
            PrintTokens(q->tokens, PrintTokensOpts{});
            return;
        }

        if (auto q_type = std::dynamic_pointer_cast<const MQType>(query.data); q_type != nullptr) {
            if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kAfterIdentifier)) != 0) {
                Print(" ");
            }
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(query.loc, "", css_);
            }
            switch (q_type->op) {
            case MQTypeOp::kMQTypeOpNot:
                Print("not ");
                break;
            case MQTypeOp::kMQTypeOpOnly:
                Print("only ");
                break;
            default:
                break;
            }
            PrintIdent(q_type->type, IdentMode::kNormal, TrailingWhitespace::kMayNeedWhitespaceAfter);
            if (q_type->and_or_null.data != nullptr) {
                Print(" and ");
                MQFlags child_flags = MQFlags::kNone;
                if (auto binary = std::dynamic_pointer_cast<const MQBinary>(q_type->and_or_null.data);
                    binary != nullptr && binary->op == MQBinaryOp::kMQBinaryOpOr) {
                    child_flags = MQFlags::kNeedsParens;
                }
                PrintMediaQuery(q_type->and_or_null, child_flags);
            }
        } else if (auto q_not = std::dynamic_pointer_cast<const MQNot>(query.data); q_not != nullptr) {
            if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kNeedsParens)) != 0) {
                Print("(");
            } else if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kAfterIdentifier)) != 0) {
                Print(" ");
            }
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(query.loc, "", css_);
            }
            Print("not ");
            PrintMediaQuery(q_not->inner, MQFlags::kNeedsParens);
            if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kNeedsParens)) != 0) {
                Print(")");
            }
        } else if (auto q_binary = std::dynamic_pointer_cast<const MQBinary>(query.data); q_binary != nullptr) {
            if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kNeedsParens)) != 0) {
                Print("(");
            }
            for (size_t i = 0; i < q_binary->terms.size(); i++) {
                if (i > 0) {
                    if (!options_.minify_whitespace) {
                        Print(" ");
                    }
                    switch (q_binary->op) {
                    case MQBinaryOp::kMQBinaryOpAnd:
                        Print("and ");
                        break;
                    case MQBinaryOp::kMQBinaryOpOr:
                        Print("or ");
                        break;
                    }
                }
                PrintMediaQuery(q_binary->terms[i], MQFlags::kNeedsParens);
            }
            if ((static_cast<uint8_t>(flags) & static_cast<uint8_t>(MQFlags::kNeedsParens)) != 0) {
                Print(")");
            }
        } else if (auto q_plain_or_boolean = std::dynamic_pointer_cast<const MQPlainOrBoolean>(query.data); q_plain_or_boolean != nullptr) {
            Print("(");
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(query.loc, "", css_);
            }
            PrintIdent(q_plain_or_boolean->name, IdentMode::kNormal, TrailingWhitespace::kMayNeedWhitespaceAfter);
            if (!q_plain_or_boolean->value_or_nil.empty()) {
                if (options_.minify_whitespace) {
                    Print(":");
                } else {
                    Print(": ");
                }
                PrintTokens(q_plain_or_boolean->value_or_nil, PrintTokensOpts{});
            }
            Print(")");
        } else if (auto q_range = std::dynamic_pointer_cast<const MQRange>(query.data); q_range != nullptr) {
            const char* space = " ";
            if (options_.minify_whitespace) {
                space = "";
            }
            Print("(");
            if (q_range->before_cmp != MQCmp::kMQCmpNone) {
                PrintTokens(q_range->before, PrintTokensOpts{});
                Print(space);
                Print(ToString(q_range->before_cmp));
                Print(space);
            }
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(q_range->name_loc, "", css_);
            }
            PrintIdent(q_range->name, IdentMode::kNormal, TrailingWhitespace::kMayNeedWhitespaceAfter);
            if (q_range->after_cmp != MQCmp::kMQCmpNone) {
                Print(space);
                Print(ToString(q_range->after_cmp));
                Print(space);
                PrintTokens(q_range->after, PrintTokensOpts{});
            }
            Print(")");
        }
    }

    // Prints a comment with proper indentation. Multi-line comments are
    // re-indented so each line aligns with the current nesting level. The
    // "</style" sequence is escaped to prevent premature script execution
    // when CSS is embedded in HTML.
    void Printer::PrintIndentedComment(int32_t indent, std::string text) {
        if (!compat::Has(options_.unsupported_features, compat::CSSFeature::kInlineStyle)) {
            text = helpers::EscapeClosingTag(text, "/style");
        }

        for (;;) {
            size_t newline = text.find('\n');
            if (newline == std::string::npos) {
                break;
            }
            Print(text.substr(0, newline + 1));
            if (!options_.minify_whitespace) {
                PrintIndent(indent);
            }
            text = text.substr(newline + 1);
        }
        Print(text);
    }

    // Prints a block of CSS rules enclosed in braces. In minified mode,
    // the opening brace is on the same line; in pretty mode, it is followed
    // by a newline. The last rule in minified mode can omit its trailing
    // semicolon. A source mapping is emitted for the closing brace location.
    void Printer::PrintRuleBlock(const std::vector<Rule>& rules, int32_t indent, logger::Loc close_brace_loc) {
        if (options_.minify_whitespace) {
            Print("{");
        } else {
            Print("{\n");
        }

        for (size_t i = 0; i < rules.size(); i++) {
            bool omit_trailing_semicolon = options_.minify_whitespace && i + 1 == rules.size();
            PrintRule(rules[i], indent + 1, omit_trailing_semicolon);
        }

        if (options_.add_source_mappings && close_brace_loc.start != 0) {
            builder_.AddSourceMapping(close_brace_loc, "", css_);
        }
        if (!options_.minify_whitespace) {
            PrintIndent(indent);
        }
        Print("}");
    }

    // Prints a comma-separated list of complex selectors. In multi-line mode
    // (the default for rule selectors), each selector gets its own line with
    // proper indentation. In single-line mode (used inside @scope and :is()),
    // selectors are separated by ", ". In minified mode, commas have no
    // trailing space and line limits are respected.
    void Printer::PrintComplexSelectors(const std::vector<ComplexSelector>& selectors, int32_t indent, SelectorLayout layout) {
        for (size_t i = 0; i < selectors.size(); i++) {
            if (i > 0) {
                if (options_.minify_whitespace) {
                    Print(",");
                    if (options_.line_limit > 0) {
                        PrintNewlinePastLineLimit(indent);
                    }
                } else if (layout == SelectorLayout::kMultiLine) {
                    Print(",\n");
                    PrintIndent(indent);
                } else {
                    Print(", ");
                }
            }

            for (size_t j = 0; j < selectors[i].selectors.size(); j++) {
                PrintCompoundSelector(selectors[i].selectors[j], j == 0, indent);
            }
        }
    }

    // Prints a single compound selector, which consists of an optional type
    // selector, optional nesting selectors (&), and zero or more subclass
    // selectors (classes, IDs, attributes, pseudo-classes). Combinators
    // between compound selectors are printed with appropriate spacing.
    //
    // The is_first flag determines whether a leading space is needed before
    // a descendant combinator. Whitespace between adjacent subclass selectors
    // is omitted since they are always directly adjacent (e.g., ".a.b").
    void Printer::PrintCompoundSelector(const CompoundSelector& sel, bool is_first, int32_t indent) {
        if (!is_first && sel.combinator.byte_ == 0) {
            if (options_.line_limit <= 0 || !PrintNewlinePastLineLimit(indent)) {
                Print(" ");
            }
        }

        if (sel.combinator.byte_ != 0) {
            if (!is_first && !options_.minify_whitespace) {
                Print(" ");
            }

            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(sel.combinator.loc, "", css_);
            }
            css_.push_back(static_cast<char>(sel.combinator.byte_));

            if ((options_.line_limit <= 0 || !PrintNewlinePastLineLimit(indent)) && !options_.minify_whitespace) {
                Print(" ");
            }
        }

        if (sel.type_selector != nullptr) {
            TrailingWhitespace whitespace = TrailingWhitespace::kMayNeedWhitespaceAfter;
            if (!sel.subclass_selectors.empty()) {
                whitespace = TrailingWhitespace::kCanDiscardWhitespaceAfter;
            }
            PrintNamespacedName(*sel.type_selector, whitespace);
        }

        for (logger::Loc loc : sel.nesting_selector_locs) {
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(loc, "", css_);
            }
            Print("&");
        }

        for (size_t i = 0; i < sel.subclass_selectors.size(); i++) {
            TrailingWhitespace whitespace = TrailingWhitespace::kMayNeedWhitespaceAfter;

            if (i + 1 < sel.subclass_selectors.size()) {
                whitespace = TrailingWhitespace::kCanDiscardWhitespaceAfter;
            }

            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(sel.subclass_selectors[i].range.loc, "", css_);
            }

            const SS* ss = sel.subclass_selectors[i].data.get();
            if (auto* s_hash = dynamic_cast<const SSHash*>(ss); s_hash != nullptr) {
                Print("#");
                PrintSymbol(s_hash->name.loc, s_hash->name.ref, IdentMode::kNormal, whitespace);
            } else if (auto* s_class = dynamic_cast<const SSClass*>(ss); s_class != nullptr) {
                Print(".");
                PrintSymbol(s_class->name.loc, s_class->name.ref, IdentMode::kNormal, whitespace);
            } else if (auto* s_attribute = dynamic_cast<const SSAttribute*>(ss); s_attribute != nullptr) {
                Print("[");
                PrintNamespacedName(s_attribute->namespaced_name, TrailingWhitespace::kCanDiscardWhitespaceAfter);
                if (!s_attribute->matcher_op.empty()) {
                    Print(s_attribute->matcher_op);
                    bool print_as_ident = false;

                    if (WouldStartIdentifierWithoutEscapes(s_attribute->matcher_value)) {
                        print_as_ident = true;
                        size_t k = 0;
                        while (k < s_attribute->matcher_value.size()) {
                            size_t w;
                            char32_t c = DecodeRune(s_attribute->matcher_value, k, w);
                            if (!IsNameContinue(c)) {
                                print_as_ident = false;
                                break;
                            }
                            k += w;
                        }
                    }

                    if (print_as_ident) {
                        PrintIdent(s_attribute->matcher_value, IdentMode::kNormal, TrailingWhitespace::kCanDiscardWhitespaceAfter);
                    } else {
                        PrintQuoted(s_attribute->matcher_value, PrintQuotedFlags::kNone);
                    }
                }
                if (s_attribute->matcher_modifier != 0) {
                    Print(" ");
                    css_.push_back(static_cast<char>(s_attribute->matcher_modifier));
                }
                Print("]");
            } else if (auto* s_pseudo_class = dynamic_cast<const SSPseudoClass*>(ss); s_pseudo_class != nullptr) {
                PrintPseudoClassSelector(*s_pseudo_class, whitespace);
            } else if (auto* s_pseudo_class_with_selector_list = dynamic_cast<const SSPseudoClassWithSelectorList*>(ss); s_pseudo_class_with_selector_list != nullptr) {
                Print(":");
                Print(ToString(s_pseudo_class_with_selector_list->kind));
                Print("(");
                if (!s_pseudo_class_with_selector_list->index.a.empty() || !s_pseudo_class_with_selector_list->index.b.empty()) {
                    PrintNthIndex(s_pseudo_class_with_selector_list->index);
                    if (!s_pseudo_class_with_selector_list->selectors.empty()) {
                        if (options_.minify_whitespace && s_pseudo_class_with_selector_list->selectors[0].selectors[0].type_selector == nullptr) {
                            Print(" of");
                        } else {
                            Print(" of ");
                        }
                    }
                }
                PrintComplexSelectors(s_pseudo_class_with_selector_list->selectors, indent, SelectorLayout::kSingleLine);
                Print(")");
            }
        }
    }

    // Prints an An+B nth-index expression. Handles all CSS syntax variants:
    // "odd", "even", "5", "-n+3", "2n+1", "-n", etc. The coefficient "a" and
    // offset "b" are printed according to CSS shorthand rules where 1 is
    // implied and -1 is represented by just "-n".
    //
    // Example: PrintNthIndex({a="2", b="1"}) => "2n+1"
    // Example: PrintNthIndex({a="-1", b="3"}) => "-n+3"
    // Example: PrintNthIndex({a="", b="5"}) => "5"
    void Printer::PrintNthIndex(const NthIndex& index) {
        if (!index.a.empty()) {
            if (index.a == "-1") {
                Print("-");
            } else if (index.a != "1") {
                Print(index.a);
            }
            Print("n");
            if (!index.b.empty()) {
                if (index.b[0] != '-') {
                    Print("+");
                }
                Print(index.b);
            }
        } else if (!index.b.empty()) {
            Print(index.b);
        }
    }

    // Prints a namespaced name, which consists of an optional namespace prefix
    // followed by a local name. The namespace can be an identifier, an asterisk
    // (universal namespace), or absent. The local name can be an identifier,
    // asterisk, or ampersand (nesting selector).
    //
    // Example: PrintNamespacedName(ns="svg", name="circle") => "svg|circle"
    // Example: PrintNamespacedName(ns=*, name="*") => "*|*"
    // Example: PrintNamespacedName(ns=null, name="div") => "div"
    void Printer::PrintNamespacedName(const NamespacedName& ns_name, TrailingWhitespace whitespace) {
        if (ns_name.namespace_prefix != nullptr) {
            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(ns_name.namespace_prefix->range.loc, "", css_);
            }

            switch (ns_name.namespace_prefix->kind) {
            case TokenType::kIdent:
                PrintIdent(ns_name.namespace_prefix->text, IdentMode::kNormal, TrailingWhitespace::kCanDiscardWhitespaceAfter);
                break;
            case TokenType::kDelimAsterisk:
                Print("*");
                break;
            default:
                std::abort();
            }

            Print("|");
        }

        if (options_.add_source_mappings) {
            builder_.AddSourceMapping(ns_name.name.range.loc, "", css_);
        }

        switch (ns_name.name.kind) {
        case TokenType::kIdent:
            PrintIdent(ns_name.name.text, IdentMode::kNormal, whitespace);
            break;
        case TokenType::kDelimAsterisk:
            Print("*");
            break;
        case TokenType::kDelimAmpersand:
            Print("&");
            break;
        default:
            std::abort();
        }
    }

    // Prints a pseudo-class or pseudo-element selector. Distinguishes between
    // single-colon pseudo-classes (:hover) and double-colon pseudo-elements
    // (::before). For functional pseudo-classes like :is() and :not(), the
    // argument list is printed inside parentheses.
    //
    // Example: PrintPseudoClassSelector(SSPseudoClass("hover", is_element=false))
    //   => ":hover"
    // Example: PrintPseudoClassSelector(SSPseudoClass("before", is_element=true))
    //   => "::before"
    void Printer::PrintPseudoClassSelector(const SSPseudoClass& pseudo, TrailingWhitespace whitespace) {
        if (pseudo.is_element) {
            Print("::");
        } else {
            Print(":");
        }

        if (pseudo.args != nullptr) {
            PrintIdent(pseudo.name, IdentMode::kNormal, TrailingWhitespace::kCanDiscardWhitespaceAfter);
            Print("(");
            PrintTokens(*pseudo.args, PrintTokensOpts{});
            Print(")");
        } else {
            PrintIdent(pseudo.name, IdentMode::kNormal, whitespace);
        }
    }

    void Printer::Print(std::string_view text) {
        css_.append(text.data(), text.size());
    }

    void Printer::Print(char c) {
        css_.push_back(c);
    }

    // Determines the best quote character for a string or URL value. Counts the
    // "cost" of each quoting strategy based on characters that need escaping.
    // For URLs, the cost of using no quotes at all is also computed. Prefers
    // double quotes when costs are equal.
    //
    // Example: BestQuoteCharForString("hello world", false) => '"'
    // Example: BestQuoteCharForString("it's", false) => '"'   -- single quote has cost
    // Example: BestQuoteCharForString("a b", true) => 0       -- unquoted is cheapest for URLs
    char Printer::BestQuoteCharForString(const std::string& text, bool for_url) {
        int for_url_cost = 0;
        int single_cost = 2;
        int double_cost = 2;

        for (char ch : text) {
            unsigned char c = static_cast<unsigned char>(ch);
            switch (c) {
            case '\'':
                for_url_cost++;
                single_cost++;
                break;

            case '"':
                for_url_cost++;
                double_cost++;
                break;

            case '(':
            case ')':
            case ' ':
            case '\t':
                for_url_cost++;
                break;

            case '\\':
            case '\n':
            case '\r':
            case '\f':
                for_url_cost++;
                single_cost++;
                double_cost++;
                break;

            default:
                break;
            }
        }

        if (for_url && for_url_cost < single_cost && for_url_cost < double_cost) {
            return kQuoteForURL;
        }

        if (single_cost < double_cost) {
            return '\'';
        }

        return '"';
    }

    void Printer::PrintQuoted(const std::string& text, PrintQuotedFlags flags) {
        PrintQuotedWithQuote(text, BestQuoteCharForString(text, false), flags);
    }

    // Prints a single character with the appropriate escape encoding. Hex escapes
    // are used for characters that cannot be represented with a simple backslash
    // escape (like null bytes or control characters). A trailing space is appended
    // after hex escapes when the next character could be misinterpreted as part
    // of the escape sequence (e.g., hex digits or whitespace).
    void Printer::PrintWithEscape(char32_t c, EscapeKind escape, std::string_view remaining_text, bool may_need_whitespace_after) {
        char temp[4];

        if (escape == EscapeKind::kBackslash && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            escape = EscapeKind::kHex;
        }

        switch (escape) {
        case EscapeKind::kNone: {
            int width = EncodeRune(temp, c);
            css_.append(temp, static_cast<size_t>(width));
            break;
        }

        case EscapeKind::kBackslash:
            css_.push_back('\\');
            {
                int width = EncodeRune(temp, c);
                css_.append(temp, static_cast<size_t>(width));
            }
            break;

        case EscapeKind::kHex: {
            char text[32];
            int len = std::snprintf(text, sizeof(text), "\\%x", static_cast<unsigned>(c));
            css_.append(text, static_cast<size_t>(len));

            if (len < 1 + 6) {
                int next = RuneLen(c);
                if (next < static_cast<int>(remaining_text.size())) {
                    char c2 = remaining_text[static_cast<size_t>(next)];
                    if (c2 == ' ' || c2 == '\t' || (c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'f') ||
                        (c2 >= 'A' && c2 <= 'F')) {
                        css_.push_back(' ');
                    }
                } else if (may_need_whitespace_after) {
                    css_.push_back(' ');
                }
            }
            break;
        }
        }
    }

    // Prints a quoted string with proper escaping. This is a performance-critical
    // function that handles all the complexity of CSS string escaping: null bytes,
    // newlines, backslashes, quote characters, and the </style safety escape.
    // Long lines are wrapped using escaped newlines when line_limit is set.
    // When printing URL values (quote == kQuoteForURL), parentheses, spaces, and
    // quotes are also escaped since URLs are unquoted.
    //
    // The function uses a "run" optimization: consecutive characters that need
    // no escaping are batch-appended to the output buffer, avoiding per-character
    // overhead for the common case.
    void Printer::PrintQuotedWithQuote(const std::string& text, char quote, PrintQuotedFlags flags) {
        if (quote != kQuoteForURL) {
            css_.push_back(quote);
        }

        size_t n = text.size();
        size_t i = 0;
        size_t run_start = 0;

        int start_line_length = 0;
        bool wrap_long_lines = false;
        if (options_.line_limit > 0 && quote != kQuoteForURL &&
            (static_cast<uint8_t>(flags) & static_cast<uint8_t>(PrintQuotedFlags::kNoWrap)) == 0) {
            start_line_length = CurrentLineLength();
            if (start_line_length > options_.line_limit) {
                start_line_length = options_.line_limit;
            }
            wrap_long_lines = true;
        }

        while (i < n) {
            if (wrap_long_lines && start_line_length + static_cast<int>(i) >= options_.line_limit) {
                if (run_start < i) {
                    css_.append(text, run_start, i - run_start);
                    run_start = i;
                }
                css_.append("\\\n", 2);
                start_line_length -= options_.line_limit;
            }

            size_t width;
            char32_t c = DecodeRune(text, i, width);
            EscapeKind escape = EscapeKind::kNone;

            switch (c) {
            case 0x00:
            case '\r':
            case '\n':
            case '\f':
                escape = EscapeKind::kHex;
                break;

            case '\\':
                escape = EscapeKind::kBackslash;
                break;

            case '(':
            case ')':
            case ' ':
            case '\t':
            case '"':
            case '\'':
                if (quote == kQuoteForURL) {
                    escape = EscapeKind::kBackslash;
                }
                break;

            case '/':
                if (!compat::Has(options_.unsupported_features, compat::CSSFeature::kInlineStyle) && i >= 1 && text[i - 1] == '<' &&
                    i + 6 <= n && EqualFoldASCII(std::string_view(text).substr(i + 1, 5), "style")) {
                    escape = EscapeKind::kBackslash;
                }
                break;

            default:
                if ((options_.ascii_only && c >= 0x80) || c == 0xFEFF) {
                    escape = EscapeKind::kHex;
                }
                break;
            }

            if (c == static_cast<char32_t>(static_cast<unsigned char>(quote))) {
                escape = EscapeKind::kBackslash;
            }

            if (escape != EscapeKind::kNone) {
                if (run_start < i) {
                    css_.append(text, run_start, i - run_start);
                }
                PrintWithEscape(c, escape, std::string_view(text).substr(i), false);
                run_start = i + width;
            }
            i += width;
        }

        if (run_start < n) {
            css_.append(text, run_start, n - run_start);
        }

        if (quote != kQuoteForURL) {
            css_.push_back(quote);
        }
    }

    // Returns the length of the current line in the output buffer. This is used
    // for line-wrapping decisions. The function caches the last known newline
    // position to avoid scanning from the beginning of the buffer on every call.
    int Printer::CurrentLineLength() {
        size_t n = css_.size();
        size_t stop = static_cast<size_t>(old_line_end_);

        for (size_t i = n; i > stop; i--) {
            char c = css_[i - 1];
            if (c == '\r' || c == '\n') {
                old_line_start_ = static_cast<int>(i);
                break;
            }
        }

        old_line_end_ = static_cast<int>(n);
        return static_cast<int>(n) - old_line_start_;
    }

    // If the current line exceeds the line limit, inserts a newline and
    // optional indentation. Returns true if a newline was inserted.
    bool Printer::PrintNewlinePastLineLimit(int32_t indent) {
        if (CurrentLineLength() < options_.line_limit) {
            return false;
        }
        Print("\n");
        if (!options_.minify_whitespace) {
            PrintIndent(indent);
        }
        return true;
    }

    // Prints a CSS identifier with proper escaping. This is a performance-
    // critical function with a fast path for identifiers containing only ASCII
    // name-continuation characters (the common case). The slow path handles
    // characters that need backslash or hex escaping. Dimension units get
    // special treatment: a leading digit or "e"/"E" followed by a digit is
    // hex-escaped to avoid being parsed as a number.
    //
    // Example: PrintIdent("color", kNormal, kMayNeedWhitespaceAfter) => "color"
    // Example: PrintIdent("my class", kNormal, ...) => "my\\ class"
    // Example: PrintIdent("2x", kDimensionUnit, ...) => "\\32 x"
    void Printer::PrintIdent(const std::string& text, IdentMode mode, TrailingWhitespace whitespace) {
        size_t n = text.size();

        EscapeKind initial_escape = EscapeKind::kNone;
        switch (mode) {
        case IdentMode::kNormal:
            if (!WouldStartIdentifierWithoutEscapes(text)) {
                initial_escape = EscapeKind::kBackslash;
            }
            break;
        case IdentMode::kHash:
            break;
        case IdentMode::kDimensionUnit:
        case IdentMode::kDimensionUnitAfterExponent:
            if (!WouldStartIdentifierWithoutEscapes(text)) {
                initial_escape = EscapeKind::kBackslash;
            } else if (n > 0) {
                char c = text[0];
                if (c >= '0' && c <= '9') {
                    initial_escape = EscapeKind::kHex;
                } else if ((c == 'e' || c == 'E') && mode != IdentMode::kDimensionUnitAfterExponent) {
                    if (n >= 2 && text[1] >= '0' && text[1] <= '9') {
                        initial_escape = EscapeKind::kHex;
                    } else if (n >= 3 && text[1] == '-' && text[2] >= '0' && text[2] <= '9') {
                        initial_escape = EscapeKind::kHex;
                    }
                }
            }
            break;
        }

        if (initial_escape == EscapeKind::kNone) {
            bool slow_path = false;
            for (size_t i = 0; i < n; i++) {
                char c = text[i];
                if (static_cast<unsigned char>(c) >= 0x80 || !IsNameContinue(static_cast<char32_t>(static_cast<unsigned char>(c)))) {
                    slow_path = true;
                    break;
                }
            }
            if (!slow_path) {
                css_.append(text);
                return;
            }
        }

        size_t i = 0;
        while (i < n) {
            size_t width;
            char32_t c = DecodeRune(text, i, width);
            EscapeKind escape = EscapeKind::kNone;

            if (options_.ascii_only && c >= 0x80) {
                escape = EscapeKind::kHex;
            } else if (c == '\r' || c == '\n' || c == '\f' || c == 0xFEFF) {
                escape = EscapeKind::kHex;
            } else {
                if (!IsNameContinue(c)) {
                    escape = EscapeKind::kBackslash;
                }

                if (i == 0 && initial_escape != EscapeKind::kNone) {
                    escape = initial_escape;
                }
            }

            bool may_need_whitespace_after =
                whitespace == TrailingWhitespace::kMayNeedWhitespaceAfter && escape != EscapeKind::kNone && i + width == n;
            PrintWithEscape(c, escape, std::string_view(text).substr(i), may_need_whitespace_after);
            i += width;
        }
    }

    // Prints a symbol by looking up its original name and any renamed version
    // from the symbol map. If the symbol was renamed (e.g., during minification),
    // the source map records the original name for the renamed location. This
    // enables accurate source maps even when identifiers are shortened.
    void Printer::PrintSymbol(logger::Loc loc, compiler::Ref ref, IdentMode mode, TrailingWhitespace whitespace) {
        ref = compiler::FollowSymbols(symbols_, ref);
        std::string original_name = symbols_.Get(ref)->original_name;
        std::string name;
        auto it = options_.local_names.find(ref);
        if (it != options_.local_names.end()) {
            name = it->second;
        } else {
            name = original_name;
        }
        if (options_.add_source_mappings) {
            if (original_name == name) {
                original_name = "";
            }
            builder_.AddSourceMapping(loc, original_name, css_);
        }
        PrintIdent(name, mode, whitespace);
    }

    // Prints indentation as a series of two-space units. When line_limit is
    // set, the indent is capped at half the line limit to prevent indentation
    // from consuming the entire line.
    void Printer::PrintIndent(int32_t indent) {
        int n = static_cast<int>(indent);
        if (options_.line_limit > 0 && n * 2 >= options_.line_limit) {
            n = options_.line_limit / 2;
        }
        for (int i = 0; i < n; i++) {
            css_.append("  ", 2);
        }
    }

    // Determines the multi-line comma grouping period for a CSS function.
    // Gradient functions with 2+ commas use period 1 (break after each stop).
    // matrix() uses period 2 (break after every 2 values = 3 per line).
    // matrix3d() uses period 4 (break after every 4 values = 3 per line).
    // Returns 0 if no special formatting applies.
    //
    // Example: FunctionMultiLineCommaPeriod(token "linear-gradient(..., a, b, c)") => 1
    // Example: FunctionMultiLineCommaPeriod(token "matrix(1, 0, 0, 1, 0, 0)") => 2
    // Example: FunctionMultiLineCommaPeriod(token "rgb(255, 0, 0)") => 0
    uint8_t Printer::FunctionMultiLineCommaPeriod(const Token& token) {
        if (token.kind == TokenType::kFunction && token.children != nullptr) {
            int comma_count = 0;
            for (const Token& t : *token.children) {
                if (t.kind == TokenType::kComma) {
                    comma_count++;
                }
            }

            std::string lower = ToLowerASCII(token.text);
            if (lower == "linear-gradient" || lower == "radial-gradient" || lower == "conic-gradient" ||
                lower == "repeating-linear-gradient" || lower == "repeating-radial-gradient" ||
                lower == "repeating-conic-gradient") {
                if (comma_count >= 2) {
                    return 1;
                }
            } else if (lower == "matrix") {
                if (comma_count == 5) {
                    return 2;
                }
            } else if (lower == "matrix3d") {
                if (comma_count == 15) {
                    return 4;
                }
            }
        }
        return 0;
    }

    // Prints a list of CSS tokens, handling whitespace insertion between tokens,
    // source map generation, and recursive descent into child token lists (for
    // functions, parentheses, braces, and brackets). Returns whether whitespace
    // was printed after the last token, which callers use to avoid doubling up
    // spaces.
    //
    // In pretty-printed mode with is_declaration=true, comma-separated lists
    // with 3+ items are automatically wrapped to improve readability. Gradient
    // and matrix functions get special line-breaking treatment via
    // FunctionMultiLineCommaPeriod.
    //
    // URL tokens are printed by looking up the import record for the actual path
    // and wrapping it in url() with appropriate quoting. Unique keys (internal
    // bundle references) are always quoted to ensure safe substitution later.
    bool Printer::PrintTokens(const std::vector<Token>& tokens, PrintTokensOpts opts) {
        bool has_whitespace_after = !tokens.empty() && Has(tokens[0].whitespace, WhitespaceFlags::kWhitespaceBefore);

        int comma_period = opts.multi_line_comma_period;
        if (!options_.minify_whitespace && opts.is_declaration) {
            int comma_count = 0;
            for (const Token& t : tokens) {
                if (t.kind == TokenType::kComma) {
                    comma_count++;
                    if (comma_count >= 2) {
                        comma_period = 1;
                        break;
                    }
                }
                if (t.kind == TokenType::kFunction && FunctionMultiLineCommaPeriod(t) > 0) {
                    comma_period = 1;
                    break;
                }
            }
        }

        int comma_count = 0;
        for (size_t i = 0; i < tokens.size(); i++) {
            const Token& t = tokens[i];
            if (t.kind == TokenType::kComma) {
                comma_count++;
            }
            if (t.kind == TokenType::kWhitespace) {
                has_whitespace_after = true;
                continue;
            }
            if (has_whitespace_after) {
                if (comma_period > 0 &&
                    (i == 0 || (tokens[i - 1].kind == TokenType::kComma && comma_count % comma_period == 0))) {
                    Print("\n");
                    PrintIndent(opts.indent + 1);
                } else if (options_.line_limit <= 0 || !PrintNewlinePastLineLimit(opts.indent + 1)) {
                    Print(" ");
                }
            }
            has_whitespace_after = Has(t.whitespace, WhitespaceFlags::kWhitespaceAfter) ||
                (i + 1 < tokens.size() && Has(tokens[i + 1].whitespace, WhitespaceFlags::kWhitespaceBefore));

            TrailingWhitespace whitespace = TrailingWhitespace::kMayNeedWhitespaceAfter;
            if (!has_whitespace_after) {
                whitespace = TrailingWhitespace::kCanDiscardWhitespaceAfter;
            }

            if (options_.add_source_mappings) {
                builder_.AddSourceMapping(t.loc, "", css_);
            }

            switch (t.kind) {
            case TokenType::kIdent:
                PrintIdent(t.text, IdentMode::kNormal, whitespace);
                break;

            case TokenType::kSymbol:
                PrintSymbol(t.loc, compiler::Ref{options_.input_source_index, t.payload_index}, IdentMode::kNormal, whitespace);
                break;

            case TokenType::kFunction:
                PrintIdent(t.text, IdentMode::kNormal, whitespace);
                Print("(");
                break;

            case TokenType::kDimension: {
                std::string value = t.DimensionValue();
                Print(value);
                IdentMode mode = IdentMode::kDimensionUnit;
                if (value.find_first_of("eE") != std::string::npos) {
                    mode = IdentMode::kDimensionUnitAfterExponent;
                }
                PrintIdent(t.DimensionUnit(), mode, whitespace);
                break;
            }

            case TokenType::kAtKeyword:
                Print("@");
                PrintIdent(t.text, IdentMode::kNormal, whitespace);
                break;

            case TokenType::kHash:
                Print("#");
                PrintIdent(t.text, IdentMode::kHash, whitespace);
                break;

            case TokenType::kString:
                PrintQuoted(t.text, PrintQuotedFlags::kNone);
                break;

            case TokenType::kUrl: {
                const  compiler::ImportRecord& record = import_records_[t.payload_index];
                std::string text = record.path.text;
                bool try_to_avoid_quote = true;
                PrintQuotedFlags flags = PrintQuotedFlags::kNone;
                if (compiler::Has(record.flags,  compiler::ImportRecordFlags::kContainsUniqueKey)) {
                    flags = flags | PrintQuotedFlags::kNoWrap;
                    try_to_avoid_quote = false;
                } else if (options_.line_limit > 0 && CurrentLineLength() + static_cast<int>(text.size()) >= options_.line_limit) {
                    try_to_avoid_quote = false;
                }
                Print("url(");
                PrintQuotedWithQuote(text, BestQuoteCharForString(text, try_to_avoid_quote), flags);
                Print(")");
                RecordImportPathForMetafile(t.payload_index);
                break;
            }

            case TokenType::kUnterminatedString:
                Print(t.text);
                Print("\n");
                if (!options_.minify_whitespace) {
                    PrintIndent(opts.indent);
                }
                has_whitespace_after = false;
                break;

            default:
                Print(t.text);
                break;
            }

            if (t.children != nullptr) {
                uint8_t child_comma_period = 0;

                if (comma_period > 0 && opts.is_declaration) {
                    child_comma_period = FunctionMultiLineCommaPeriod(t);
                }

                if (child_comma_period > 0) {
                    opts.indent++;
                    if (!options_.minify_whitespace) {
                        Print("\n");
                        PrintIndent(opts.indent + 1);
                    }
                }

                PrintTokensOpts child_opts;
                child_opts.indent = opts.indent;
                child_opts.multi_line_comma_period = child_comma_period;
                PrintTokens(*t.children, child_opts);

                if (child_comma_period > 0) {
                    opts.indent--;
                }

                switch (t.kind) {
                case TokenType::kFunction:
                    Print(")");
                    break;

                case TokenType::kOpenParen:
                    Print(")");
                    break;

                case TokenType::kOpenBrace:
                    Print("}");
                    break;

                case TokenType::kOpenBracket:
                    Print("]");
                    break;

                default:
                    break;
                }
            }
        }
        if (has_whitespace_after) {
            Print(" ");
        }
        return has_whitespace_after;
    }

}
