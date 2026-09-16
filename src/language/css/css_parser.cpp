#include <tuple>
#include <utility>

#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_parser.hpp"
#include "guchho/css/css_helpers.hpp"
#include "guchho/css/css_lexer.hpp"
#include "guchho/css/css_properties.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/compat.hpp"
#include "guchho/helpers.hpp"
#include "guchho/compiler.hpp"


namespace guchho::css {

    namespace {

        // Strips the specified bit flags from a WhitespaceFlags bitmask.
        // Used to clear leading/trailing whitespace markers from tokens.
        // Example: ClearWhitespace(Whitespac eFlags::kWhitespaceBefore | WhitespaceFlags::kWhitespaceAfter,
        //                           WhitespaceFlags::kWhitespaceBefore) => kWhitespaceAfter
        inline WhitespaceFlags ClearWhitespace(WhitespaceFlags flags, uint8_t bits) {
            return static_cast<WhitespaceFlags>(static_cast<uint8_t>(flags) & ~bits);
        }

        // Returns a set of HTML element names that are both non-deprecated and
        // supported by Internet Explorer 7. This is used by selector merging
        // logic to avoid combining selectors that rely on elements unsupported
        // by legacy browsers, which would produce incorrect results.
        const std::unordered_set<std::string_view>& NonDeprecatedElementsSupportedByIE7() {
            static const std::unordered_set<std::string_view> elements = {
                "a", "abbr", "address", "area", "b", "base", "blockquote", "body", "br",
                "button", "caption", "cite", "code", "col", "colgroup", "dd", "del", "dfn",
                "div", "dl", "dt", "em", "embed", "fieldset", "form", "h1", "h2", "h3", "h4",
                "h5", "h6", "head", "hr", "html", "i", "iframe", "img", "input", "ins", "kbd",
                "label", "legend", "li", "link", "map", "menu", "meta", "noscript", "object",
                "ol", "optgroup", "option", "p", "param", "pre", "q", "ruby", "s", "samp",
                "script", "select", "small", "span", "strong", "style", "sub", "sup", "table",
                "tbody", "td", "textarea", "tfoot", "th", "thead", "title", "tr", "u", "ul",
                "var",
            };
            return elements;
        }

    }

    // Checks whether a given element name is a non-deprecated HTML element
    // that was supported by Internet Explorer 7.
    // Example: IsNonDeprecatedElementSupportedByIE7("div") => true
    // Example: IsNonDeprecatedElementSupportedByIE7("dialog") => false (not in IE7)
    bool IsNonDeprecatedElementSupportedByIE7(std::string_view name) {
        return NonDeprecatedElementsSupportedByIE7().count(name) != 0;
    }


    // Compares two ParserOptions structs for deep equality. This checks all
    // fields including the css_prefix_data map. Used to determine whether
    // two parse operations with the same options can share cached results.
    bool operator==(const ParserOptions& a, const ParserOptions& b) {
        if (a.original_target_env != b.original_target_env) return false;
        if (a.unsupported_css_features != b.unsupported_css_features) return false;
        if (a.minify_syntax != b.minify_syntax) return false;
        if (a.minify_whitespace != b.minify_whitespace) return false;
        if (a.minify_identifiers != b.minify_identifiers) return false;
        if (a.symbol_mode != b.symbol_mode) return false;
        if (a.css_prefix_data.size() != b.css_prefix_data.size()) return false;
        for (const auto& [key, value] : a.css_prefix_data) {
            auto it = b.css_prefix_data.find(key);
            if (it == b.css_prefix_data.end() || it->second != value) return false;
        }
        return true;
    }

    // Converts global configuration options into CSS-parser-specific options.
    // The Loader parameter determines symbol_mode: global CSS loaders produce
    // kGlobal symbols, local CSS loaders produce kLocal symbols, and all others
    // disable symbol tracking.
    // Example: OptionsFromConfig(Loader::kLocalCSS, opts) => symbol_mode = kLocal
    ParserOptions OptionsFromConfig(guchho::config::Loader loader, const guchho::config::Options& options) {
        SymbolMode symbol_mode = SymbolMode::kDisabled;
        if (loader == guchho::config::Loader::kGlobalCSS) {
            symbol_mode = SymbolMode::kGlobal;
        } else if (loader == guchho::config::Loader::kLocalCSS) {
            symbol_mode = SymbolMode::kLocal;
        }

        ParserOptions result;
        result.css_prefix_data = options.CSSPrefixData;
        result.minify_syntax = options.MinifySyntax;
        result.minify_whitespace = options.MinifyWhitespace;
        result.minify_identifiers = options.MinifyIdentifiers;
        result.unsupported_css_features = options.UnsupportedCSSFeatures;
        result.original_target_env = options.OriginalTargetEnv;
        result.symbol_mode = symbol_mode;
        return result;
    }

    // Classifies a lowercase at-rule token into a semantic category that
    // determines how the parser should handle its body. Returns kUnknown for
    // at-rules that the parser does not have special handling for.
    // Example: LookupSpecialAtRule("media") => kInheritContext (parse as nested rules)
    // Example: LookupSpecialAtRule("font-face") => kDeclarations (parse as declarations)
    // Example: LookupSpecialAtRule("layer") => kQualifiedOrEmpty (can be block or semicolon)
    AtRuleKind LookupSpecialAtRule(std::string_view lower_at_token) {
        static const std::unordered_map<std::string_view, AtRuleKind> table = {
            {"media", AtRuleKind::kInheritContext},
            {"supports", AtRuleKind::kInheritContext},

            {"font-face", AtRuleKind::kDeclarations},
            {"page", AtRuleKind::kDeclarations},

            {"bottom-center", AtRuleKind::kDeclarations},
            {"bottom-left-corner", AtRuleKind::kDeclarations},
            {"bottom-left", AtRuleKind::kDeclarations},
            {"bottom-right-corner", AtRuleKind::kDeclarations},
            {"bottom-right", AtRuleKind::kDeclarations},
            {"left-bottom", AtRuleKind::kDeclarations},
            {"left-middle", AtRuleKind::kDeclarations},
            {"left-top", AtRuleKind::kDeclarations},
            {"right-bottom", AtRuleKind::kDeclarations},
            {"right-middle", AtRuleKind::kDeclarations},
            {"right-top", AtRuleKind::kDeclarations},
            {"top-center", AtRuleKind::kDeclarations},
            {"top-left-corner", AtRuleKind::kDeclarations},
            {"top-left", AtRuleKind::kDeclarations},
            {"top-right-corner", AtRuleKind::kDeclarations},
            {"top-right", AtRuleKind::kDeclarations},

            {"viewport", AtRuleKind::kDeclarations},
            {"-ms-viewport", AtRuleKind::kDeclarations},

            {"document", AtRuleKind::kInheritContext},
            {"-moz-document", AtRuleKind::kInheritContext},

            {"layer", AtRuleKind::kQualifiedOrEmpty},
            {"scope", AtRuleKind::kInheritContext},

            {"font-palette-values", AtRuleKind::kDeclarations},
            {"counter-style", AtRuleKind::kDeclarations},

            {"font-feature-values", AtRuleKind::kDeclarations},
            {"annotation", AtRuleKind::kDeclarations},
            {"character-variant", AtRuleKind::kDeclarations},
            {"historical-forms", AtRuleKind::kDeclarations},
            {"ornaments", AtRuleKind::kDeclarations},
            {"styleset", AtRuleKind::kDeclarations},
            {"stylistic", AtRuleKind::kDeclarations},
            {"swash", AtRuleKind::kDeclarations},

            {"container", AtRuleKind::kInheritContext},
            {"starting-style", AtRuleKind::kInheritContext},

            {"position-try", AtRuleKind::kDeclarations},
            {"view-transition", AtRuleKind::kDeclarations},
        };
        auto it = table.find(lower_at_token);
        if (it != table.end()) return it->second;
        return AtRuleKind::kUnknown;
    }

    // Determines whether a known at-rule can be safely removed when its body
    // is empty. This is used during minification to strip out no-op rules like
    // an empty @media block that contains no rules.
    // Example: AtKnownRuleCanBeRemovedIfEmpty("media") => true
    // Example: AtKnownRuleCanBeRemovedIfEmpty("keyframes") => false
    bool AtKnownRuleCanBeRemovedIfEmpty(std::string_view lower_at_token) {
        static const std::unordered_set<std::string_view> table = {
            "media", "supports", "font-face", "page",
            "bottom-center", "bottom-left-corner", "bottom-left", "bottom-right-corner",
            "bottom-right", "left-bottom", "left-middle", "left-top", "right-bottom",
            "right-middle", "right-top", "top-center", "top-left-corner", "top-left",
            "top-right-corner", "top-right", "scope", "font-palette-values", "container",
        };
        return table.count(lower_at_token) != 0;
    }



    // Main entry point for parsing a CSS source file. Tokenizes the input,
    // parses the token stream into a list of rules, and assembles the final AST.
    // The resulting AST includes the rule list, character frequency data (for
    // identifier minification), symbols, import records, and source metadata.
    // Example: Parse(log, source, opts) => AST with rules, symbols, imports, etc.
    AST Parse(guchho::logger::Log& log, const guchho::logger::Source& source, const ParserOptions& options) {
        Parser parser(log, source, options);
        std::vector<Rule> rules = parser.ParseListOfRules(RuleContext{true, true});
        parser.Expect(TokenType::kEndOfFile);

        AST ast;
        ast.rules = std::move(rules);
        ast.char_freq = parser.ComputeCharacterFrequency();
        ast.symbols = std::move(parser.symbols_);
        ast.import_records = std::move(parser.import_records_);
        ast.approximate_line_count = parser.approximate_line_count_;
        ast.source_map_comment = std::move(parser.source_map_comment_);
        ast.local_symbols = std::move(parser.local_symbols_);
        ast.local_scope = std::move(parser.local_scope_);
        ast.global_scope = std::move(parser.global_scope_);
        ast.composes = std::move(parser.composes_);
        ast.layers_pre_import = std::move(parser.layers_pre_import_);
        ast.layers_post_import = std::move(parser.layers_post_import_);
        return ast;
    }

    // Constructs the parser by immediately tokenizing the source input. The
    // lexer runs eagerly so the parser can work with a flat token array. If
    // minify_identifiers is enabled, all comments are recorded so their
    // character frequencies can be subtracted during identifier mangling.
    // Legal comments (those starting with //! or /*! or //@ or /*!@) are
    // preserved separately for potential embedding in the output.
    Parser::Parser(guchho::logger::Log& log, const guchho::logger::Source& source, const ParserOptions& options)
        : log_(&log), source_(&source), tracker_(&source), options_(options) {
        lexer::Options lexer_options;
        lexer_options.record_all_comments = options.minify_identifiers;
        lexer::TokenizeResult result = lexer::Tokenize(log, source, lexer_options);
        tokens_ = std::move(result.tokens);
        all_comments_ = std::move(result.all_comments);
        legal_comments_ = std::move(result.legal_comments);
        source_map_comment_ = std::move(result.source_map_comment);
        approximate_line_count_ = result.approximate_line_count;
        prev_error_ = guchho::logger::Loc{-1};
        make_local_symbols_ = options.symbol_mode == SymbolMode::kLocal;
    }

    void Parser::Advance() {
        if (index_ < tokens_.size()) {
            ++index_;
        }
    }

    // Returns the token at the given absolute index. If the index is beyond
    // the end of the token array, returns a synthetic EOF token whose start
    // position is at the end of the source. This avoids bounds checks at
    // every callsite.
    // Example: At(5) returns tokens_[5] if it exists, otherwise an EOF token
    lexer::Token Parser::At(size_t index) const {
        if (index < tokens_.size()) {
            return tokens_[index];
        }
        lexer::Token token;
        token.kind = TokenType::kEndOfFile;
        token.range.loc.start = static_cast<int32_t>(source_->contents.size());
        return token;
    }

    lexer::Token Parser::Current() const {
        return At(index_);
    }

    lexer::Token Parser::Next() const {
        return At(index_ + 1);
    }

    // Returns the raw source text for the current token without any decoding.
    // Used in error messages to show exactly what the user wrote.
    // Example: if source is "color: red" and current token is "red", returns "red"
    std::string Parser::Raw() const {
        lexer::Token t = Current();
        return source_->contents.substr(static_cast<size_t>(t.range.loc.start),
                                        static_cast<size_t>(t.range.len));
    }

    // Returns the decoded text of the current token. String and URL tokens
    // have escape sequences resolved; other tokens are returned as-is.
    // Example: Decoded() on token "hello\nworld" (with escapes) => "hello\nworld" (newline)
    std::string Parser::Decoded() const {
        return Current().DecodedText(source_->contents);
    }

    bool Parser::Peek(TokenType kind) const {
        return kind == Current().kind;
    }

    // Consumes the current token if it matches the expected kind. Returns
    // true and advances if matched, false otherwise (leaves position unchanged).
    // Example: Eat(kColon) advances past ":" and returns true; Eat(kColon) on "red" returns false
    bool Parser::Eat(TokenType kind) {
        if (Peek(kind)) {
            Advance();
            return true;
        }
        return false;
    }

    bool Parser::Expect(TokenType kind) {
        return ExpectWithMatchingLoc(kind, guchho::logger::Loc{-1});
    }

    // Expects the current token to be of the given kind, emitting a
    // diagnostic if it is not. The matching_loc parameter enables contextual
    // error messages for unmatched opening brackets/braces/parens by pointing
    // back to the opening token. Returns true if the token matched and was
    // consumed, false otherwise. Only one diagnostic is emitted per source
    // location to avoid cascading error noise.
    bool Parser::ExpectWithMatchingLoc(TokenType kind, guchho::logger::Loc matching_loc) {
        if (Eat(kind)) {
            return true;
        }
        lexer::Token t = Current();
        if ((t.flags & kDidWarnAboutSingleLineComment) != 0) {
            return false;
        }

        std::string text;
        std::string suggestion;
        std::vector<guchho::logger::MsgData> notes;

        std::string expected = lexer::ToString(kind);
        if (expected.size() >= 2 && expected.front() == '"' && expected.back() == '"') {
            suggestion = expected.substr(1, expected.size() - 2);
        }

        if ((kind == TokenType::kSemicolon || kind == TokenType::kColon) && index_ > 0 && At(index_ - 1).kind == TokenType::kWhitespace) {
            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_Expected, expected);
            t = At(index_ - 1);
        } else if ((kind == TokenType::kCloseBrace || kind == TokenType::kCloseBracket || kind == TokenType::kCloseParen) &&
                matching_loc.start != -1 && matching_loc.start + 1 <= static_cast<int32_t>(source_->contents.size())) {
            std::string c = source_->contents.substr(static_cast<size_t>(matching_loc.start), 1);
            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedToGoWith, expected, guchho::helpers::quoteString(c));
            notes.push_back(tracker_.MakeMsgData(guchho::logger::Range{matching_loc, 1},
                                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnbalancedNote, guchho::helpers::quoteString(c))));
        } else {
            switch (t.kind) {
            case TokenType::kEndOfFile:
            case TokenType::kWhitespace:
                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedButFound, expected, lexer::ToString(t.kind));
                t.range.len = 0;
                break;
            case TokenType::kBadUrl:
            case TokenType::kUnterminatedString:
                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedButFound, expected, lexer::ToString(t.kind));
                break;
            default:
                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedButFound, expected, guchho::helpers::quoteString(Raw()));
                break;
            }
        }

        if (t.range.loc.start > prev_error_.start) {
            guchho::logger::MsgData data = tracker_.MakeMsgData(t.range, text);
            if (data.location) {
                data.location->suggestion = suggestion;
            }
            guchho::logger::Msg msg;
            msg.id = guchho::logger::MsgID::kCSS_CSSSyntaxError;
            msg.kind = guchho::logger::MsgKind::kWarning;
            msg.data = std::move(data);
            msg.notes = std::move(notes);
            log_->AddMsgID(msg.id, msg);
            prev_error_ = t.range.loc;
        }
        return false;
    }

    // Emits an "unexpected token" diagnostic for the current token position.
    // Like ExpectWithMatchingLoc, this deduplicates by source location to
    // avoid flooding the user with cascading errors from a single mistake.
    void Parser::Unexpected() {
        lexer::Token t = Current();
        if (t.range.loc.start > prev_error_.start && (t.flags & kDidWarnAboutSingleLineComment) == 0) {
            std::string text;
        switch (t.kind) {
        case TokenType::kEndOfFile:
        case TokenType::kWhitespace:
            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_Unexpected, lexer::ToString(t.kind));
            t.range.len = 0;
            break;
        case TokenType::kBadUrl:
        case TokenType::kUnterminatedString:
            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_Unexpected, lexer::ToString(t.kind));
            break;
        default:
            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_Unexpected, guchho::helpers::quoteString(Raw()));
            break;
        }
        log_->AddID(guchho::logger::MsgID::kCSS_CSSSyntaxError, guchho::logger::MsgKind::kWarning, &tracker_,
                    t.range, text);
            prev_error_ = t.range.loc;
        }
    }

    // Looks up or creates a compiler symbol for the given name at the specified
    // location. If the name already exists in the current scope, its existing
    // entry is returned. Otherwise a new symbol is created and added to the
    // symbols list and the scope map. The use_count_estimate is incremented
    // each time a symbol is referenced, which helps the identifier minifier
    // prioritize shorter names for frequently-used identifiers.
    // Example: SymbolForName(loc, ".myClass") creates or returns a symbol for ".myClass"
    guchho::compiler::LocRef Parser::SymbolForName(guchho::logger::Loc loc, const std::string& name) {
        guchho::compiler::SymbolKind kind;
        std::unordered_map<std::string, guchho::compiler::LocRef>* scope;

        if (make_local_symbols_) {
            kind = guchho::compiler::SymbolKind::kLocalCSS;
            scope = &local_scope_;
        } else {
            kind = guchho::compiler::SymbolKind::kGlobalCSS;
            scope = &global_scope_;
        }

        guchho::compiler::LocRef entry;
        auto it = scope->find(name);
        if (it == scope->end()) {
            entry.loc = loc;
            entry.ref = guchho::compiler::Ref{source_->index, static_cast<uint32_t>(symbols_.size())};
            guchho::compiler::Symbol symbol;
            symbol.kind = kind;
            symbol.original_name = name;
            symbol.link = guchho::compiler::kInvalidRef;
            symbols_.push_back(std::move(symbol));
            (*scope)[name] = entry;
            if (kind == guchho::compiler::SymbolKind::kLocalCSS) {
                local_symbols_.push_back(entry);
            }
        } else {
            entry = it->second;
        }

        symbols_[entry.ref.inner_index].use_count_estimate++;
        return entry;
    }

    // Records the layer names declared by an @layer rule. Each layer name is
    // prefixed with any enclosing layer path to produce fully-qualified layer
    // names. This is skipped if anonymous layers are currently being parsed
    // (anon_layer_count_ > 0) since anonymous layers cannot be referenced.
    // Example: inside @layer a { @layer b; }, this records ["a", "b"]
    void Parser::RecordAtLayerRule(std::vector<std::vector<std::string>> layers) {
        if (anon_layer_count_ > 0) {
            return;
        }

        for (std::vector<std::string>& layer : layers) {
            if (!enclosing_layer_.empty()) {
                std::vector<std::string> clone;
                clone.reserve(enclosing_layer_.size() + layer.size());
                clone.insert(clone.end(), enclosing_layer_.begin(), enclosing_layer_.end());
                clone.insert(clone.end(), layer.begin(), layer.end());
                layer = std::move(clone);
            }
            layers_post_import_.push_back(std::move(layer));
        }
    }

    // Computes character frequency data across the entire source, comments,
    // import paths, and local symbols. This data is used by the identifier
    // minifier to assign the shortest possible names to the most frequently
    // occurring characters. Returns nullptr if identifier minification is
    // disabled. Comments are scanned with negative weight so their characters
    // do not influence the minification of actual code identifiers.
    std::shared_ptr<guchho::compiler::CharFreq> Parser::ComputeCharacterFrequency() {
        if (!options_.minify_identifiers) {
            return nullptr;
        }

        std::shared_ptr<guchho::compiler::CharFreq> char_freq = std::make_shared<guchho::compiler::CharFreq>();
        char_freq->Scan(source_->contents, 1);

        for (const guchho::logger::Range& comment_range : all_comments_) {
            char_freq->Scan(source_->TextForRange(comment_range), -1);
        }

        for (const guchho::compiler::ImportRecord& record : import_records_) {
            if (!record.source_index.IsValid()) {
                char_freq->Scan(record.path.text, -1);
            }
        }

        for (const guchho::compiler::Symbol& symbol : symbols_) {
            if (symbol.kind == guchho::compiler::SymbolKind::kLocalCSS) {
                char_freq->Scan(symbol.original_name, -static_cast<int32_t>(symbol.use_count_estimate));
            }
        }

        return char_freq;
    }

    // Parses a list of CSS rules from the token stream. This is the main
    // recursive-descent loop that drives the parser. It handles top-level
    // rules (selectors, at-rules) and nested rules within declaration lists.
    //
    // The context parameter controls behavior: is_top_level determines whether
    // @charset/@import ordering rules apply and whether HTML comments (<!--/-->)
    // should be skipped. parse_selectors controls whether bare tokens at the
    // start of a rule are interpreted as selectors or as qualified rules.
    //
    // Legal comments encountered between rules are emitted as comment rules.
    // CSS nesting is lowered at the top level when the target does not support
    // it. At the end, if minify_syntax is enabled, rules are mangled (empty
    // rules removed, adjacent selectors merged, etc.).
    //
    // Example: ParseListOfRules({true, true}) on "a { color: red } b { }"
    //   => [RSelector(selectors=["a"], rules=[RDeclaration(key="color")]),
    //       (empty "b" rule removed if minifying)]
    std::vector<Rule> Parser::ParseListOfRules(const RuleContext& context) {
        AtRuleContext at_rule_context;
        if (context.is_top_level) {
            at_rule_context.charset_validity = AtRuleValidity::kValid;
            at_rule_context.import_validity = AtRuleValidity::kValid;
            at_rule_context.is_top_level = true;
        }
        std::vector<Rule> rules;
        bool did_find_at_import = false;

        for (;;) {
            if (context.is_top_level) {
                nesting_is_present_ = false;
            }

            while (legal_comment_index_ < legal_comments_.size()) {
                const lexer::Comment& comment = legal_comments_[legal_comment_index_];
                if (comment.token_index_after > static_cast<uint32_t>(index_)) {
                    break;
                }
                if (comment.token_index_after == static_cast<uint32_t>(index_)) {
                    Rule rule;
                    rule.loc = comment.loc;
                    auto data = std::make_shared<RComment>();
                    data->text = comment.text;
                    rule.data = std::move(data);
                    rules.push_back(std::move(rule));
                }
                ++legal_comment_index_;
            }

            switch (Current().kind) {
            case TokenType::kEndOfFile:
                goto done_loop;

            case TokenType::kCloseBrace:
                if (!context.is_top_level) {
                    goto done_loop;
                }
                break;

            case TokenType::kWhitespace:
                Advance();
                continue;

            case TokenType::kAtKeyword: {
                Rule rule = ParseAtRule(at_rule_context);

                if (context.is_top_level) {
                    if (dynamic_cast<RAtCharset*>(rule.data.get()) != nullptr) {
                    } else if (dynamic_cast<RAtImport*>(rule.data.get()) != nullptr) {
                        did_find_at_import = true;
                        if (at_rule_context.charset_validity == AtRuleValidity::kValid) {
                            at_rule_context.after_loc = rule.loc;
                            at_rule_context.charset_validity = AtRuleValidity::kInvalidAfter;
                        }
                    } else if (auto* r = dynamic_cast<RAtLayer*>(rule.data.get())) {
                        if (at_rule_context.charset_validity == AtRuleValidity::kValid) {
                            at_rule_context.after_loc = rule.loc;
                            at_rule_context.charset_validity = AtRuleValidity::kInvalidAfter;
                        }

                        if (at_rule_context.import_validity == AtRuleValidity::kValid &&
                            (!r->rules.empty() || did_find_at_import)) {
                            at_rule_context.after_loc = rule.loc;
                            at_rule_context.charset_validity = AtRuleValidity::kInvalidAfter;
                            at_rule_context.import_validity = AtRuleValidity::kInvalidAfter;
                        }
                    } else {
                        if (at_rule_context.import_validity == AtRuleValidity::kValid) {
                            at_rule_context.after_loc = rule.loc;
                            at_rule_context.charset_validity = AtRuleValidity::kInvalidAfter;
                            at_rule_context.import_validity = AtRuleValidity::kInvalidAfter;
                        }
                    }
                }

                if (nesting_is_present_ &&
                    guchho::compat::Has(options_.unsupported_css_features, guchho::compat::CSSFeature::kNesting) &&
                    context.is_top_level) {
                    rules = LowerNestingInRule(std::move(rule), std::move(rules));
                } else {
                    rules.push_back(std::move(rule));
                }
                continue;
            }

            case TokenType::kCdo:
            case TokenType::kCdc:
                if (context.is_top_level) {
                    Advance();
                    continue;
                }
                break;

            default:
                break;
            }

            if (at_rule_context.import_validity == AtRuleValidity::kValid) {
                at_rule_context.after_loc = Current().range.loc;
                at_rule_context.charset_validity = AtRuleValidity::kInvalidAfter;
                at_rule_context.import_validity = AtRuleValidity::kInvalidAfter;
            }

            if (!context.is_top_level) {
                auto [scan, end_index] = ScanForEndOfRule();
                if (scan == EndOfRuleScan::kSemicolon) {
                    Rule rule;
                    rule.loc = Current().range.loc;
                    auto data = std::make_shared<RBadDeclaration>();
                    data->tokens = ConvertTokens(std::vector<lexer::Token>(
                        tokens_.begin() + static_cast<ptrdiff_t>(index_),
                        tokens_.begin() + static_cast<ptrdiff_t>(end_index)));
                    rule.data = std::move(data);
                    rules.push_back(std::move(rule));
                    index_ = static_cast<size_t>(end_index) + 1;
                    continue;
                }
            }

            Rule rule;
            if (context.parse_selectors) {
                rule = ParseSelectorRule(context.is_top_level, ParseSelectorOpts{});
            } else {
                ParseQualifiedRuleOpts qr_opts;
                qr_opts.is_top_level = context.is_top_level;
                rule = ParseQualifiedRule(qr_opts);
            }

            if (nesting_is_present_ &&
                guchho::compat::Has(options_.unsupported_css_features, guchho::compat::CSSFeature::kNesting) &&
                context.is_top_level) {
                rules = LowerNestingInRule(std::move(rule), std::move(rules));
            } else {
                rules.push_back(std::move(rule));
            }
        }

    done_loop:
        if (options_.minify_syntax) {
            rules = MangleRules(std::move(rules), context.is_top_level);
        }
        return rules;
    }

    // Parses a list of CSS declarations (property: value pairs), at-rules,
    // and nested selectors within a rule block. This is the inner parsing
    // loop used for the body of selector rules, @media blocks, and other
    // rule types that contain declarations.
    //
    // Declarations are first parsed as raw rules, then post-processed by
    // ProcessDeclarations which handles vendor prefixes, shorthand merging,
    // and other transformations. Nested selectors are detected by scanning
    // ahead for an opening brace after a selector-like token sequence.
    //
    // If minify_syntax is enabled, unnecessary nesting like "& { x: y }"
    // is inlined to just "x: y".
    //
    // Example: ParseListOfDeclarations() on "color: red; font-size: 12px"
    //   => [RDeclaration(key="color", value="red"),
    //       RDeclaration(key="font-size", value="12px")]
    std::vector<Rule> Parser::ParseListOfDeclarations(const ListOfDeclarationsOpts& opts) {
        std::vector<Rule> list;
        bool found_nesting = false;

        for (;;) {
            switch (Current().kind) {
            case TokenType::kWhitespace:
            case TokenType::kSemicolon:
                Advance();
                continue;

            case TokenType::kEndOfFile:
            case TokenType::kCloseBrace: {
                DeclarationsOptions dopt;
                dopt.unsupported_css_features = options_.unsupported_css_features;
                dopt.css_prefix_data = &options_.css_prefix_data;
                dopt.minify_syntax = options_.minify_syntax;
                dopt.minify_whitespace = options_.minify_whitespace;
                dopt.symbol_mode_enabled = options_.symbol_mode != SymbolMode::kDisabled;

                DeclarationsSymbolContext sctx;
                sctx.symbols = &symbols_;
                sctx.local_symbols = &local_symbols_;
                sctx.local_scope = &local_scope_;
                sctx.global_scope = &global_scope_;
                sctx.composes = &composes_;
                sctx.import_records = &import_records_;
                sctx.source_index = source_->index;
                sctx.make_local_symbols = make_local_symbols_;

                DeclarationsLogContext lctx;
                lctx.log = log_;
                lctx.tracker = &tracker_;
                lctx.prev_error = &prev_error_;
                lctx.source = source_;

                list = ProcessDeclarations(std::move(list), dopt, opts.composes_context, sctx, lctx);

                if (options_.minify_syntax) {
                    list = MangleRules(std::move(list), false /* is_top_level */);

                    if (opts.can_inline_no_op_nesting && found_nesting) {
                        std::vector<Rule> inline_decls;
                        size_t n = 0;
                        for (const Rule& rule : list) {
                            if (auto* r = dynamic_cast<RSelector*>(rule.data.get());
                                r && r->selectors.size() == 1) {
                                const ComplexSelector& sel = r->selectors[0];
                                if (sel.selectors.size() == 1 && sel.selectors[0].IsSingleAmpersand()) {
                                    inline_decls.insert(inline_decls.end(), r->rules.begin(), r->rules.end());
                                    continue;
                                }
                            }
                            list[n] = rule;
                            ++n;
                        }
                        list.resize(n);
                        list.insert(list.end(), inline_decls.begin(), inline_decls.end());
                    }
                }
                return list;
            }

            case TokenType::kAtKeyword:
                if (in_selector_subtree_ > 0) {
                    nesting_is_present_ = true;
                }
                {
                    AtRuleContext arc;
                    arc.is_declaration_list = true;
                    arc.can_inline_no_op_nesting = opts.can_inline_no_op_nesting;
                    list.push_back(ParseAtRule(arc));
                }
                continue;

            default:
                if (ScanForEndOfRule().first == EndOfRuleScan::kOpenBrace) {
                    nesting_is_present_ = true;
                    found_nesting = true;
                    ParseSelectorOpts sopts;
                    sopts.is_declaration_context = true;
                    sopts.composes_context = opts.composes_context;
                    Rule rule = ParseSelectorRule(false, sopts);

                    if (auto* sel = dynamic_cast<RSelector*>(rule.data.get());
                        sel && sel->selectors.size() == 1) {
                        const ComplexSelector& first = sel->selectors[0];
                        if (first.selectors.size() == 1 && first.selectors[0].was_empty_from_local_or_global &&
                            first.selectors[0].IsSingleAmpersand()) {
                            list.insert(list.end(), sel->rules.begin(), sel->rules.end());
                            continue;
                        }
                    }

                    list.push_back(std::move(rule));
                } else {
                    list.push_back(ParseDeclaration());
                }
                continue;
            }
        }
    }

    // Minifies a list of CSS rules by removing empty rules, merging adjacent
    // selectors with identical bodies, collapsing redundant @media nesting,
    // and simplifying @layer syntax. This runs as a post-processing pass
    // after parsing when minify_syntax is enabled.
    //
    // Key transformations:
    //   - Empty @media, @supports, @font-face, etc. blocks are removed
    //   - "@layer foo {}" becomes "@layer foo;"
    //   - "@layer a { @layer b {} }" becomes "@layer a.b;"
    //   - Adjacent selectors like "a { x: y } a { x: y }" become "a, a { x: y }"
    //   - @media rules duplicating a parent's conditions are unwrapped
    //
    // For non-top-level rules, a separate back-to-front pass removes
    // duplicate rules across the codebase using hash-based deduplication.
    //
    // Example: MangleRules(["a { }", "b { color: red }", "b { color: red }"])
    //   => ["b { color: red }"] (empty rule removed, duplicates merged)
    std::vector<Rule> Parser::MangleRules(std::vector<Rule> rules, bool is_top_level) {
        std::vector<Rule> mangled_rules;
        std::shared_ptr<RuleData> prev_non_comment;

        for (const Rule& rule : rules) {
            std::shared_ptr<RuleData> next_non_comment = rule.data;

            if (dynamic_cast<RAtKeyframes*>(rule.data.get()) != nullptr) {
            } else if (auto* r = dynamic_cast<RAtLayer*>(rule.data.get())) {
                if (r->rules.empty() && !r->names.empty()) {
                    r->rules.clear();
                    r->has_block = false;
                } else if (r->rules.size() == 1 && r->names.size() == 1) {
                    if (auto* r2 = dynamic_cast<RAtLayer*>(r->rules[0].data.get());
                        r2 && r2->names.size() == 1) {
                        r->names[0].insert(r->names[0].end(), r2->names[0].begin(), r2->names[0].end());
                        r->rules = std::move(r2->rules);
                    }
                }
            } else if (auto* r2 = dynamic_cast<RKnownAt*>(rule.data.get())) {
                if (r2->rules.empty() && AtKnownRuleCanBeRemovedIfEmpty(r2->at_token)) {
                    continue;
                }
            } else if (auto* r3 = dynamic_cast<RAtMedia*>(rule.data.get())) {
                if (r3->rules.empty()) {
                    continue;
                }

                bool matched = false;
                for (const std::vector<MediaQuery>& queries : enclosing_at_media_) {
                    if (MediaQueriesEqual(r3->queries, queries, nullptr)) {
                        mangled_rules.insert(mangled_rules.end(), r3->rules.begin(), r3->rules.end());
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    continue;
                }
            } else if (auto* r4 = dynamic_cast<RSelector*>(rule.data.get())) {
                if (r4->rules.empty()) {
                    continue;
                }

                if (prev_non_comment != nullptr) {
                    if (auto* prev = dynamic_cast<RSelector*>(prev_non_comment.get());
                        prev && RulesEqual(r4->rules, prev->rules, nullptr) &&
                        IsSafeSelectors(r4->selectors) && IsSafeSelectors(prev->selectors)) {
                        for (const ComplexSelector& sel : r4->selectors) {
                            bool duplicate = false;
                            for (const ComplexSelector& prev_sel : prev->selectors) {
                                if (sel.Equal(prev_sel, nullptr)) {
                                    duplicate = true;
                                    break;
                                }
                            }
                            if (!duplicate) {
                                prev->selectors.push_back(sel);
                            }
                        }
                        continue;
                    }
                }
            } else if (dynamic_cast<RComment*>(rule.data.get()) != nullptr) {
                next_non_comment = nullptr;
            }

            if (next_non_comment != nullptr) {
                prev_non_comment = next_non_comment;
            }
            mangled_rules.push_back(rule);
        }

        if (!is_top_level) {
            DeadRuleRemover remover(guchho::compiler::SymbolMap{});
            mangled_rules = remover.RemoveDeadRulesInPlace(source_->index, std::move(mangled_rules), import_records_);
        }

        return mangled_rules;
    }

    DeadRuleRemover::DeadRuleRemover(const guchho::compiler::SymbolMap& symbols) {
        symbols_ = symbols;
        check_.symbols = &symbols_;
    }

    // Removes duplicate and dead rules from a rule list using a back-to-front
    // scan with hash-based deduplication. Rules are hashed and compared for
    // structural equality. If two rules are identical, only the last one is
    // kept. Cross-file deduplication is supported by passing import records
    // from different source files into the equality check, which resolves
    // url() tokens to determine if they reference the same resource.
    //
    // Rules with selectors that match nothing (like empty :is()) are also
    // removed. The back-to-front scan ensures that when duplicates exist,
    // the last occurrence is preserved (important for cascade order).
    //
    // Example: RemoveDeadRulesInPlace(["a { x: 1 }", "a { x: 1 }"])
    //   => ["a { x: 1 }"] (first duplicate removed)
    std::vector<Rule> DeadRuleRemover::RemoveDeadRulesInPlace(
        uint32_t source_index, std::vector<Rule> rules,
        const std::vector<guchho::compiler::ImportRecord>& import_records) {
        uint32_t call_counter = static_cast<uint32_t>(calls_.size());
        calls_.push_back(CallEntry{import_records, source_index});

        int n = static_cast<int>(rules.size());
        int start = n;
        for (int i = n - 1; i >= 0; --i) {
            const Rule& rule = rules[static_cast<size_t>(i)];

            bool skip = false;
            if (auto* r = dynamic_cast<RSelector*>(rule.data.get());
                r && AllSelectorsAreDead(r->selectors)) {
                skip = true;
            } else if (auto hash = rule.data->Hash()) {
                HashEntry& entry = entries_[*hash];
                for (const RuleEntry& current : entry.rules) {
                    CrossFileEqualityCheck* check = nullptr;

                    if (current.call_counter != call_counter) {
                        check = &check_;
                        const CallEntry& call = calls_[current.call_counter];
                        check->import_records_a = &import_records;
                        check->import_records_b = &call.import_records;
                        check->source_index_a = source_index;
                        check->source_index_b = call.source_index;
                    }

                    if (rule.data->Equal(current.data.get(), check)) {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    entry.rules.push_back(RuleEntry{rule.data, call_counter});
                    entries_[*hash] = entry;
                }
            }

            if (!skip) {
                --start;
                rules[static_cast<size_t>(start)] = rule;
            }
        }

        if (start > 0) {
            rules.erase(rules.begin(), rules.begin() + start);
        }
        return rules;
    }

    // Checks whether any compound selector in the list contains an empty
    // :is() or :where() pseudo-class. Since these match nothing when given
    // no arguments, any selector containing them can never match any element
    // and is therefore "dead".
    // Example: ContainsDeadSelectors([:is()]) => true
    // Example: ContainsDeadSelectors([.foo]) => false
    bool ContainsDeadSelectors(const std::vector<CompoundSelector>& selectors) {
        for (const CompoundSelector& sel : selectors) {
            for (const SubclassSelector& ss : sel.subclass_selectors) {
                if (auto* pseudo = dynamic_cast<SSPseudoClassWithSelectorList*>(ss.data.get());
                    pseudo && pseudo->selectors.empty() &&
                    (pseudo->kind == PseudoClassKind::kPseudoClassIs ||
                    pseudo->kind == PseudoClassKind::kPseudoClassWhere)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Returns true only if every complex selector in the list contains at
    // least one dead selector (empty :is() or :where()). Used to determine
    // if an entire rule can be eliminated during dead code removal.
    // Example: AllSelectorsAreDead([:is(.a), :is(.b)]) => true
    // Example: AllSelectorsAreDead([:is(.a), .b]) => false
    bool AllSelectorsAreDead(const std::vector<ComplexSelector>& selectors) {
        for (const ComplexSelector& sel : selectors) {
            if (!ContainsDeadSelectors(sel.selectors)) {
                return false;
            }
        }
        return true;
    }

    // Checks whether all selectors in the list are "safe" for merging with
    // adjacent selectors. A selector is safe if it avoids features that are
    // not universally supported: nesting selectors (&), combinators before
    // IE10, namespaced type selectors before IE9, deprecated/unsupported
    // elements in IE7, case-sensitive attribute modifiers, and most pseudo-
    // classes except a small whitelist (active, first-child, hover, link,
    // visited). Pseudo-elements and :is/:where/:not are never safe.
    //
    // This prevents the minifier from merging selectors that would change
    // meaning in older browsers.
    // Example: IsSafeSelectors([".foo", ".bar"]) => true
    // Example: IsSafeSelectors(["& .foo"]) => false (nesting selector)
    bool IsSafeSelectors(const std::vector<ComplexSelector>& complex_selectors) {
        for (const ComplexSelector& complex : complex_selectors) {
            for (const CompoundSelector& compound : complex.selectors) {
                if (!compound.nesting_selector_locs.empty()) {
                    return false;
                }

                if (compound.combinator.byte_ != 0) {
                    return false;
                }

                if (compound.type_selector != nullptr) {
                    if (compound.type_selector->namespace_prefix != nullptr) {
                        return false;
                    }

                    if (compound.type_selector->name.kind == TokenType::kIdent &&
                        !IsNonDeprecatedElementSupportedByIE7(compound.type_selector->name.text)) {
                        return false;
                    }
                }

                for (const SubclassSelector& ss : compound.subclass_selectors) {
                    if (auto* s = dynamic_cast<SSAttribute*>(ss.data.get())) {
                        if (s->matcher_modifier != 0) {
                            return false;
                        }
                    } else if (auto* s2 = dynamic_cast<SSPseudoClass*>(ss.data.get())) {
                        if (s2->args == nullptr && !s2->is_element) {
                            if (s2->name == "active" || s2->name == "first-child" || s2->name == "hover" ||
                                s2->name == "link" || s2->name == "visited") {
                                continue;
                            }
                        }
                        return false;
                    } else if (dynamic_cast<SSPseudoClassWithSelectorList*>(ss.data.get()) != nullptr) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // Attempts to parse the current token(s) as either a URL (bare or
    // function-wrapped) or a string literal. Returns a tuple of (text,
    // range, success). For "url(...)" function calls, this handles the
    // special syntax where the URL can be bare or quoted inside the
    // parentheses, with optional whitespace.
    //
    // Example: ParseURLOrString() on token kString '"foo.css"'
    //   => ("foo.css", range, true)
    // Example: ParseURLOrString() on tokens kFunction "url" kString '"bar.png"' kCloseParen
    //   => ("bar.png", string_range, true)
    // Example: ParseURLOrString() on token kIdent "red"
    //   => ("", range, false)
    std::tuple<std::string, guchho::logger::Range, bool> Parser::ParseURLOrString() {
        lexer::Token t = Current();
        switch (t.kind) {
        case TokenType::kString: {
            std::string text = Decoded();
            Advance();
            return {text, t.range, true};
        }

        case TokenType::kUrl: {
            std::string text = Decoded();
            Advance();
            return {text, t.range, true};
        }

        case TokenType::kFunction: {
            if (helpers::ToLowerASCII(Decoded()) == "url") {
                guchho::logger::Loc matching_loc{Current().range.End() - 1};
                size_t i = index_ + 1;

                while (At(i).kind == TokenType::kWhitespace) {
                    ++i;
                }

                if (At(i).kind == TokenType::kString) {
                    size_t string_index = i;
                    ++i;

                    while (At(i).kind == TokenType::kWhitespace) {
                        ++i;
                    }

                    TokenType close = At(i).kind;
                    if (close == TokenType::kCloseParen || close == TokenType::kEndOfFile) {
                        lexer::Token st = At(string_index);
                        std::string text = st.DecodedText(source_->contents);
                        index_ = i;
                        ExpectWithMatchingLoc(TokenType::kCloseParen, matching_loc);
                        return {text, st.range, true};
                    }
                }
            }
            break;
        }

        default:
            break;
        }

        return {"", guchho::logger::Range{}, false};
    }

    // Same as ParseURLOrString but emits a diagnostic if the current token
    // is neither a URL nor a string. Used in contexts where a URL or string
    // is syntactically required, such as @import.
    std::tuple<std::string, guchho::logger::Range, bool> Parser::ExpectURLOrString() {
        auto [url, r, ok] = ParseURLOrString();
        if (!ok) {
            Expect(TokenType::kUrl);
        }
        return {url, r, ok};
    }

    // Parses an at-rule starting from the current "@keyword" token. This is
    // the main at-rule dispatch function. It reads the at-rule name, classifies
    // it via LookupSpecialAtRule, then branches into specific parsing logic for
    // each known at-rule type (@charset, @import, @keyframes, @layer, @media,
    // @scope, etc.). Unknown at-rules have their prelude and block parsed as
    // generic token sequences.
    //
    // The context carries validity flags that enforce ordering constraints:
    // @charset must be first, @import must come before other rules, and
    // @layer rules cannot appear between @import and @namespace rules.
    //
    // Example: ParseAtRule() on "@media (min-width: 600px) { .a { color: red } }"
    //   => RAtMedia(queries=[...], rules=[RSelector(selectors=[".a"], ...)])
    Rule Parser::ParseAtRule(const AtRuleContext& context) {
        std::string at_token = Decoded();
        guchho::logger::Range at_range = Current().range;
        std::string lower_at_token = helpers::ToLowerASCII(at_token);
        AtRuleKind kind = LookupSpecialAtRule(lower_at_token);
        Advance();

        size_t prelude_start = index_;

        bool is_keyframes = lower_at_token == "keyframes" || lower_at_token == "-webkit-keyframes" ||
                            lower_at_token == "-moz-keyframes" || lower_at_token == "-ms-keyframes" ||
                            lower_at_token == "-o-keyframes";

        if (lower_at_token == "charset") {
            switch (context.charset_validity) {
            case AtRuleValidity::kInvalid:
                log_->AddID(guchho::logger::MsgID::kCSS_InvalidAtCharset, guchho::logger::MsgKind::kWarning,
                            &tracker_, at_range,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_AtCharsetMustBeFirst));
                break;

            case AtRuleValidity::kInvalidAfter: {
                std::vector<guchho::logger::MsgData> notes{
                    tracker_.MakeMsgData(guchho::logger::Range{context.after_loc},
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_CharsetRuleBeforeNote))};
                log_->AddIDWithNotes(guchho::logger::MsgID::kCSS_InvalidAtCharset, guchho::logger::MsgKind::kWarning,
                                    &tracker_, at_range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_AtCharsetMustBeFirst),
                                    notes);
                break;
            }

            case AtRuleValidity::kValid:
                kind = AtRuleKind::kEmpty;
                Expect(TokenType::kWhitespace);
                if (Peek(TokenType::kString)) {
                    std::string encoding = Decoded();
                    if (helpers::ToLowerASCII(encoding) != "utf-8") {
                        log_->AddID(guchho::logger::MsgID::kCSS_UnsupportedAtCharset, guchho::logger::MsgKind::kWarning,
                                    &tracker_, Current().range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_UnsupportedCharsetWillUseUTF8, encoding));
                    }
                    Advance();
                    Expect(TokenType::kSemicolon);
                    Rule rule;
                    rule.loc = at_range.loc;
                    auto data = std::make_shared<RAtCharset>();
                    data->encoding = std::move(encoding);
                    rule.data = std::move(data);
                    return rule;
                }
                Expect(TokenType::kString);
                break;
            }
        } else if (lower_at_token == "import") {
            switch (context.import_validity) {
            case AtRuleValidity::kInvalid:
                log_->AddID(guchho::logger::MsgID::kCSS_InvalidAtImport, guchho::logger::MsgKind::kWarning,
                            &tracker_, at_range,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_AtImportOnlyTopLevel));
                break;

            case AtRuleValidity::kInvalidAfter: {
                std::vector<guchho::logger::MsgData> notes{
                    tracker_.MakeMsgData(guchho::logger::Range{context.after_loc},
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_AtImportRuleBeforeNote))};
                log_->AddIDWithNotes(guchho::logger::MsgID::kCSS_InvalidAtImport, guchho::logger::MsgKind::kWarning,
                                    &tracker_, at_range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_AllAtImportsMustComeFirst),
                                    notes);
                break;
            }

            case AtRuleValidity::kValid: {
                kind = AtRuleKind::kEmpty;
                Eat(TokenType::kWhitespace);
                auto [path, r, ok] = ExpectURLOrString();
                if (ok) {
                    ImportConditions conditions;
                    size_t import_conditions_start = index_;

                    Eat(TokenType::kWhitespace);
                    if ((Peek(TokenType::kIdent) || Peek(TokenType::kFunction)) && helpers::ToLowerASCII(Decoded()) == "layer") {
                        ParseComponentValue();
                        conditions.layers = ConvertTokens(std::vector<lexer::Token>(
                            tokens_.begin() + static_cast<ptrdiff_t>(import_conditions_start),
                            tokens_.begin() + static_cast<ptrdiff_t>(index_)));
                        import_conditions_start = index_;

                        if (!conditions.layers.empty()) {
                            conditions.layers[0].whitespace =
                                ClearWhitespace(conditions.layers[0].whitespace,
                                                static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore) |
                                                    static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
                        }
                    }

                    Eat(TokenType::kWhitespace);
                    if (Peek(TokenType::kFunction) && helpers::ToLowerASCII(Decoded()) == "supports") {
                        ParseComponentValue();
                        conditions.supports = ConvertTokens(std::vector<lexer::Token>(
                            tokens_.begin() + static_cast<ptrdiff_t>(import_conditions_start),
                            tokens_.begin() + static_cast<ptrdiff_t>(index_)));
                        import_conditions_start = index_;

                        if (!conditions.supports.empty()) {
                            conditions.supports[0].whitespace =
                                ClearWhitespace(conditions.supports[0].whitespace,
                                                static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore) |
                                                    static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
                        }
                    }

                    conditions.queries = ParseMediaQueryListUntil([](TokenType kind) {
                        return kind == TokenType::kSemicolon || kind == TokenType::kOpenBrace || kind == TokenType::kCloseBrace ||
                            kind == TokenType::kEndOfFile;
                    });
                    if (Peek(TokenType::kOpenBrace)) {
                        break;
                    }

                    std::shared_ptr<ImportConditions> import_conditions;
                    if (!conditions.layers.empty() || !conditions.supports.empty() || !conditions.queries.empty()) {
                        import_conditions = std::make_shared<ImportConditions>(std::move(conditions));
                    }

                    Expect(TokenType::kSemicolon);
                    uint32_t import_record_index = static_cast<uint32_t>(import_records_.size());
                    guchho::compiler::ImportRecord record;
                    record.kind = guchho::compiler::ImportKind::kAt;
                    record.path.text = path;
                    record.range = r;
                    import_records_.push_back(std::move(record));

                    if (!has_seen_at_import_) {
                        has_seen_at_import_ = true;
                        layers_pre_import_ = std::move(layers_post_import_);
                        layers_post_import_.clear();
                    }

                    Rule rule;
                    rule.loc = at_range.loc;
                    auto data = std::make_shared<RAtImport>();
                    data->import_record_index = import_record_index;
                    data->import_conditions = std::move(import_conditions);
                    rule.data = std::move(data);
                    return rule;
                }
                break;
            }
            }
        } else if (is_keyframes) {
            Eat(TokenType::kWhitespace);
            guchho::logger::Loc name_loc = Current().range.loc;
            std::string name;

            if (Peek(TokenType::kIdent)) {
                name = Decoded();
                if (IsInvalidAnimationName(name)) {
                    guchho::logger::Msg msg;
                    msg.id = guchho::logger::MsgID::kCSS_CSSSyntaxError;
                    msg.kind = guchho::logger::MsgKind::kWarning;
                    msg.data = tracker_.MakeMsgData(Current().range,
                                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_KeyframesNameWithoutQuotes, name));
                    guchho::logger::MsgData note;
                    note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_KeyframesQuoteNote, name);
                    msg.notes.push_back(std::move(note));
                    if (msg.data.location) {
                        msg.data.location->suggestion = "\"" + name + "\"";
                    }
                    log_->AddMsgID(msg.id, msg);
                    goto parse_prelude;
                }
                Advance();
            } else if (Peek(TokenType::kString)) {
                name = Decoded();
                Advance();
                if (!make_local_symbols_ && IsInvalidAnimationName(name)) {
                    goto parse_prelude;
                }
            } else if (!Expect(TokenType::kIdent)) {
                goto parse_prelude;
            }

            Eat(TokenType::kWhitespace);
            size_t block_start = index_;

            guchho::logger::Loc matching_loc = Current().range.loc;
            if (Expect(TokenType::kOpenBrace)) {
                std::vector<KeyframeBlock> blocks;
                bool bad_syntax = false;

                for (;;) {
                    switch (Current().kind) {
                    case TokenType::kWhitespace:
                        Advance();
                        continue;

                    case TokenType::kCloseBrace: {
                        guchho::logger::Loc close_brace_loc = Current().range.loc;
                        Advance();
                        Rule rule;
                        rule.loc = at_range.loc;
                        auto data = std::make_shared<RAtKeyframes>();
                        data->at_token = at_token;
                        data->name = SymbolForName(name_loc, name);
                        data->blocks = std::move(blocks);
                        data->close_brace_loc = close_brace_loc;
                        rule.data = std::move(data);
                        return rule;
                    }

                    case TokenType::kEndOfFile:
                        bad_syntax = true;
                        break;

                    case TokenType::kOpenBrace:
                        Expect(TokenType::kPercentage);
                        bad_syntax = true;
                        break;

                    default: {
                        std::vector<std::string> selectors;
                        guchho::logger::Loc first_selector_loc;
                        bool selectors_done = false;

                        while (!selectors_done) {
                            lexer::Token t = Current();
                            switch (t.kind) {
                            case TokenType::kWhitespace:
                                Advance();
                                continue;

                            case TokenType::kOpenBrace: {
                                guchho::logger::Loc block_matching_loc = Current().range.loc;
                                Advance();
                                std::vector<Rule> rules = ParseListOfDeclarations(ListOfDeclarationsOpts{});
                                guchho::logger::Loc close_brace_loc = Current().range.loc;
                                if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, block_matching_loc)) {
                                    close_brace_loc = guchho::logger::Loc{};
                                }

                                if (!options_.minify_syntax || !rules.empty()) {
                                    KeyframeBlock block;
                                    block.selectors = std::move(selectors);
                                    block.rules = std::move(rules);
                                    block.loc = first_selector_loc;
                                    block.close_brace_loc = close_brace_loc;
                                    blocks.push_back(std::move(block));
                                }
                                selectors_done = true;
                                continue;
                            }

                            case TokenType::kCloseBrace:
                            case TokenType::kEndOfFile:
                                Expect(TokenType::kOpenBrace);
                                bad_syntax = true;
                                selectors_done = true;
                                continue;

                            case TokenType::kIdent:
                            case TokenType::kPercentage: {
                                if (first_selector_loc.start == 0) {
                                    first_selector_loc = Current().range.loc;
                                }
                                std::string text = Decoded();
                                if (t.kind == TokenType::kIdent) {
                                    if (helpers::ToLowerASCII(text) == "from") {
                                        if (options_.minify_syntax) {
                                            text = "0%";
                                        }
                                    } else if (helpers::ToLowerASCII(text) != "to") {
                                        Expect(TokenType::kPercentage);
                                    }
                                } else if (options_.minify_syntax && text == "100%") {
                                    text = "to";
                                }
                                selectors.push_back(text);
                                Advance();

                                Eat(TokenType::kWhitespace);
                                if (Eat(TokenType::kComma)) {
                                    Eat(TokenType::kWhitespace);
                                    TokenType k = Current().kind;
                                    if (k != TokenType::kIdent && k != TokenType::kPercentage) {
                                        Expect(TokenType::kPercentage);
                                        bad_syntax = true;
                                        selectors_done = true;
                                    }
                                } else {
                                    TokenType k = Current().kind;
                                    if (k != TokenType::kOpenBrace && k != TokenType::kCloseBrace && k != TokenType::kEndOfFile) {
                                        Expect(TokenType::kComma);
                                        bad_syntax = true;
                                        selectors_done = true;
                                    }
                                }
                                continue;
                            }

                            default:
                                Expect(TokenType::kPercentage);
                                bad_syntax = true;
                                selectors_done = true;
                                continue;
                            }
                        }
                    }
                    }
                    if (bad_syntax) {
                        break;
                    }
                }

                while (!Peek(TokenType::kCloseBrace) && !Peek(TokenType::kEndOfFile)) {
                    ParseComponentValue();
                }
                ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc);
                std::vector<Token> prelude = ConvertTokens(std::vector<lexer::Token>(
                    tokens_.begin() + static_cast<ptrdiff_t>(prelude_start),
                    tokens_.begin() + static_cast<ptrdiff_t>(block_start)));
                ConvertTokensOpts cvt_opts;
                cvt_opts.allow_imports = true;
                auto [block, _] = ConvertTokensHelper(
                    std::vector<lexer::Token>(tokens_.begin() + static_cast<ptrdiff_t>(block_start),
                                            tokens_.begin() + static_cast<ptrdiff_t>(index_)),
                    TokenType::kEndOfFile, cvt_opts);
                Rule rule;
                rule.loc = at_range.loc;
                auto data = std::make_shared<RUnknownAt>();
                data->at_token = at_token;
                data->prelude = std::move(prelude);
                data->block = std::move(block);
                rule.data = std::move(data);
                return rule;
            }
        } else if (lower_at_token == "layer") {
            std::vector<std::vector<std::string>> names;
            Eat(TokenType::kWhitespace);
            if (Peek(TokenType::kIdent)) {
                for (;;) {
                    auto [ident, ok] = ExpectValidLayerNameIdent();
                    if (!ok) {
                        goto parse_prelude;
                    }
                    std::vector<std::string> name{ident};
                    for (;;) {
                        Eat(TokenType::kWhitespace);
                        if (!Eat(TokenType::kDelimDot)) {
                            break;
                        }
                        Eat(TokenType::kWhitespace);
                        auto [ident2, ok2] = ExpectValidLayerNameIdent();
                        if (!ok2) {
                            goto parse_prelude;
                        }
                        name.push_back(ident2);
                    }
                    names.push_back(std::move(name));
                    Eat(TokenType::kWhitespace);
                    if (!Eat(TokenType::kComma)) {
                        break;
                    }
                    Eat(TokenType::kWhitespace);
                }
            }

            guchho::logger::Loc matching_loc = Current().range.loc;
            if (names.size() <= 1 && Eat(TokenType::kOpenBrace)) {
                RecordAtLayerRule(names);
                std::vector<std::string> old_enclosing_layer = enclosing_layer_;
                if (names.size() == 1) {
                    enclosing_layer_.insert(enclosing_layer_.end(), names[0].begin(), names[0].end());
                } else {
                    ++anon_layer_count_;
                }

                std::vector<Rule> rules;
                if (context.is_declaration_list) {
                    ListOfDeclarationsOpts dopt;
                    dopt.can_inline_no_op_nesting = context.can_inline_no_op_nesting;
                    rules = ParseListOfDeclarations(dopt);
                } else {
                    RuleContext rctx;
                    rctx.parse_selectors = true;
                    rules = ParseListOfRules(rctx);
                }

                if (names.size() != 1) {
                    --anon_layer_count_;
                }
                enclosing_layer_ = old_enclosing_layer;
                guchho::logger::Loc close_brace_loc = Current().range.loc;
                if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                    close_brace_loc = guchho::logger::Loc{};
                }
                Rule rule;
                rule.loc = at_range.loc;
                auto data = std::make_shared<RAtLayer>();
                data->names = std::move(names);
                data->rules = std::move(rules);
                data->close_brace_loc = close_brace_loc;
                data->has_block = true;
                rule.data = std::move(data);
                return rule;
            }

            if (names.size() >= 1 && Eat(TokenType::kSemicolon)) {
                RecordAtLayerRule(names);
                Rule rule;
                rule.loc = at_range.loc;
                auto data = std::make_shared<RAtLayer>();
                data->names = std::move(names);
                rule.data = std::move(data);
                return rule;
            }

            switch (Current().kind) {
            case TokenType::kEndOfFile:
                Expect(TokenType::kSemicolon);
                RecordAtLayerRule(names);
                {
                    Rule rule;
                    rule.loc = at_range.loc;
                    auto data = std::make_shared<RAtLayer>();
                    data->names = std::move(names);
                    rule.data = std::move(data);
                    return rule;
                }

            case TokenType::kCloseBrace:
                Expect(TokenType::kSemicolon);
                if (!context.is_top_level) {
                    RecordAtLayerRule(names);
                    Rule rule;
                    rule.loc = at_range.loc;
                    auto data = std::make_shared<RAtLayer>();
                    data->names = std::move(names);
                    rule.data = std::move(data);
                    return rule;
                }
                break;

            case TokenType::kOpenBrace:
                Expect(TokenType::kSemicolon);
                break;

            default:
                Unexpected();
                break;
            }
        } else if (lower_at_token == "media") {
            std::vector<MediaQuery> queries = ParseMediaQueryListUntil([](TokenType kind) {
                return kind == TokenType::kOpenBrace;
            });

            guchho::logger::Loc matching_loc = Current().range.loc;
            if (!Expect(TokenType::kOpenBrace)) {
                goto parse_prelude;
            }

            enclosing_at_media_.push_back(queries);

            std::vector<Rule> rules;
            if (context.is_declaration_list) {
                ListOfDeclarationsOpts dopt;
                dopt.can_inline_no_op_nesting = context.can_inline_no_op_nesting;
                rules = ParseListOfDeclarations(dopt);
            } else {
                RuleContext rctx;
                rctx.parse_selectors = true;
                rules = ParseListOfRules(rctx);
            }

            enclosing_at_media_.pop_back();

            guchho::logger::Loc close_brace_loc = Current().range.loc;
            if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                close_brace_loc = guchho::logger::Loc{};
            }

            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RAtMedia>();
            data->queries = std::move(queries);
            data->rules = std::move(rules);
            data->close_brace_loc = close_brace_loc;
            rule.data = std::move(data);
            return rule;
        } else if (lower_at_token == "scope") {
            bool ok = true;

            std::vector<ComplexSelector> start;
            Eat(TokenType::kWhitespace);
            if (Eat(TokenType::kOpenParen)) {
                ParseSelectorOpts sopts;
                sopts.stop_on_close_paren = true;
                std::tie(start, ok) = ParseSelectorList(sopts);
                if (!ok || !Expect(TokenType::kCloseParen)) {
                    goto parse_prelude;
                }
                Eat(TokenType::kWhitespace);
            }

            std::vector<ComplexSelector> end;
            if (helpers::ToLowerASCII(Decoded()) == "to" && Eat(TokenType::kIdent)) {
                Eat(TokenType::kWhitespace);
                if (!Expect(TokenType::kOpenParen)) {
                    goto parse_prelude;
                }
                ParseSelectorOpts sopts;
                sopts.stop_on_close_paren = true;
                std::tie(end, ok) = ParseSelectorList(sopts);
                if (!ok || !Expect(TokenType::kCloseParen)) {
                    goto parse_prelude;
                }
            }
            Eat(TokenType::kWhitespace);

            guchho::logger::Loc matching_loc = Current().range.loc;
            if (!Expect(TokenType::kOpenBrace)) {
                goto parse_prelude;
            }

            std::vector<Rule> rules;
            if (context.is_declaration_list) {
                ListOfDeclarationsOpts dopt;
                dopt.can_inline_no_op_nesting = context.can_inline_no_op_nesting;
                rules = ParseListOfDeclarations(dopt);
            } else {
                RuleContext rctx;
                rctx.parse_selectors = true;
                rules = ParseListOfRules(rctx);
            }

            guchho::logger::Loc close_brace_loc = Current().range.loc;
            if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                close_brace_loc = guchho::logger::Loc{};
            }

            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RAtScope>();
            data->start = std::move(start);
            data->end = std::move(end);
            data->rules = std::move(rules);
            data->close_brace_loc = close_brace_loc;
            rule.data = std::move(data);
            return rule;
        } else {
            if (kind == AtRuleKind::kUnknown && lower_at_token == "namespace") {
                log_->AddID(guchho::logger::MsgID::kCSS_UnsupportedAtNamespace, guchho::logger::MsgKind::kWarning,
                            &tracker_, at_range,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_AtNamespaceNotSupported));
            }
        }

    parse_prelude:
        index_ = prelude_start;
        std::vector<Token> prelude;
        for (;;) {
            switch (Current().kind) {
            case TokenType::kOpenBrace:
            case TokenType::kEndOfFile:
                goto prelude_done;

            case TokenType::kSemicolon:
            case TokenType::kCloseBrace:
                prelude = ConvertTokens(std::vector<lexer::Token>(
                    tokens_.begin() + static_cast<ptrdiff_t>(prelude_start),
                    tokens_.begin() + static_cast<ptrdiff_t>(index_)));

                switch (kind) {
                case AtRuleKind::kQualifiedOrEmpty:
                    goto prelude_done;

                case AtRuleKind::kEmpty:
                case AtRuleKind::kUnknown: {
                    Expect(TokenType::kSemicolon);
                    Rule rule;
                    rule.loc = at_range.loc;
                    auto data = std::make_shared<RUnknownAt>();
                    data->at_token = at_token;
                    data->prelude = std::move(prelude);
                    rule.data = std::move(data);
                    return rule;
                }

                default:
                    Expect(TokenType::kOpenBrace);
                    Eat(TokenType::kSemicolon);
                    Rule rule;
                    rule.loc = at_range.loc;
                    auto data = std::make_shared<RUnknownAt>();
                    data->at_token = at_token;
                    data->prelude = std::move(prelude);
                    rule.data = std::move(data);
                    return rule;
                }

            default:
                ParseComponentValue();
            }
        }

    prelude_done:
        if (prelude.empty()) {
            prelude = ConvertTokens(std::vector<lexer::Token>(
                tokens_.begin() + static_cast<ptrdiff_t>(prelude_start),
                tokens_.begin() + static_cast<ptrdiff_t>(index_)));
        }
        size_t block_start = index_;

        switch (kind) {
        case AtRuleKind::kEmpty: {
            Expect(TokenType::kSemicolon);
            ParseBlock(TokenType::kOpenBrace, TokenType::kCloseBrace);
            std::vector<Token> block = ConvertTokens(std::vector<lexer::Token>(
                tokens_.begin() + static_cast<ptrdiff_t>(block_start),
                tokens_.begin() + static_cast<ptrdiff_t>(index_)));
            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RUnknownAt>();
            data->at_token = at_token;
            data->prelude = std::move(prelude);
            data->block = std::move(block);
            rule.data = std::move(data);
            return rule;
        }

        case AtRuleKind::kDeclarations: {
            guchho::logger::Loc matching_loc = Current().range.loc;
            Expect(TokenType::kOpenBrace);
            std::vector<Rule> rules = ParseListOfDeclarations(ListOfDeclarationsOpts{});
            guchho::logger::Loc close_brace_loc = Current().range.loc;
            if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                close_brace_loc = guchho::logger::Loc{};
            }

            if (prelude.size() == 1 && lower_at_token == "counter-style") {
                Token& t = prelude[0];
                if (t.kind == TokenType::kIdent) {
                    t.kind = TokenType::kSymbol;
                    t.payload_index = SymbolForName(t.loc, t.text).ref.inner_index;
                }
            }

            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RKnownAt>();
            data->at_token = at_token;
            data->prelude = std::move(prelude);
            data->rules = std::move(rules);
            data->close_brace_loc = close_brace_loc;
            rule.data = std::move(data);
            return rule;
        }

        case AtRuleKind::kInheritContext: {
            guchho::logger::Loc matching_loc = Current().range.loc;
            Expect(TokenType::kOpenBrace);

            std::vector<Rule> rules;
            if (context.is_declaration_list) {
                ListOfDeclarationsOpts dopt;
                dopt.can_inline_no_op_nesting = context.can_inline_no_op_nesting;
                rules = ParseListOfDeclarations(dopt);
            } else {
                RuleContext rctx;
                rctx.parse_selectors = true;
                rules = ParseListOfRules(rctx);
            }

            guchho::logger::Loc close_brace_loc = Current().range.loc;
            if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                close_brace_loc = guchho::logger::Loc{};
            }

            if (!prelude.empty() && lower_at_token == "container") {
                Token& t = prelude[0];
                if (t.kind == TokenType::kIdent && helpers::ToLowerASCII(t.text) != "not") {
                    t.kind = TokenType::kSymbol;
                    t.payload_index = SymbolForName(t.loc, t.text).ref.inner_index;
                }
            }

            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RKnownAt>();
            data->at_token = at_token;
            data->prelude = std::move(prelude);
            data->rules = std::move(rules);
            data->close_brace_loc = close_brace_loc;
            rule.data = std::move(data);
            return rule;
        }

        case AtRuleKind::kQualifiedOrEmpty: {
            guchho::logger::Loc matching_loc = Current().range.loc;
            if (Eat(TokenType::kOpenBrace)) {
                RuleContext rctx;
                rctx.parse_selectors = true;
                std::vector<Rule> rules = ParseListOfRules(rctx);
                guchho::logger::Loc close_brace_loc = Current().range.loc;
                if (!ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                    close_brace_loc = guchho::logger::Loc{};
                }
                Rule rule;
                rule.loc = at_range.loc;
                auto data = std::make_shared<RKnownAt>();
                data->at_token = at_token;
                data->prelude = std::move(prelude);
                data->rules = std::move(rules);
                data->close_brace_loc = close_brace_loc;
                rule.data = std::move(data);
                return rule;
            }
            Expect(TokenType::kSemicolon);
            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RKnownAt>();
            data->at_token = at_token;
            data->prelude = std::move(prelude);
            rule.data = std::move(data);
            return rule;
        }

        default: {
            ParseBlock(TokenType::kOpenBrace, TokenType::kCloseBrace);
            ConvertTokensOpts cvt_opts;
            cvt_opts.allow_imports = true;
            auto [block, _] = ConvertTokensHelper(
                std::vector<lexer::Token>(tokens_.begin() + static_cast<ptrdiff_t>(block_start),
                                        tokens_.begin() + static_cast<ptrdiff_t>(index_)),
                TokenType::kEndOfFile, cvt_opts);
            Rule rule;
            rule.loc = at_range.loc;
            auto data = std::make_shared<RUnknownAt>();
            data->at_token = at_token;
            data->prelude = std::move(prelude);
            data->block = std::move(block);
            rule.data = std::move(data);
            return rule;
        }
        }
    }

    // Validates and consumes an identifier token that is used as a layer name.
    // CSS reserves "initial", "inherit", and "unset" as invalid layer names.
    // Returns the decoded name and true on success, or empty string and false
    // on failure (with a diagnostic emitted).
    // Example: ExpectValidLayerNameIdent() on token "my-layer" => ("my-layer", true)
    // Example: ExpectValidLayerNameIdent() on token "initial" => ("", false) + warning
    std::pair<std::string, bool> Parser::ExpectValidLayerNameIdent() {
        guchho::logger::Range r = Current().range;
        std::string text = Decoded();
        if (!Expect(TokenType::kIdent)) {
            return {"", false};
        }
        if (text == "initial" || text == "inherit" || text == "unset") {
            log_->AddID(guchho::logger::MsgID::kCSS_InvalidAtLayer, guchho::logger::MsgKind::kWarning, &tracker_,
                        r,
                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_LayerNameNotAllowed, text));
            prev_error_ = r.loc;
            return {"", false};
        }
        return {text, true};
    }

    // Converts a raw lexer token sequence into the parser's higher-level Token
    // representation. This handles recursive descent into nested structures
    // (parentheses, braces, brackets, function calls), tracks whitespace
    // relationships between tokens, records url() imports, simplifies calc()
    // expressions during minification, and mangling numbers/dimensions.
    //
    // The close parameter specifies which token type terminates this sequence
    // (e.g., kCloseParen for function arguments, kEndOfFile for top-level).
    //
    // Verbatim whitespace mode is automatically enabled for CSS custom property
    // definitions (--variable: ...) where whitespace is semantically significant.
    //
    // Example: ConvertTokensHelper([ident "a", colon, ws, ident "red"], kEndOfFile)
    //   => ([Token(ident,"a"), Token(colon,":"), Token(ident,"red")], [])
    std::vector<Token> Parser::ConvertTokens(const std::vector<lexer::Token>& tokens) {
        return ConvertTokensHelper(tokens, TokenType::kEndOfFile, ConvertTokensOpts{}).first;
    }

    std::pair<std::vector<Token>, std::vector<lexer::Token>> Parser::ConvertTokensHelper(
        std::vector<lexer::Token> tokens, TokenType close, const ConvertTokensOpts& opts) {
        std::vector<Token> result;
        WhitespaceFlags next_whitespace = WhitespaceFlags::kNone;
        ConvertTokensOpts opts_copy = opts;

        if (!opts_copy.verbatim_whitespace) {
            for (size_t i = 0; i < tokens.size(); ++i) {
                const lexer::Token& t = tokens[i];
                if (t.kind == TokenType::kWhitespace) {
                    continue;
                }
                if (t.kind == TokenType::kIdent && t.DecodedText(source_->contents).rfind("--", 0) == 0) {
                    for (size_t j = i + 1; j < tokens.size(); ++j) {
                        const lexer::Token& t2 = tokens[j];
                        if (t2.kind == TokenType::kWhitespace) {
                            continue;
                        }
                        if (t2.kind == TokenType::kColon) {
                            opts_copy.verbatim_whitespace = true;
                        }
                        break;
                    }
                }
                break;
            }
        }

        for (;;) {
            if (tokens.empty()) {
                break;
            }
            lexer::Token t = tokens[0];
            tokens.erase(tokens.begin());
            if (t.kind == close) {
                break;
            }
            Token token;
            token.loc = t.range.loc;
            token.kind = t.kind;
            token.text = t.DecodedText(source_->contents);
            token.whitespace = next_whitespace;
            next_whitespace = WhitespaceFlags::kNone;

            if (opts_copy.is_inside_calc_function && IsNumeric(t.kind) && !result.empty() &&
                IsNumeric(result.back().kind) &&
                (token.text.rfind("+", 0) == 0 || token.text.rfind("-", 0) == 0)) {
                log_->AddID(guchho::logger::MsgID::kCSS_InvalidCalc, guchho::logger::MsgKind::kWarning,
                            &tracker_, guchho::logger::Range{t.range.loc, 1},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_WhitespaceBothSidesOperator, token.text.substr(0, 1)));
            }

            switch (t.kind) {
            case TokenType::kWhitespace:
                if (!result.empty()) {
                    result.back().whitespace = result.back().whitespace | WhitespaceFlags::kWhitespaceAfter;
                }
                next_whitespace = WhitespaceFlags::kWhitespaceBefore;
                continue;

            case TokenType::kDelimPlus:
            case TokenType::kDelimMinus:
                if (opts_copy.is_inside_calc_function && !tokens.empty()) {
                    if (result.empty() || result.back().kind == TokenType::kComma) {
                        log_->AddID(guchho::logger::MsgID::kCSS_InvalidCalc, guchho::logger::MsgKind::kWarning,
                                    &tracker_, t.range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_InfixOnlyOperator, token.text));
                    } else if (token.whitespace != WhitespaceFlags::kWhitespaceBefore ||
                            tokens[0].kind != TokenType::kWhitespace) {
                        log_->AddID(guchho::logger::MsgID::kCSS_InvalidCalc, guchho::logger::MsgKind::kWarning,
                                    &tracker_, t.range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_WhitespaceBothSidesOperator, token.text));
                    }
                }
                break;

            case TokenType::kNumber:
                if (options_.minify_syntax) {
                    if (auto [text, ok] = MangleNumber(token.text); ok) {
                        token.text = text;
                    }
                }
                break;

            case TokenType::kPercentage:
                if (options_.minify_syntax) {
                    if (auto [text, ok] = MangleNumber(token.PercentageValue()); ok) {
                        token.text = text + "%";
                    }
                }
                break;

            case TokenType::kDimension:
                token.unit_offset = t.unit_offset;

                if (options_.minify_syntax) {
                    if (auto [text, ok] = MangleNumber(token.DimensionValue()); ok) {
                        token.text = text + token.DimensionUnit();
                        token.unit_offset = static_cast<uint16_t>(text.size());
                    }

                    if (auto [value, unit, ok] = MangleDimension(token.DimensionValue(), token.DimensionUnit()); ok) {
                        token.text = value + unit;
                        token.unit_offset = static_cast<uint16_t>(value.size());
                    }
                }
                break;

            case TokenType::kUrl: {
                token.payload_index = static_cast<uint32_t>(import_records_.size());
                guchho::compiler::ImportRecordFlags flags = guchho::compiler::ImportRecordFlags::kNone;
                if (!opts_copy.allow_imports) {
                    flags = flags | guchho::compiler::ImportRecordFlags::kIsUnused;
                }
                guchho::compiler::ImportRecord record;
                record.kind = guchho::compiler::ImportKind::kUrl;
                record.path.text = token.text;
                record.range = t.range;
                record.flags = flags;
                import_records_.push_back(std::move(record));
                token.text = "";
                break;
            }

            case TokenType::kFunction: {
                std::vector<lexer::Token> original = tokens;
                ConvertTokensOpts nested_opts = opts_copy;
                if (helpers::ToLowerASCII(token.text) == "var") {
                    nested_opts.verbatim_whitespace = true;
                }
                if (helpers::ToLowerASCII(token.text) == "calc") {
                    nested_opts.is_inside_calc_function = true;
                }
                auto nested_result = ConvertTokensHelper(tokens, TokenType::kCloseParen, nested_opts);
                token.children = std::make_shared<std::vector<Token>>(std::move(nested_result.first));
                tokens = std::move(nested_result.second);

                if (options_.minify_syntax && helpers::ToLowerASCII(token.text) == "calc") {
                    token = TryToReduceCalcExpression(std::move(token), options_.minify_whitespace);
                }

                if (helpers::ToLowerASCII(token.text) == "url" && token.children && token.children->size() == 1 &&
                    (*token.children)[0].kind == TokenType::kString) {
                    std::string url_text = (*token.children)[0].text;
                    token.kind = TokenType::kUrl;
                    token.text = "";
                    token.children.reset();
                    token.payload_index = static_cast<uint32_t>(import_records_.size());
                    guchho::compiler::ImportRecordFlags url_flags = guchho::compiler::ImportRecordFlags::kNone;
                    if (!opts_copy.allow_imports) {
                        url_flags = url_flags | guchho::compiler::ImportRecordFlags::kIsUnused;
                    }
                    guchho::compiler::ImportRecord record;
                    record.kind = guchho::compiler::ImportKind::kUrl;
                    record.path.text = url_text;
                    record.range = original[0].range;
                    record.flags = url_flags;
                    import_records_.push_back(std::move(record));
                }
                break;
            }

            case TokenType::kOpenParen: {
                auto nested_result = ConvertTokensHelper(tokens, TokenType::kCloseParen, opts_copy);
                token.children = std::make_shared<std::vector<Token>>(std::move(nested_result.first));
                tokens = std::move(nested_result.second);
                break;
            }

            case TokenType::kOpenBrace: {
                auto nested_result = ConvertTokensHelper(tokens, TokenType::kCloseBrace, opts_copy);
                std::vector<Token>& nested = nested_result.first;

                if (!opts_copy.verbatim_whitespace && !options_.minify_whitespace && !nested.empty()) {
                    nested[0].whitespace = nested[0].whitespace | WhitespaceFlags::kWhitespaceBefore;
                    nested.back().whitespace = nested.back().whitespace | WhitespaceFlags::kWhitespaceAfter;
                }

                token.children = std::make_shared<std::vector<Token>>(std::move(nested));
                tokens = std::move(nested_result.second);
                break;
            }

            case TokenType::kOpenBracket: {
                auto nested_result = ConvertTokensHelper(tokens, TokenType::kCloseBracket, opts_copy);
                token.children = std::make_shared<std::vector<Token>>(std::move(nested_result.first));
                tokens = std::move(nested_result.second);
                break;
            }

            default:
                break;
            }

            result.push_back(std::move(token));
        }

        if (!opts_copy.verbatim_whitespace) {
            for (size_t i = 0; i < result.size(); ++i) {
                Token& token = result[i];

                if (i == 0) {
                    token.whitespace =
                        ClearWhitespace(token.whitespace, static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
                }
                if (i + 1 == result.size()) {
                    token.whitespace =
                        ClearWhitespace(token.whitespace, static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
                }

                switch (token.kind) {
                case TokenType::kComma:
                    token.whitespace =
                        ClearWhitespace(token.whitespace, static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
                    if (i > 0) {
                        result[i - 1].whitespace =
                            ClearWhitespace(result[i - 1].whitespace,
                                            static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
                    }

                    if (options_.minify_whitespace) {
                        token.whitespace =
                            ClearWhitespace(token.whitespace, static_cast<uint8_t>(WhitespaceFlags::kWhitespaceAfter));
                        if (i + 1 < result.size()) {
                            result[i + 1].whitespace =
                                ClearWhitespace(result[i + 1].whitespace,
                                                static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
                        }
                    } else {
                        token.whitespace = token.whitespace | WhitespaceFlags::kWhitespaceAfter;
                        if (i + 1 < result.size()) {
                            result[i + 1].whitespace = result[i + 1].whitespace | WhitespaceFlags::kWhitespaceBefore;
                        }
                    }
                    break;

                default:
                    break;
                }
            }
        }

        if (opts_copy.verbatim_whitespace && result.empty() && next_whitespace == WhitespaceFlags::kWhitespaceBefore) {
            Token ws;
            ws.kind = TokenType::kWhitespace;
            result.push_back(std::move(ws));
        }

        return {std::move(result), std::move(tokens)};
    }

    // Shifts the decimal point in a numeric string by dot_offset positions.
    // Returns the modified string and true on success, or empty string and
    // false if the number uses scientific notation (which is not handled).
    // Leading/trailing zeros are stripped and the dot is repositioned.
    //
    // Example: ShiftDot("1.5", -1) => ("15", true)   -- shifts dot left
    // Example: ShiftDot("15", 1) => ("1.5", true)     -- shifts dot right
    // Example: ShiftDot("1e5", 1) => ("", false)       -- scientific notation unsupported
    std::pair<std::string, bool> ShiftDot(std::string text, int dot_offset) {
        if (text.find_first_of("eE") != std::string::npos) {
            return {"", false};
        }

        std::string sign;
        if (!text.empty() && (text[0] == '-' || text[0] == '+')) {
            sign = text.substr(0, 1);
            text = text.substr(1);
        }

        size_t dot = text.find('.');
        std::string digits;
        if (dot == std::string::npos) {
            dot = text.size();
            digits = text;
        } else {
            digits = text.substr(0, dot) + text.substr(dot + 1);
        }

        int new_dot = static_cast<int>(dot) + dot_offset;

        while (!digits.empty() && new_dot > 0 && digits[0] == '0') {
            digits = digits.substr(1);
            --new_dot;
        }

        while (!digits.empty() && static_cast<int>(digits.size()) > new_dot && digits.back() == '0') {
            digits.pop_back();
        }

        if (new_dot >= static_cast<int>(digits.size())) {
            std::string trailing(static_cast<size_t>(new_dot - static_cast<int>(digits.size())), '0');
            return {sign + digits + trailing, true};
        }

        if (new_dot < 0) {
            digits = std::string(static_cast<size_t>(-new_dot), '0') + digits;
            new_dot = 0;
        }

        return {sign + digits.substr(0, static_cast<size_t>(new_dot)) + "." +
                    digits.substr(static_cast<size_t>(new_dot)),
                true};
    }

    // Attempts to convert a CSS dimension value between time units (ms and s)
    // if doing so produces a shorter representation. Returns the new value,
    // new unit, and true if a conversion was made; otherwise returns false.
    //
    // Example: MangleDimension("1000", "ms") => ("1", "s", true) -- 1000ms => 1s
    // Example: MangleDimension("0.5", "s") => ("500", "ms", true) -- 0.5s => 500ms
    // Example: MangleDimension("100", "px") => ("", "", false)   -- no conversion possible
    std::tuple<std::string, std::string, bool> MangleDimension(std::string value, std::string unit) {
        constexpr int ms_len = 2;
        constexpr int s_len = 1;

        if (helpers::ToLowerASCII(unit) == "ms") {
            if (auto [shifted, ok] = ShiftDot(value, -3);
                ok && static_cast<int>(shifted.size()) + s_len < static_cast<int>(value.size()) + ms_len) {
                return {shifted, "s", true};
            }
        }
        if (helpers::ToLowerASCII(unit) == "s") {
            if (auto [shifted, ok] = ShiftDot(value, 3);
                ok && static_cast<int>(shifted.size()) + ms_len < static_cast<int>(value.size()) + s_len) {
                return {shifted, "ms", true};
            }
        }

        return {"", "", false};
    }

    // Simplifies a CSS numeric literal by removing unnecessary trailing zeros,
    // removing the decimal point when no fractional part remains, and stripping
    // leading zeros from the integer part. Returns the simplified string and
    // whether any change was made.
    //
    // Example: MangleNumber("1.0") => ("1", true)
    // Example: MangleNumber("0.500") => (".5", true)
    // Example: MangleNumber("100") => ("100", false)
    // Example: MangleNumber("+0.5") => ("+.5", true)
    std::pair<std::string, bool> MangleNumber(std::string t) {
        std::string original = t;

        size_t dot = t.find('.');
        if (dot != std::string::npos) {
            while (!t.empty() && t.back() == '0') {
                t.pop_back();
            }

            if (dot + 1 == t.size()) {
                t = t.substr(0, dot);
                if (t.empty() || t == "+" || t == "-") {
                    t += "0";
                }
            } else {
                if (t.size() >= 3 && t[0] == '0' && t[1] == '.' && t[2] >= '0' && t[2] <= '9') {
                    t = t.substr(1);
                } else if (t.size() >= 4 && (t[0] == '+' || t[0] == '-') && t[1] == '0' && t[2] == '.' &&
                        t[3] >= '0' && t[3] <= '9') {
                    t = t.substr(0, 1) + t.substr(2);
                }
            }
        }

        return {t, t != original};
    }

    // Attempts to parse the current tokens as a CSS selector rule. First tries
    // to parse the prelude as a valid selector list. If that succeeds, parses
    // the following block as declarations. If selector parsing fails, falls
    // back to ParseQualifiedRule which treats the prelude as an arbitrary
    // token sequence.
    //
    // This function also manages the composes_context for CSS Modules support,
    // determining whether "composes:" declarations can refer to parent classes.
    //
    // Example: ParseSelectorRule(true, {}) on ".a { color: red }"
    //   => RSelector(selectors=[".a"], rules=[RDeclaration("color", "red")])
    // Example: ParseSelectorRule(true, {}) on "123 { }" (invalid selector)
    //   => RQualified(prelude=["123"], rules=[]) (falls through to qualified rule)
    Rule Parser::ParseSelectorRule(bool is_top_level, const ParseSelectorOpts& opts) {
        bool local = make_local_symbols_;
        size_t prelude_start = index_;

        auto [list, ok] = ParseSelectorList(opts);
        if (ok) {
            bool can_inline_no_op_nesting = true;
            for (const ComplexSelector& sel : list) {
                if (sel.UsesPseudoElement()) {
                    can_inline_no_op_nesting = false;
                    break;
                }
            }
            auto selector = std::make_shared<RSelector>();
            selector->selectors = std::move(list);
            guchho::logger::Loc matching_loc = Current().range.loc;
            if (Expect(TokenType::kOpenBrace)) {
                ++in_selector_subtree_;
                ListOfDeclarationsOpts decl_opts;
                decl_opts.can_inline_no_op_nesting = can_inline_no_op_nesting;

                ComposesContext composes_context;
                if (opts.composes_context != nullptr && selector->selectors.size() == 1 &&
                    selector->selectors[0].selectors.size() == 1 &&
                    selector->selectors[0].selectors[0].IsSingleAmpersand()) {
                    decl_opts.composes_context = opts.composes_context;
                } else {
                    composes_context.parent_range = selector->selectors[0].selectors[0].Range();
                    if (opts.composes_context != nullptr) {
                        composes_context.problem_range = opts.composes_context->parent_range;
                    }
                    for (const ComplexSelector& sel : selector->selectors) {
                        const CompoundSelector& first = sel.selectors[0];
                        if (first.combinator.byte_ != 0) {
                            composes_context.problem_range = guchho::logger::Range{first.combinator.loc, 1};
                        } else if (first.type_selector != nullptr) {
                            composes_context.problem_range = first.type_selector->Range();
                        } else if (!first.nesting_selector_locs.empty()) {
                            composes_context.problem_range = guchho::logger::Range{first.nesting_selector_locs[0], 1};
                        } else {
                            for (size_t i = 0; i < first.subclass_selectors.size(); ++i) {
                                const SubclassSelector& ss = first.subclass_selectors[i];
                                auto* class_ss = dynamic_cast<SSClass*>(ss.data.get());
                                if (i > 0 || class_ss == nullptr) {
                                    composes_context.problem_range = ss.range;
                                } else {
                                    composes_context.parent_refs.push_back(class_ss->name.ref);
                                }
                            }
                        }
                        if (composes_context.problem_range.len > 0) {
                            break;
                        }
                        if (sel.selectors.size() > 1) {
                            composes_context.problem_range = sel.selectors[1].Range();
                            break;
                        }
                    }
                    decl_opts.composes_context = &composes_context;
                }

                selector->rules = ParseListOfDeclarations(decl_opts);
                --in_selector_subtree_;
                guchho::logger::Loc close_brace_loc = Current().range.loc;
                if (ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                    selector->close_brace_loc = close_brace_loc;
                }
                make_local_symbols_ = local;
                Rule rule;
                rule.loc = tokens_[prelude_start].range.loc;
                rule.data = std::move(selector);
                return rule;
            }
        }

        make_local_symbols_ = local;
        index_ = prelude_start;

        ParseQualifiedRuleOpts popts;
        popts.is_already_invalid = true;
        popts.is_top_level = is_top_level;
        popts.is_declaration_context = opts.is_declaration_context;
        return ParseQualifiedRule(popts);
    }

    // Parses a generic CSS qualified rule: an arbitrary prelude (token sequence
    // before the opening brace) followed by a declaration block. Unlike
    // ParseSelectorRule, this does not attempt to interpret the prelude as a
    // selector. It consumes tokens until an opening brace, end-of-file, or
    // (in non-top-level context) a closing brace is found.
    //
    // If in a declaration context and a semicolon is found before an opening
    // brace, the tokens are treated as a bad declaration rather than a rule.
    //
    // Example: ParseQualifiedRule() on "a, b { color: red }"
    //   => RQualified(prelude=["a", ",", " "b""], rules=[RDeclaration(...)])
    Rule Parser::ParseQualifiedRule(const ParseQualifiedRuleOpts& opts) {
        size_t prelude_start = index_;
        guchho::logger::Loc prelude_loc = Current().range.loc;

        for (;;) {
            switch (Current().kind) {
            case TokenType::kOpenBrace:
            case TokenType::kEndOfFile:
                goto loop_done;

            case TokenType::kCloseBrace:
                if (!opts.is_top_level) {
                    goto loop_done;
                }
                break;

            case TokenType::kSemicolon:
                if (opts.is_declaration_context) {
                    Rule rule;
                    rule.loc = prelude_loc;
                    auto data = std::make_shared<RBadDeclaration>();
                    data->tokens = ConvertTokens(std::vector<lexer::Token>(
                        tokens_.begin() + static_cast<ptrdiff_t>(prelude_start),
                        tokens_.begin() + static_cast<ptrdiff_t>(index_)));
                    rule.data = std::move(data);
                    return rule;
                }
                break;

            default:
                break;
            }

            ParseComponentValue();
        }

    loop_done:
        auto qualified = std::make_shared<RQualified>();
        qualified->prelude = ConvertTokens(std::vector<lexer::Token>(
            tokens_.begin() + static_cast<ptrdiff_t>(prelude_start),
            tokens_.begin() + static_cast<ptrdiff_t>(index_)));

        guchho::logger::Loc matching_loc = Current().range.loc;
        if (Eat(TokenType::kOpenBrace)) {
            qualified->rules = ParseListOfDeclarations(ListOfDeclarationsOpts{});
            guchho::logger::Loc close_brace_loc = Current().range.loc;
            if (ExpectWithMatchingLoc(TokenType::kCloseBrace, matching_loc)) {
                qualified->close_brace_loc = close_brace_loc;
            }
        } else if (!opts.is_already_invalid) {
            Expect(TokenType::kOpenBrace);
        }

        Rule rule;
        rule.loc = prelude_loc;
        rule.data = std::move(qualified);
        return rule;
    }

    // Scans forward from the current token position to determine where the
    // current rule ends, without actually parsing. Returns a pair of
    // (scan_result, position). The scan tracks balanced brackets/braces/
    // parentheses and stops at:
    //   - kSemicolon: a bare semicolon at the top level (declaration end)
    //   - kOpenBrace: an opening brace at the top level (rule block start)
    //   - kUnknown with position -1: end of tokens or unbalanced close
    //
    // This is used to distinguish between selectors and declarations when
    // the parser cannot determine the intent from the first token alone.
    //
    // Example: ScanForEndOfRule() at "color: red;" => (kSemicolon, index_of_semicolon)
    // Example: ScanForEndOfRule() at ".a { color: red }" => (kOpenBrace, index_of_brace)
    std::pair<EndOfRuleScan, int32_t> Parser::ScanForEndOfRule() {
        std::vector<TokenType> stack;
        int32_t absolute = static_cast<int32_t>(index_);

        for (size_t i = index_; i < tokens_.size(); ++i, ++absolute) {
            const lexer::Token& t = tokens_[i];
            switch (t.kind) {
            case TokenType::kSemicolon:
                if (stack.empty()) {
                    return {EndOfRuleScan::kSemicolon, absolute};
                }
                break;

            case TokenType::kFunction:
            case TokenType::kOpenParen:
                stack.push_back(TokenType::kCloseParen);
                break;

            case TokenType::kOpenBracket:
                stack.push_back(TokenType::kCloseBracket);
                break;

            case TokenType::kOpenBrace:
                if (stack.empty()) {
                    return {EndOfRuleScan::kOpenBrace, absolute};
                }
                stack.push_back(TokenType::kCloseBrace);
                break;

            case TokenType::kCloseParen:
            case TokenType::kCloseBracket:
                if (!stack.empty() && t.kind == stack.back()) {
                    stack.pop_back();
                }
                break;

            case TokenType::kCloseBrace:
                if (!stack.empty() && t.kind == stack.back()) {
                    stack.pop_back();
                } else {
                    return {EndOfRuleScan::kUnknown, -1};
                }
                break;

            default:
                break;
            }
        }

        return {EndOfRuleScan::kUnknown, -1};
    }

    // Parses a single CSS declaration: a property name followed by a colon and
    // a value. The property name must be an identifier token. The value is
    // consumed until a semicolon, closing brace, or end-of-file is reached.
    // Trailing !important is detected and stripped from the value.
    //
    // If the property name is not a recognized CSS property, a typo-correction
    // suggestion may be emitted. Unknown properties are still parsed as
    // RDeclaration with key = kDUnknown.
    //
    // Example: ParseDeclaration() on "color: red !important;"
    //   => RDeclaration(key=kDColor, value=[Token("red")], important=true)
    // Example: ParseDeclaration() on "colr: blue;"
    //   => RDeclaration(key=kDUnknown, value=[Token("blue")], important=false)
    Rule Parser::ParseDeclaration() {
        size_t key_start = index_;
        guchho::logger::Range key_range = tokens_[key_start].range;
        bool key_is_ident = Expect(TokenType::kIdent);
        bool ok = false;
        if (key_is_ident) {
            Eat(TokenType::kWhitespace);
            ok = Eat(TokenType::kColon);
        }

        size_t value_start = index_;
        for (;;) {
            switch (Current().kind) {
            case TokenType::kEndOfFile:
            case TokenType::kSemicolon:
            case TokenType::kCloseBrace:
                goto stop;

            default:
                ParseComponentValue();
            }
        }

    stop:
        if (!ok) {
            if (key_is_ident) {
                int32_t end = key_range.End();
                if (end > prev_error_.start) {
                    prev_error_.start = end;
                    guchho::logger::MsgData data = tracker_.MakeMsgData(guchho::logger::Range{guchho::logger::Loc{end}},
                                                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_ExpectedColon));
                    if (data.location) {
                        data.location->suggestion = ":";
                    }
                    guchho::logger::Msg msg;
                    msg.id = guchho::logger::MsgID::kCSS_CSSSyntaxError;
                    msg.kind = guchho::logger::MsgKind::kWarning;
                    msg.data = std::move(data);
                    log_->AddMsgID(msg.id, msg);
                }
            }

            Rule rule;
            rule.loc = key_range.loc;
            auto data = std::make_shared<RBadDeclaration>();
            data->tokens = ConvertTokens(std::vector<lexer::Token>(
                tokens_.begin() + static_cast<ptrdiff_t>(key_start),
                tokens_.begin() + static_cast<ptrdiff_t>(index_)));
            rule.data = std::move(data);
            return rule;
        }

        lexer::Token key_token = tokens_[key_start];
        std::string key_text = key_token.DecodedText(source_->contents);
        std::vector<lexer::Token> value(tokens_.begin() + static_cast<ptrdiff_t>(value_start),
                                        tokens_.begin() + static_cast<ptrdiff_t>(index_));
        bool verbatim_whitespace = key_text.rfind("--", 0) == 0;

        bool important = false;
        int i = static_cast<int>(value.size()) - 1;
        if (i >= 0 && value[static_cast<size_t>(i)].kind == TokenType::kWhitespace) {
            --i;
        }
        if (i >= 0 && value[static_cast<size_t>(i)].kind == TokenType::kIdent &&
            helpers::ToLowerASCII(value[static_cast<size_t>(i)].DecodedText(source_->contents)) == "important") {
            --i;
            if (i >= 0 && value[static_cast<size_t>(i)].kind == TokenType::kWhitespace) {
                --i;
            }
            if (i >= 0 && value[static_cast<size_t>(i)].kind == TokenType::kDelimExclamation) {
                value.resize(static_cast<size_t>(i));
                important = true;
            }
        }

        ConvertTokensOpts cto;
        cto.allow_imports = true;
        cto.verbatim_whitespace = verbatim_whitespace;
        std::vector<Token> result = ConvertTokensHelper(std::move(value), TokenType::kEndOfFile, cto).first;

        if (!verbatim_whitespace && !result.empty()) {
            if (options_.minify_whitespace) {
                result[0].whitespace =
                    ClearWhitespace(result[0].whitespace, static_cast<uint8_t>(WhitespaceFlags::kWhitespaceBefore));
            } else {
                result[0].whitespace = result[0].whitespace | WhitespaceFlags::kWhitespaceBefore;
            }
        }

        std::string lower_key_text = helpers::ToLowerASCII(key_text);
        Declarations key = kDUnknown;
        auto it = KnownDeclarations.find(lower_key_text);
        if (it != KnownDeclarations.end()) {
            key = it->second;
        }

        if (key == kDUnknown) {
            if (auto corrected = MaybeCorrectDeclarationTypo(lower_key_text)) {
                guchho::logger::MsgData data = tracker_.MakeMsgData(key_token.range,
                                                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_NotAKnownCSSProperty, key_text));
                if (data.location) {
                    data.location->suggestion = *corrected;
                }
                guchho::logger::Msg msg;
                msg.id = guchho::logger::MsgID::kCSS_UnsupportedCSSProperty;
                msg.kind = guchho::logger::MsgKind::kWarning;
                msg.data = std::move(data);
                guchho::logger::MsgData note;
                note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kCSS_DidYouMeanNote, *corrected);
                msg.notes.push_back(std::move(note));
                log_->AddMsgID(msg.id, msg);
            }
        }

        Rule rule;
        rule.loc = key_range.loc;
        auto data = std::make_shared<RDeclaration>();
        data->key = key;
        data->key_text = key_text;
        data->key_range = key_token.range;
        data->value = std::move(result);
        data->important = important;
        rule.data = std::move(data);
        return rule;
    }

    // Advances past a single CSS component value. This is a "consume any
    // value" operation used when the parser does not need to interpret the
    // value semantically. For function calls, parenthesized expressions,
    // block bodies, and bracketed sequences, this recursively consumes the
    // entire nested structure. For simple tokens (ident, number, etc.), it
    // advances past just that one token.
    void Parser::ParseComponentValue() {
        switch (Current().kind) {
        case TokenType::kFunction:
            ParseBlock(TokenType::kFunction, TokenType::kCloseParen);
            break;

        case TokenType::kOpenParen:
            ParseBlock(TokenType::kOpenParen, TokenType::kCloseParen);
            break;

        case TokenType::kOpenBrace:
            ParseBlock(TokenType::kOpenBrace, TokenType::kCloseBrace);
            break;

        case TokenType::kOpenBracket:
            ParseBlock(TokenType::kOpenBracket, TokenType::kCloseBracket);
            break;

        case TokenType::kEndOfFile:
            Unexpected();
            break;

        default:
            Advance();
            break;
        }
    }

    // Consumes a balanced block delimited by open/close token pairs. After
    // consuming the opening token, it recursively consumes component values
    // until the matching close token is found. If end-of-file is reached
    // before the close token, an "unbalanced" diagnostic is emitted pointing
    // back to the opening token.
    void Parser::ParseBlock(TokenType open, TokenType close) {
        lexer::Token current = Current();
        int32_t matching_start = current.range.End() - 1;
        if (Expect(open)) {
            while (!Eat(close)) {
                if (Peek(TokenType::kEndOfFile)) {
                    ExpectWithMatchingLoc(close, guchho::logger::Loc{matching_start});
                    return;
                }
                ParseComponentValue();
            }
        }
    }

    // Parses a comma-separated list of media queries until the stop predicate
    // returns true for the current token. Each query is parsed via
    // ParseMediaQuery(); if that fails, the tokens are collected as arbitrary
    // tokens. Commas separate queries; the list ends when a non-comma token
    // satisfies the stop condition.
    //
    // Example: ParseMediaQueryListUntil(is_semicolon) on "(min-width: 600px), print"
    //   => [MQPlain("(min-width: 600px)"), MQType(type="print")]
    std::vector<MediaQuery> Parser::ParseMediaQueryListUntil(const std::function<bool(TokenType)>& stop) {
        std::vector<MediaQuery> queries;
        Eat(TokenType::kWhitespace);
        while (!Peek(TokenType::kEndOfFile) && !stop(Current().kind)) {
            size_t start = index_;
            MediaQuery query;
            std::pair<MediaQuery, bool> parsed = ParseMediaQuery();
            if (!parsed.second) {
                index_ = start;
                guchho::logger::Loc loc = Current().range.loc;
                while (!Peek(TokenType::kEndOfFile) && !stop(Current().kind) && !Peek(TokenType::kComma)) {
                    ParseComponentValue();
                }
                std::vector<Token> tokens = ConvertTokens(std::vector<lexer::Token>(
                    tokens_.begin() + static_cast<ptrdiff_t>(start),
                    tokens_.begin() + static_cast<ptrdiff_t>(index_)));
                auto data = std::make_shared<MQArbitraryTokens>();
                data->tokens = std::move(tokens);
                query = MediaQuery{data, loc};
            } else {
                query = std::move(parsed.first);
            }
            queries.push_back(std::move(query));
            Eat(TokenType::kWhitespace);
            if (!Eat(TokenType::kComma)) {
                break;
            }
            Eat(TokenType::kWhitespace);
        }
        return queries;
    }

    // Parses a single media query. A media query can take two forms:
    //   1. A media type (e.g., "screen") optionally preceded by "not"/"only"
    //      and followed by "and <condition>"
    //   2. A bare media condition (e.g., "(min-width: 600px)")
    //
    // The function first checks if the current token looks like a media
    // condition (starts with "(" or "not ("), and if so, delegates to
    // ParseMediaCondition. Otherwise it parses the media type form.
    //
    // Example: ParseMediaQuery() on "screen and (min-width: 600px)"
    //   => MQType(type="screen", and=MQPlain("(min-width: 600px)"))
    // Example: ParseMediaQuery() on "not print"
    //   => MQType(op=not, type="print")
    std::pair<MediaQuery, bool> Parser::ParseMediaQuery() {
        guchho::logger::Loc loc = Current().range.loc;

        if (LooksLikeMediaCondition()) {
            return ParseMediaCondition(MediaOr::kWithOr);
        }

        std::string media_type = Decoded();
        if (!Peek(TokenType::kIdent)) {
            Expect(TokenType::kIdent);
            return {MediaQuery{}, false};
        }
        MQTypeOp op = MQTypeOp::kMQTypeOpNone;
        if (helpers::EqualFoldASCII(media_type, "not")) {
            op = MQTypeOp::kMQTypeOpNot;
        } else if (helpers::EqualFoldASCII(media_type, "only")) {
            op = MQTypeOp::kMQTypeOpOnly;
        }
        if (op != MQTypeOp::kMQTypeOpNone) {
            Advance();
            Eat(TokenType::kWhitespace);
            media_type = Decoded();
            if (!Peek(TokenType::kIdent)) {
                Expect(TokenType::kIdent);
                return {MediaQuery{}, false};
            }
        }

        if (helpers::EqualFoldASCII(media_type, "only") || helpers::EqualFoldASCII(media_type, "not") ||
            helpers::EqualFoldASCII(media_type, "and") || helpers::EqualFoldASCII(media_type, "or") ||
            helpers::EqualFoldASCII(media_type, "layer")) {
            Unexpected();
            return {MediaQuery{}, false};
        }
        Advance();
        Eat(TokenType::kWhitespace);

        MediaQuery and_or_null;
        if (Peek(TokenType::kIdent) && helpers::EqualFoldASCII(Decoded(), "and")) {
            Advance();
            Eat(TokenType::kWhitespace);
            auto [parsed, ok] = ParseMediaCondition(MediaOr::kWithoutOr);
            if (!ok) {
                return {MediaQuery{}, false};
            }
            and_or_null = std::move(parsed);
        }

        auto data = std::make_shared<MQType>();
        data->op = op;
        data->type = std::move(media_type);
        data->and_or_null = std::move(and_or_null);
        return {MediaQuery{data, loc}, true};
    }

    // Heuristic check that determines whether the current token sequence
    // looks like a media condition (parenthesized expression or function)
    // rather than a media type. This is needed because the CSS syntax for
    // media queries is ambiguous: "(min-width: 600px)" could be a condition
    // or a type name in parentheses.
    bool Parser::LooksLikeMediaCondition() {
        TokenType kind = Current().kind;
        return kind == TokenType::kOpenParen || kind == TokenType::kFunction ||
            (kind == TokenType::kIdent && helpers::EqualFoldASCII(Decoded(), "not") && Next().kind == TokenType::kWhitespace &&
                At(index_ + 2).kind == TokenType::kOpenParen);
    }

    // Parses a media condition, which can be:
    //   - A leading "not" followed by a media-in-parens
    //   - A single media-in-parens term
    //   - Multiple terms joined by "and" or "or" operators
    //
    // The or_ parameter controls whether "or" operators are permitted at this
    // nesting level (only at the top level of a media query).
    //
    // Example: ParseMediaCondition(kWithOr) on "(min-width: 600px) and (color)"
    //   => MQBinary(op=and, terms=[MQPlain("min-width"), MQPlain("color")])
    // Example: ParseMediaCondition(kWithOr) on "not (print)"
    //   => MQNot(inner=MQPlain("print"))
    std::pair<MediaQuery, bool> Parser::ParseMediaCondition(MediaOr or_) {
        guchho::logger::Loc loc = Current().range.loc;

        if (Peek(TokenType::kIdent) && helpers::EqualFoldASCII(Decoded(), "not")) {
            Advance();
            Eat(TokenType::kWhitespace);
            auto [inner, ok] = ParseMediaInParens();
            if (!ok) {
                return {MediaQuery{}, false};
            }
            return {MaybeSimplifyMediaNot(loc, std::move(inner), options_.minify_syntax), true};
        }

        auto [first, first_ok] = ParseMediaInParens();
        if (!first_ok) {
            return {MediaQuery{}, false};
        }
        Eat(TokenType::kWhitespace);

        if (Peek(TokenType::kIdent)) {
            std::string keyword = Decoded();
            if (helpers::EqualFoldASCII(keyword, "and") || (or_ == MediaOr::kWithOr && helpers::EqualFoldASCII(keyword, "or"))) {
                MQBinaryOp op = MQBinaryOp::kMQBinaryOpAnd;
                if (keyword.size() == 2) {
                    op = MQBinaryOp::kMQBinaryOpOr;
                }
                std::vector<MediaQuery> inner = AppendMediaTerm({}, first, op, options_.minify_syntax);
                for (;;) {
                    Advance();
                    Eat(TokenType::kWhitespace);
                    auto [next, next_ok] = ParseMediaInParens();
                    if (!next_ok) {
                        return {MediaQuery{}, false};
                    }
                    inner = AppendMediaTerm(std::move(inner), next, op, options_.minify_syntax);
                    Eat(TokenType::kWhitespace);
                    if (!Peek(TokenType::kIdent) || !helpers::EqualFoldASCII(Decoded(), keyword)) {
                        break;
                    }
                }
                auto data = std::make_shared<MQBinary>();
                data->op = op;
                data->terms = std::move(inner);
                return {MediaQuery{data, loc}, true};
            }
        }

        return {std::move(first), true};
    }

    // Parses a "media in parens" production: a parenthesized expression that
    // may contain either a nested media condition (recursive) or a feature
    // query. If the tokens inside the parentheses match a known media feature
    // pattern (boolean, plain, or range syntax), a structured AST node is
    // produced. Otherwise, the tokens are captured as arbitrary token sequences.
    //
    // Range media features are lowered to legacy syntax when the target
    // environment does not support the range syntax.
    //
    // Example: ParseMediaInParens() on "(min-width: 600px)"
    //   => MQPlain(name="min-width", op=">=", value="600px")
    // Example: ParseMediaInParens() on "(not (color))"
    //   => MQNot(inner=MQPlain("color"))
    std::pair<MediaQuery, bool> Parser::ParseMediaInParens() {
        Eat(TokenType::kWhitespace);
        size_t start = index_;

        bool is_function = Eat(TokenType::kFunction);
        if (!is_function && !Expect(TokenType::kOpenParen)) {
            return {MediaQuery{}, false};
        }
        Eat(TokenType::kWhitespace);

        if (!is_function && LooksLikeMediaCondition()) {
            auto [inner, ok] = ParseMediaCondition(MediaOr::kWithOr);
            if (!ok) {
                return {MediaQuery{}, false};
            }
            Eat(TokenType::kWhitespace);
            if (!Expect(TokenType::kCloseParen)) {
                return {MediaQuery{}, false};
            }
            return {std::move(inner), true};
        }

        while (!Peek(TokenType::kCloseParen) && !Peek(TokenType::kEndOfFile)) {
            ParseComponentValue();
        }
        size_t end = index_;
        if (!Expect(TokenType::kCloseParen)) {
            return {MediaQuery{}, false};
        }
        std::vector<Token> tokens = ConvertTokens(std::vector<lexer::Token>(
            tokens_.begin() + static_cast<ptrdiff_t>(start),
            tokens_.begin() + static_cast<ptrdiff_t>(end)));
        guchho::logger::Loc loc = tokens[0].loc;

        if (!is_function && tokens.size() == 1) {
            if (tokens[0].children != nullptr) {
                if (std::shared_ptr<MQPlainOrBoolean> term = ParsePlainOrBooleanMediaFeature(*tokens[0].children); term) {
                    return {MediaQuery{term, loc}, true};
                }
                if (std::shared_ptr<MQRange> term = ParseRangeMediaFeature(*tokens[0].children); term) {
                    if (guchho::compat::Has(options_.unsupported_css_features, guchho::compat::CSSFeature::kMediaRange)) {
                        std::vector<MediaQuery> terms;
                        if (term->before_cmp != MQCmp::kMQCmpNone) {
                            terms.push_back(
                                LowerMediaRange(term->name_loc, term->name, Reverse(term->before_cmp), term->before));
                        }
                        if (term->after_cmp != MQCmp::kMQCmpNone) {
                            terms.push_back(LowerMediaRange(term->name_loc, term->name, term->after_cmp, term->after));
                        }
                        if (terms.size() == 1) {
                            return {std::move(terms[0]), true};
                        }
                        auto data = std::make_shared<MQBinary>();
                        data->op = MQBinaryOp::kMQBinaryOpAnd;
                        data->terms = std::move(terms);
                        return {MediaQuery{data, loc}, true};
                    }
                    return {MediaQuery{term, loc}, true};
                }
            }
        }
        auto data = std::make_shared<MQArbitraryTokens>();
        data->tokens = std::move(tokens);
        return {MediaQuery{data, loc}, true};
    }

} 
