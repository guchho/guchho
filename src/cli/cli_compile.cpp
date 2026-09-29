// The compiler half of the service: lex, parse, transform and print, one
// command per stage per language.
//
// This is the same engine the build runs. Every handler below calls the same
// lexer, the same parser and the same printer that a bundle goes through, and
// the only thing that is different is that the result travels back as a value
// rather than into an output file. That is the whole design: a compile command
// that reimplemented any of it would be a second answer to a question the
// engine already answers, and the two would disagree about what "the same parse"
// means before long.
//
// What makes it possible to do this without a build around it is that the
// three pipelines are usable on their own. A source is a Source, a parse is a
// Parse, a print is a Print. What a build adds is the module graph, the resolver
// and the linker, and a standalone compile of one file has no use for any of
// those three.
//
// The AST does not cross the wire, and this is the most important decision in
// the file. None of the three trees is a value: they are graphs of engine-owned
// nodes with interned symbols, scopes pointing at scopes, and pointers into
// shared tables. Serializing one would mean writing a serializer for each —
// three more implementations of three more things, in a fourth language, to be
// kept in step with a parser that is still being written. Instead a parse stores
// its tree here, under a number, and answers with that number plus a structural
// summary produced by walking the engine's own tree. So a caller gets to ask
// what the parser made of the source — which nodes, which rules, which symbols
// — without this package containing a parser.
//
// The registry is a plain map and needs no lock, for the reason the context
// registry does not either: the service is single-threaded between reading one
// request and reading the next, so a tree is only ever touched from the handler
// that owns the request.

#include "guchho/cli.hpp"
#include "guchho/service.hpp"

#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/logger.hpp"

#include "guchho/css/css_lexer.hpp"
#include "guchho/css/css_parser.hpp"
#include "guchho/css/css_printer.hpp"

#include "guchho/html/html_ast.hpp"
#include "guchho/html/html_bridge.hpp"
#include "guchho/html/html_lexer.hpp"
#include "guchho/html/html_printer.hpp"

#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/javascript/js_printer.hpp"
#include "guchho/javascript/js_renamer.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace guchho::cli {

    namespace {

        // =========================================================================
        // Shared helpers
        // =========================================================================

        // A response being built one key at a time.
        //
        // The protocol's object is a list of pairs kept in the order they were
        // written, and a response here is nearly all built after its first key,
        // so it is written as a list and handed over in one piece at the end.
        // A local type rather than a protocol one because nothing outside this
        // file ever holds half a response: the value either exists or does not.
        class Object {
            public:
                void Set(std::string key, service::Value value) {
                    entries_.emplace_back(std::move(key), std::move(value));
                }

                // Leaves the builder empty. Every response is built once, at the
                // end of the handler that assembled it, and nothing reads a
                // response twice.
                service::Value Build() { return service::Value::Object(std::move(entries_)); }

            private:
                std::vector<std::pair<std::string, service::Value>> entries_;
        };

        // A diagnostic for something the host asked for that this could not do,
        // carrying the reason in its own words. The same shape the build handlers
        // use, so a host has one place to read problems from whichever command
        // produced them.
        service::Value CompileError(std::string id, std::string text) {
            api::Message message;
            message.id = std::move(id);
            message.text = std::move(text);
            return service::Value::Object({
                {"error", service::Value::String(message.text)},
                {"errors", service::Value::Array({service::MessageToValue(message)})},
            });
        }

        // The name a source file is given when the host did not say. "<stdin>"
        // rather than a path, because there is no file: it is what a diagnostic
        // should say about source that arrived in a request instead of off disk,
        // and it is the name the transform path already uses for the same case.
        constexpr std::string_view kDefaultName = "<stdin>";

        // A source built from text the host sent.
        //
        // The fields a Source carries are set explicitly rather than left
        // default, because two of them are load-bearing. "key_path" is what the
        // caches key on and what a diagnostic names; "pretty_paths" is what a
        // message renders as its file, and a source with no pretty paths has
        // messages that cannot say where they are from. "index" is 0 because a
        // standalone compile is always the first and only source, which is also
        // what the symbol map in the print path is sized for.
        logger::Source MakeSource(std::string contents, std::string name) {
            logger::Source source;
            source.index = 0;
            source.identifier_name = name;
            source.pretty_paths = logger::PrettyPaths{name, name};
            source.key_path = logger::Path{name};
            source.contents = std::move(contents);
            return source;
        }

        // The source text a request carried.
        //
        // A field rather than a flag, for the reason the transform command reads
        // its input as a value: a megabyte of source does not fit in an
        // argument in any way worth relying on. Bytes are accepted as well as
        // text because a host reading a file off disk already has bytes, and
        // decoding them here would be a copy it did not ask for and could have
        // got wrong.
        //
        // The field has to be there but may be empty, because lexing the empty
        // string is a real operation that returns one end-of-file token, where
        // a request with no input at all is a host that forgot to send one.
        std::optional<std::string> SourceTextFrom(const service::Value& request) {
            const service::Value* text = request.Find("input");
            if (text == nullptr) return std::nullopt;
            if (text->IsString()) return text->AsString();
            if (text->IsBytes()) {
                const std::vector<uint8_t>& bytes = text->AsBytes();
                return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            return std::nullopt;
        }

        // The name a diagnostic should blame, when the host gave one.
        std::string SourceNameFrom(const service::Value& request) {
            if (const service::Value* name = request.Find("sourcefile")) {
                if (name->IsString() && !name->AsString().empty()) return name->AsString();
            }
            return std::string(kDefaultName);
        }

        // A boolean option, defaulting when the host did not say.
        bool BoolFrom(const service::Value& request, std::string_view key, bool fallback) {
            if (const service::Value* value = request.Find(key)) {
                if (value->IsBool()) return value->AsBool();
            }
            return fallback;
        }

        // The engine's own messages, split into the two lists a host reads.
        //
        // A log built for a compile collects rather than prints: this is a
        // request from a program, so the answer travels back in the response and
        // nothing goes to stderr. Filtering by kind is what makes one list of
        // collected messages into "errors" and "warnings", and it is the same
        // split the build path makes over the same log.
        void SplitMessages(const std::vector<logger::Msg>& msgs,
                           std::vector<api::Message>& errors,
                           std::vector<api::Message>& warnings) {
            // A location is copied field by field rather than converted by the
            // shared LocationToValue helper, because that helper is the wrong
            // direction: it produces a wire value, and this needs the api type
            // that MessagesToValue knows how to serialize. The one file involved
            // is the one the host named, so its relative form is that name.
            auto take_location = [](const logger::MsgLocation* loc) -> std::optional<api::Location> {
                if (loc == nullptr) return std::nullopt;
                return api::Location{
                    loc->file.rel,
                    loc->namespace_,
                    loc->line,
                    loc->column,
                    loc->length,
                    loc->line_text,
                    loc->suggestion,
                };
            };

            for (const logger::Msg& msg : msgs) {
                if (msg.kind != logger::MsgKind::kError && msg.kind != logger::MsgKind::kWarning) {
                    continue;
                }

                api::Message message;
                message.id = std::string(logger::MsgIDToString(msg.id));
                message.plugin_name = msg.plugin_name;
                message.text = msg.data.text;
                message.location = take_location(msg.data.location.get());
                for (const logger::MsgData& note : msg.notes) {
                    message.notes.push_back(api::Note{note.text, take_location(note.location.get())});
                }
                if (msg.data.user_detail != nullptr) {
                    message.detail = *static_cast<std::any*>(msg.data.user_detail);
                }

                if (msg.kind == logger::MsgKind::kError) {
                    errors.push_back(std::move(message));
                } else {
                    warnings.push_back(std::move(message));
                }
            }
        }

        // A collecting log and the messages it ended up with.
        //
        // NewDeferLog rather than a hand-built Log: the Log struct's callbacks
        // are std::function members with no initializers, so a default-constructed
        // one is a set of empty function targets and calling one is undefined
        // behaviour. The factory is the supported way to get a log that keeps
        // messages instead of printing them. It is passed by value into the
        // engines that take a log that way, and the copy shares the one buffer,
        // which is why done() below still sees what they logged.
        logger::Log NewCollectingLog() {
            return logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
        }

        // The response tail every compile command ends with: the two diagnostic
        // lists, and nothing else. Every one of the twelve commands answers in
        // this shape plus its own field, so a host reads failures the same way
        // whichever stage produced them.
        void AppendDiagnostics(Object& out, const std::vector<logger::Msg>& msgs) {
            std::vector<api::Message> errors;
            std::vector<api::Message> warnings;
            SplitMessages(msgs, errors, warnings);
            out.Set("errors", service::Value::Array(service::MessagesToValue(errors)));
            out.Set("warnings", service::Value::Array(service::MessagesToValue(warnings)));
        }

        // A count as the protocol's number type.
        //
        // Every number that crosses the wire is a signed 32-bit integer, and the
        // counts here are a size_t. Clamping rather than narrowing is the point
        // of this function: a tree with more than two billion nodes is not
        // something this will see, and a silently wrapped count would report a
        // tree that is a fraction of the one that was parsed.
        int32_t ToWireCount(size_t count) {
            constexpr size_t kMax = static_cast<size_t>(INT32_MAX);
            return count > kMax ? INT32_MAX : static_cast<int32_t>(count);
        }

        // A byte offset as the protocol's number type, for the same reason.
        int32_t ToWireOffset(size_t offset) { return ToWireCount(offset); }

        // =========================================================================
        // The AST registry
        // =========================================================================

        // The three trees, held in one place.
        //
        // A variant rather than three parallel maps so that a handle cannot name
        // a CSS tree where an HTML tree was expected: the language a handle
        // belongs to is decided by what it holds, and asking for a tree of the
        // wrong kind is a miss rather than a reinterpretation.
        using StoredAst = std::variant<html::AST, css::AST, javascript::AST>;

        struct AstEntry {
            StoredAst ast;
            std::string language;

            // The name the source was parsed under, kept so that a print request
            // need not carry it back. A tree without its name could still be
            // printed — the printer works from the tree — but a diagnostic
            // raised while printing has nowhere to point without one, and a
            // caller should not have to resend what it already sent.
            std::string source_name;
        };

        // Trees by number, for the lifetime of the service.
        //
        // Handles are never reused and never freed within a run. Reusing a
        // number would make a stale handle silently address a different tree,
        // which is the one failure mode a handle exists to prevent; the cost of
        // not reusing them is bounded by the number of parses a service makes,
        // and a service process is short lived next to the trees a long build
        // would hold.
        class AstRegistry {
            public:
                static AstRegistry& Instance() {
                    static AstRegistry registry;
                    return registry;
                }

                int32_t Put(AstEntry entry) {
                    const int32_t key = next_key_++;
                    entries_.emplace(key, std::move(entry));
                    return key;
                }

                AstEntry* Get(int32_t key) {
                    const auto found = entries_.find(key);
                    if (found == entries_.end()) return nullptr;
                    return &found->second;
                }

                // Removes the entry and hands it over. A transform consumes the
                // handle it was given and returns a new one, so the old tree has
                // to leave the map rather than be read after being moved out of.
                std::optional<AstEntry> Take(int32_t key) {
                    const auto found = entries_.find(key);
                    if (found == entries_.end()) return std::nullopt;
                    AstEntry entry = std::move(found->second);
                    entries_.erase(found);
                    return entry;
                }

            private:
                std::unordered_map<int32_t, AstEntry> entries_;
                int32_t next_key_ = 0;
        };

        // The handle a request is talking about, and whether it named one.
        bool AstKeyFrom(const service::Value& request, int32_t& key) {
            const service::Value* value = request.Find("ast");
            if (value == nullptr || !value->IsNumber()) return false;
            key = value->AsNumber();
            return true;
        }

        // The complaint for a handle that names nothing, or names a tree of the
        // wrong language. Both are the same sentence to a caller: it passed an id
        // this service does not have, or never had.
        service::Value UnknownAstResponse() {
            return CompileError("unknown-ast",
                "that AST has been released, or was never created");
        }

        // =========================================================================
        // Line and column
        // =========================================================================

        // Byte offset to 1-based line and 0-based column.
        //
        // A token carries its offset and the caller wants a position a text
        // editor can jump to, and the engines hand over only the first: CSS
        // tokens carry a Range, JavaScript tokens a Loc. One scan over the
        // source turns either of them into a line and a column, so that what a
        // token says and what a diagnostic says about the same offset cannot
        // disagree.
        struct Position {
            int32_t line = 1;
            int32_t column = 0;
        };

        // Counts the line breaks once, so that every token's position is a
        // binary search rather than a rescan. A stylesheet with ten thousand
        // tokens and a thousand lines would otherwise do ten thousand scans of
        // the same text.
        class LineIndex {
            public:
                explicit LineIndex(const std::string& contents) {
                    starts_.push_back(0);
                    for (size_t i = 0; i < contents.size(); i++) {
                        if (contents[i] == '\n') starts_.push_back(ToWireOffset(i + 1));
                    }
                }

                // The line containing "offset", counting from 1, and the column
                // within it from 0.
                Position At(size_t offset) const {
                    if (starts_.empty()) return Position{};
                    const int32_t at = ToWireOffset(offset);
                    // upper_bound on the first start past the offset gives the
                    // line it falls in: the last start that is <= offset.
                    const auto found = std::upper_bound(starts_.begin(), starts_.end(), at);
                    const size_t line = found == starts_.begin()
                        ? 0
                        : static_cast<size_t>((found - starts_.begin()) - 1);
                    return Position{
                        static_cast<int32_t>(line + 1),
                        at - starts_[line],
                    };
                }

            private:
                std::vector<int32_t> starts_;
        };

        // =========================================================================
        // HTML
        // =========================================================================

        // The HTML tokenizer's token handler, collecting what the host asked for.
        //
        // The tokenizer is push-based and takes a handler, so a handler is the
        // only way to see its output. This one records each token as a value
        // rather than building a tree, because lexing and parsing are separate
        // commands and the lexer's job is to answer "what tokens are in this",
        // not to build something the parser would have to be told not to use.
        //
        // Locations are requested, so every token can say where it came from.
        class HtmlTokenCollector final : public html::TokenHandler {
            public:
                explicit HtmlTokenCollector(const LineIndex& lines) : lines_(&lines) {}

                const std::vector<service::Value>& tokens() const { return tokens_; }

                void OnComment(html::CommentToken& token) override {
                    Add("comment", token.data, token.location);
                }

                void OnDoctype(html::DoctypeToken& token) override {
                    Add("doctype", token.name, token.location);
                }

                void OnStartTag(html::TagToken& token) override {
                    Add("start-tag", token.tag_name, token.location);
                }

                void OnEndTag(html::TagToken& token) override {
                    Add("end-tag", token.tag_name, token.location);
                }

                void OnEof(html::EofToken& token) override {
                    Add("eof", "", token.location);
                }

                void OnCharacter(html::CharacterToken& token) override {
                    Add("character", token.chars, token.location);
                }

                void OnNullCharacter(html::CharacterToken& token) override {
                    Add("null-character", token.chars, token.location);
                }

                void OnWhitespaceCharacter(html::CharacterToken& token) override {
                    Add("whitespace-character", token.chars, token.location);
                }

            private:
                // One token, as the value the host reads.
                //
                // The tag name goes in "value" and the attributes do not travel:
                // an attribute is a name and a value, and a start tag with a
                // hundred data-* attributes would otherwise be one enormous
                // value. The count is here so a caller can tell a bare tag from
                // one carrying attributes, and parseHTML reads them in full.
                void Add(std::string kind, std::string value, const html::Location* location) {
                    Object token;
                    token.Set("kind", service::Value::String(std::move(kind)));
                    token.Set("value", service::Value::String(std::move(value)));

                    if (location != nullptr) {
                        const Position position = lines_->At(static_cast<size_t>(location->start_offset));
                        token.Set("start", service::Value::Number(location->start_offset));
                        token.Set("end", service::Value::Number(location->end_offset));
                        token.Set("line", service::Value::Number(position.line));
                        token.Set("column", service::Value::Number(position.column));
                        token.Set("length", service::Value::Number(
                            location->end_offset - location->start_offset));
                    } else {
                        // A token with no location is not an error, it is a
                        // tokenizer running without location tracking. The
                        // fields are zero rather than absent so a caller can
                        // read them without checking, which is the same rule the
                        // published message shape follows.
                        token.Set("start", service::Value::Number(0));
                        token.Set("end", service::Value::Number(0));
                        token.Set("line", service::Value::Number(0));
                        token.Set("column", service::Value::Number(0));
                        token.Set("length", service::Value::Number(0));
                    }

                    tokens_.push_back(std::move(token).Build());
                }

                const LineIndex* lines_;
                std::vector<service::Value> tokens_;
        };

        service::Value HandleLexHTML(const service::Value& request) {
            const std::optional<std::string> text = SourceTextFrom(request);
            if (!text.has_value()) {
                return CompileError("invalid-compile-request",
                    "lexHTML needs its input as a string or as bytes");
            }

            // The tokenizer works in UTF-16 and the wire carries UTF-8, so the
            // conversion happens here rather than anywhere else in the file: it
            // is the one place a source changes representation on its way in.
            const std::u16string wide = html::ToUtf16(*text);

            const LineIndex lines(*text);
            HtmlTokenCollector collector(lines);
            html::Tokenizer tokenizer(collector, true);
            tokenizer.Write(wide, true);

            const size_t count = collector.tokens().size();

            Object out;
            out.Set("tokens", service::Value::Array(collector.tokens()));
            out.Set("count", service::Value::Number(ToWireCount(count)));
            // The tokenizer takes no log, so there is nothing to split; the two
            // lists are still written, so a host reads every command the same
            // way.
            AppendDiagnostics(out, {});
            return out.Build();
        }

        // A node's summary, as the value a caller reads it as.
        //
        // An element carries its tag name and its attribute names. Not their
        // values: a summary is the shape of the tree, and an attribute value can
        // be the entire body of a data island. A caller wanting values reads the
        // printed form, which is where values belong.
        void AppendHtmlNode(const html::Node& node, Object& out, int32_t depth) {
            out.Set("type", service::Value::String(node.node_name));
            out.Set("depth", service::Value::Number(depth));

            if (html::IsElementNode(node)) {
                out.Set("tag", service::Value::String(node.tag_name));
                std::vector<service::Value> attrs;
                attrs.reserve(node.attrs.size());
                for (const html::Attribute& attr : node.attrs) {
                    attrs.push_back(service::Value::String(attr.name));
                }
                out.Set("attributes", service::Value::Array(std::move(attrs)));
            }
        }

        // The whole tree, flattened into a list of nodes with a depth each.
        //
        // Flat rather than nested because the wire has no cycles and a tree has
        // one — a node points at its parent — so a faithful nested encoding would
        // need a reference scheme, and a caller reading a summary wants a list
        // it can walk once. "depth" is what makes the flat list a tree: it is
        // enough to reconstruct the shape, and it is the order the nodes are in.
        void WalkHtmlTree(const html::Node& node, int32_t depth,
                          std::vector<service::Value>& out) {
            Object entry;
            AppendHtmlNode(node, entry, depth);
            out.push_back(std::move(entry).Build());

            for (const std::unique_ptr<html::Node>& child : node.child_nodes) {
                if (child != nullptr) WalkHtmlTree(*child, depth + 1, out);
            }
            // A <template>'s content is a separate tree owned by the element
            // rather than one of its children, so it is walked as a child here
            // or it would not appear at all.
            if (node.template_content != nullptr) {
                WalkHtmlTree(*node.template_content, depth + 1, out);
            }
        }

        service::Value HandleParseHTML(const service::Value& request) {
            const std::optional<std::string> text = SourceTextFrom(request);
            if (!text.has_value()) {
                return CompileError("invalid-compile-request",
                    "parseHTML needs its input as a string or as bytes");
            }

            logger::Log log = NewCollectingLog();
            html::BridgeOptions options;
            options.parse_fragment = BoolFrom(request, "fragment", false);
            // Import records and inline code are what a caller inspecting a tree
            // wants to see, and collecting them costs one pass over the elements
            // the parser has already built.
            options.collect_import_records = BoolFrom(request, "collectImportRecords", true);
            options.collect_inline_code = BoolFrom(request, "collectInlineCode", true);

            const std::string name = SourceNameFrom(request);
            logger::Source source = MakeSource(*text, name);

            html::AST ast = html::Parse(log, source, options);
            const std::vector<logger::Msg> msgs = log.done();

            std::vector<service::Value> nodes;
            if (ast.node != nullptr) {
                WalkHtmlTree(*ast.node, 0, nodes);
            }

            const size_t node_count = nodes.size();
            const size_t import_count = ast.import_records.size();
            const size_t script_count = ast.inline_scripts.size();
            const size_t style_count = ast.inline_styles.size();

            AstEntry entry;
            entry.ast = std::move(ast);
            entry.language = "html";
            entry.source_name = name;
            const int32_t id = AstRegistry::Instance().Put(std::move(entry));

            Object out;
            out.Set("id", service::Value::Number(id));
            out.Set("nodes", service::Value::Array(std::move(nodes)));
            out.Set("nodeCount", service::Value::Number(ToWireCount(node_count)));
            out.Set("importRecords", service::Value::Number(ToWireCount(import_count)));
            out.Set("inlineScripts", service::Value::Number(ToWireCount(script_count)));
            out.Set("inlineStyles", service::Value::Number(ToWireCount(style_count)));
            AppendDiagnostics(out, msgs);
            return out.Build();
        }

        service::Value HandlePrintHTML(const service::Value& request) {
            int32_t key = 0;
            if (!AstKeyFrom(request, key)) {
                return CompileError("invalid-compile-request",
                    "printHTML needs the id of an HTML tree from parseHTML");
            }

            AstEntry* entry = AstRegistry::Instance().Get(key);
            if (entry == nullptr || !std::holds_alternative<html::AST>(entry->ast)) {
                return UnknownAstResponse();
            }

            html::AST& ast = std::get<html::AST>(entry->ast);
            if (ast.node == nullptr) {
                return CompileError("empty-ast", "that HTML tree has no document node to print");
            }

            html::PrinterOptions options;
            options.pretty_print = BoolFrom(request, "pretty", false);
            options.minify = BoolFrom(request, "minify", false);
            // Scripting is on by default in the printer and matches what a
            // browser does; a host parsing a fragment for a non-scripting
            // consumer can turn it off, and the noscript content is the only
            // thing it changes.
            options.scripting_enabled = BoolFrom(request, "scriptingEnabled", true);

            // Print rather than PrintOuter, for a reason that is about what a
            // parse answers with. Its root is always a document or a fragment,
            // and neither has tags of its own: the printer writes the children
            // of either, which for a document is the whole document and for a
            // fragment is the fragment. There is no root a print-html request
            // can reach that has a tag to write "outer", so there is no outer
            // option to offer.
            const std::string code = html::Print(*ast.node, options);

            Object out;
            out.Set("code", service::Value::String(code));
            return out.Build();
        }

        // =========================================================================
        // CSS
        // =========================================================================

        // The name a CSS token kind is known by.
        //
        // The engine has a table for this, and it is not the one to use: those
        // are the words a CSS diagnostics message is written in — kIdent is
        // "identifier" there, and kSymbol is "identifier" as well, which is a
        // copy and paste in that table. A host reading a token stream wants the
        // name of the kind it is, the same one a caller would write in
        // JavaScript, and the enum already spells those out. So the names are
        // taken from the enum and the engine's table is left to the diagnostics
        // it was written for.
        const char* CssTokenKindName(css::TokenType kind) {
            switch (kind) {
                case css::TokenType::kEndOfFile: return "endOfFile";
                case css::TokenType::kAtKeyword: return "atKeyword";
                case css::TokenType::kUnterminatedString: return "unterminatedString";
                case css::TokenType::kBadUrl: return "badUrl";
                case css::TokenType::kCdc: return "cdc";
                case css::TokenType::kCdo: return "cdo";
                case css::TokenType::kCloseBrace: return "closeBrace";
                case css::TokenType::kCloseBracket: return "closeBracket";
                case css::TokenType::kCloseParen: return "closeParen";
                case css::TokenType::kColon: return "colon";
                case css::TokenType::kComma: return "comma";
                case css::TokenType::kDelim: return "delim";
                case css::TokenType::kDelimAmpersand: return "ampersand";
                case css::TokenType::kDelimAsterisk: return "asterisk";
                case css::TokenType::kDelimBar: return "bar";
                case css::TokenType::kDelimCaret: return "caret";
                case css::TokenType::kDelimDollar: return "dollar";
                case css::TokenType::kDelimDot: return "dot";
                case css::TokenType::kDelimEquals: return "equals";
                case css::TokenType::kDelimExclamation: return "exclamation";
                case css::TokenType::kDelimGreaterThan: return "greaterThan";
                case css::TokenType::kDelimLessThan: return "lessThan";
                case css::TokenType::kDelimMinus: return "minus";
                case css::TokenType::kDelimPlus: return "plus";
                case css::TokenType::kDelimSlash: return "slash";
                case css::TokenType::kDelimTilde: return "tilde";
                case css::TokenType::kDimension: return "dimension";
                case css::TokenType::kFunction: return "function";
                case css::TokenType::kHash: return "hash";
                case css::TokenType::kIdent: return "ident";
                case css::TokenType::kNumber: return "number";
                case css::TokenType::kOpenBrace: return "openBrace";
                case css::TokenType::kOpenBracket: return "openBracket";
                case css::TokenType::kOpenParen: return "openParen";
                case css::TokenType::kPercentage: return "percentage";
                case css::TokenType::kSemicolon: return "semicolon";
                case css::TokenType::kString: return "string";
                case css::TokenType::kUrl: return "url";
                case css::TokenType::kWhitespace: return "whitespace";
                case css::TokenType::kSymbol: return "symbol";
            }
            return "unknown";
        }

        service::Value HandleLexCSS(const service::Value& request) {
            const std::optional<std::string> text = SourceTextFrom(request);
            if (!text.has_value()) {
                return CompileError("invalid-compile-request",
                    "lexCSS needs its input as a string or as bytes");
            }

            logger::Log log = NewCollectingLog();
            const std::string name = SourceNameFrom(request);
            const logger::Source source = MakeSource(*text, name);

            css::lexer::Options options;
            // Comments are not tokens in this lexer: the token type table has no
            // comment kind, and a comment the lexer keeps is kept in its own
            // list of ranges rather than in the stream. So a caller asking what
            // the lexer saw gets the stream and the comments separately, and the
            // two together are the source. "includeComments" is whether the
            // comment list is populated; turning it off is also cheaper, which
            // is the only reason a caller would.
            options.record_all_comments = BoolFrom(request, "includeComments", true);
            const bool want_comments = options.record_all_comments;

            const css::lexer::TokenizeResult result = css::lexer::Tokenize(log, source, options);
            const std::vector<logger::Msg> msgs = log.done();

            const LineIndex lines(*text);
            std::vector<service::Value> tokens;
            tokens.reserve(result.tokens.size());
            for (const css::lexer::Token& token : result.tokens) {
                // DecodedText rather than the raw slice: an identifier written
                // "\2D x" is an identifier named "-x", and a caller reading a
                // token stream wants the name, not the escape that spelled it.
                const std::string value = token.DecodedText(source.contents);

                const size_t start = static_cast<size_t>(token.range.loc.start);
                const size_t length = static_cast<size_t>(token.range.len);
                const Position position = lines.At(start);

                Object entry;
                entry.Set("kind", service::Value::String(CssTokenKindName(token.kind)));
                entry.Set("value", service::Value::String(value));
                entry.Set("start", service::Value::Number(ToWireOffset(start)));
                entry.Set("end", service::Value::Number(ToWireOffset(start + length)));
                entry.Set("line", service::Value::Number(position.line));
                entry.Set("column", service::Value::Number(position.column));
                entry.Set("length", service::Value::Number(ToWireOffset(length)));
                tokens.push_back(std::move(entry).Build());
            }

            // The comments the lexer kept, in source order, each with the text
            // and the range it covered. No "kind": every entry is a comment.
            std::vector<service::Value> comments;
            if (want_comments) {
                comments.reserve(result.all_comments.size());
                for (const logger::Range& range : result.all_comments) {
                    const size_t start = static_cast<size_t>(range.loc.start);
                    const size_t length = static_cast<size_t>(range.len);
                    const Position position = lines.At(start);

                    Object entry;
                    entry.Set("text", service::Value::String(
                        source.contents.substr(start, length)));
                    entry.Set("start", service::Value::Number(ToWireOffset(start)));
                    entry.Set("end", service::Value::Number(ToWireOffset(start + length)));
                    entry.Set("line", service::Value::Number(position.line));
                    entry.Set("column", service::Value::Number(position.column));
                    comments.push_back(std::move(entry).Build());
                }
            }

            const size_t count = result.tokens.size();

            Object out;
            out.Set("tokens", service::Value::Array(std::move(tokens)));
            out.Set("comments", service::Value::Array(std::move(comments)));
            out.Set("count", service::Value::Number(ToWireCount(count)));
            AppendDiagnostics(out, msgs);
            return out.Build();
        }

        // The parser options a request asked for.
        //
        // Minification is read here and passed to the parser rather than applied
        // after it, because in CSS it is a parsing decision: the parser has to
        // know a dimension is a dimension in order to mangle it, and a
        // stylesheet parsed for a minifying print is a different tree from the
        // same stylesheet parsed for a readable one.
        css::ParserOptions CssParseOptionsFrom(const service::Value& request) {
            css::ParserOptions options;
            options.minify_whitespace = BoolFrom(request, "minifyWhitespace", false);
            options.minify_syntax = BoolFrom(request, "minifySyntax", false);
            options.minify_identifiers = BoolFrom(request, "minifyIdentifiers", false);
            return options;
        }

        // The name a top-level rule is known by.
        //
        // A rule's data is a class hierarchy whose leaves are shared token
        // vectors, so a faithful encoding of one would be most of the CSS AST's
        // own declaration list. What a caller asking "what did the parser make of
        // this" wants is which kinds of rule there are and how many, and that is
        // what this answers.
        const char* CssRuleKindName(const css::Rule& rule) {
            if (rule.data == nullptr) return "null";
            if (dynamic_cast<const css::RSelector*>(rule.data.get()) != nullptr) return "selector";
            if (dynamic_cast<const css::RQualified*>(rule.data.get()) != nullptr) return "qualified";
            if (dynamic_cast<const css::RDeclaration*>(rule.data.get()) != nullptr) return "declaration";
            if (dynamic_cast<const css::RAtImport*>(rule.data.get()) != nullptr) return "atImport";
            if (dynamic_cast<const css::RAtCharset*>(rule.data.get()) != nullptr) return "atCharset";
            if (dynamic_cast<const css::RKnownAt*>(rule.data.get()) != nullptr) return "atRule";
            if (dynamic_cast<const css::RUnknownAt*>(rule.data.get()) != nullptr) return "unknownAtRule";
            if (dynamic_cast<const css::RComment*>(rule.data.get()) != nullptr) return "comment";
            return "other";
        }

        service::Value HandleParseCSS(const service::Value& request) {
            const std::optional<std::string> text = SourceTextFrom(request);
            if (!text.has_value()) {
                return CompileError("invalid-compile-request",
                    "parseCSS needs its input as a string or as bytes");
            }

            logger::Log log = NewCollectingLog();
            const std::string name = SourceNameFrom(request);
            const logger::Source source = MakeSource(*text, name);

            css::AST ast = css::Parse(log, source, CssParseOptionsFrom(request));
            const std::vector<logger::Msg> msgs = log.done();

            std::vector<service::Value> rules;
            rules.reserve(ast.rules.size());
            for (const css::Rule& rule : ast.rules) {
                Object entry;
                entry.Set("kind", service::Value::String(CssRuleKindName(rule)));
                entry.Set("start", service::Value::Number(ToWireOffset(
                    static_cast<size_t>(rule.loc.start))));
                rules.push_back(std::move(entry).Build());
            }

            const size_t rule_count = ast.rules.size();
            const size_t symbol_count = ast.symbols.size();
            const size_t import_count = ast.import_records.size();

            AstEntry registry_entry;
            registry_entry.ast = std::move(ast);
            registry_entry.language = "css";
            registry_entry.source_name = name;
            const int32_t id = AstRegistry::Instance().Put(std::move(registry_entry));

            Object out;
            out.Set("id", service::Value::Number(id));
            out.Set("rules", service::Value::Array(std::move(rules)));
            out.Set("ruleCount", service::Value::Number(ToWireCount(rule_count)));
            out.Set("symbolCount", service::Value::Number(ToWireCount(symbol_count)));
            out.Set("importRecords", service::Value::Number(ToWireCount(import_count)));
            AppendDiagnostics(out, msgs);
            return out.Build();
        }

        service::Value HandlePrintCSS(const service::Value& request) {
            int32_t key = 0;
            if (!AstKeyFrom(request, key)) {
                return CompileError("invalid-compile-request",
                    "printCSS needs the id of a CSS tree from parseCSS");
            }

            AstEntry* entry = AstRegistry::Instance().Get(key);
            if (entry == nullptr || !std::holds_alternative<css::AST>(entry->ast)) {
                return UnknownAstResponse();
            }

            css::AST& ast = std::get<css::AST>(entry->ast);

            // The symbol map a print needs is the tree's own symbols, in one
            // source. That is the whole of what the printer looks names up in,
            // and a single-source compile has exactly one source's worth of
            // symbols and no cross-file renaming to do.
            compiler::SymbolMap symbols = compiler::NewSymbolMap(1);
            symbols.symbols_for_source[0] = ast.symbols;

            css::PrinterOptions options;
            options.minify_whitespace = BoolFrom(request, "minifyWhitespace", false);
            options.ascii_only = BoolFrom(request, "asciiOnly", false);

            const css::PrintResult result = css::Print(ast, symbols, options);

            Object out;
            out.Set("code", service::Value::String(result.css));
            return out.Build();
        }

        // =========================================================================
        // JavaScript
        // =========================================================================

        // The name a JavaScript token kind is known by.
        //
        // The engine's own ToString(T) is used rather than a switch of this
        // file's own: the enum has a few hundred members, and a table written
        // beside it would be a second list to keep in step with the first. Its
        // names are the ones the lexer itself uses in a diagnostic, which is the
        // right answer to "what kind is this token".
        std::string JsTokenKindName(javascript::T kind) {
            return std::string(javascript::ToString(kind));
        }

        service::Value HandleLexJS(const service::Value& request) {
            const std::optional<std::string> text = SourceTextFrom(request);
            if (!text.has_value()) {
                return CompileError("invalid-compile-request",
                    "lexJS needs its input as a string or as bytes");
            }

            logger::Log log = NewCollectingLog();
            const std::string name = SourceNameFrom(request);
            const logger::Source source = MakeSource(*text, name);

            const LineIndex lines(*text);

            std::vector<service::Value> tokens;
            javascript::Lexer lexer(log, source, config::TSOptions{});

            // Driven to end of file rather than a fixed count: the lexer decides
            // where the tokens end, and a loop with a count in it would have to
            // agree with the lexer about that, which is a second place for the
            // two to disagree.
            while (lexer.token != javascript::T::kEndOfFile) {
                const logger::Range range = lexer.Range();
                const size_t start = static_cast<size_t>(range.loc.start);
                const size_t length = static_cast<size_t>(range.len);
                const Position position = lines.At(start);

                Object entry;
                entry.Set("kind", service::Value::String(JsTokenKindName(lexer.token)));
                entry.Set("value", service::Value::String(std::string(lexer.Raw())));
                entry.Set("start", service::Value::Number(ToWireOffset(start)));
                entry.Set("end", service::Value::Number(ToWireOffset(start + length)));
                entry.Set("line", service::Value::Number(position.line));
                entry.Set("column", service::Value::Number(position.column));
                entry.Set("length", service::Value::Number(ToWireOffset(length)));
                tokens.push_back(std::move(entry).Build());

                // Next() can throw: a malformed literal is a LexerPanic rather
                // than a message. The service's own dispatch catches that and
                // turns it into a diagnostic on the request, so letting it out of
                // here is what makes a lex failure a reported failure rather
                // than a process that died.
                lexer.Next();
            }

            const std::vector<logger::Msg> msgs = log.done();
            const size_t count = tokens.size();

            Object out;
            out.Set("tokens", service::Value::Array(std::move(tokens)));
            out.Set("count", service::Value::Number(ToWireCount(count)));
            AppendDiagnostics(out, msgs);
            return out.Build();
        }

        service::Value HandleParseJS(const service::Value& request) {
            const std::optional<std::string> text = SourceTextFrom(request);
            if (!text.has_value()) {
                return CompileError("invalid-compile-request",
                    "parseJS needs its input as a string or as bytes");
            }

            logger::Log log = NewCollectingLog();
            const std::string name = SourceNameFrom(request);
            const logger::Source source = MakeSource(*text, name);

            // The parser options come from a config::Options, because
            // OptionsFromConfig is the only supported way to build them: the
            // struct holds raw pointers into that config (a tsconfig flag, two
            // regexes) and hand-building it would mean those pointers had to
            // outlive this function on their own. Going through the config means
            // the compiler owns the objects for as long as the parse does.
            //
            // A default-constructed config::Options is a valid one: it is what a
            // build gets before a project has said anything, and the parser reads
            // every field of it.
            config::Options config_options;
            config_options.MinifyWhitespace = BoolFrom(request, "minifyWhitespace", false);
            config_options.MinifySyntax = BoolFrom(request, "minifySyntax", false);
            config_options.MinifyIdentifiers = BoolFrom(request, "minifyIdentifiers", false);
            const javascript::Options js_options = javascript::OptionsFromConfig(&config_options);

            // Parse takes the log and the source by value and shares the log's
            // one buffer, which is why the messages it logged are still readable
            // from "log" afterwards.
            const auto [ast, ok] = javascript::Parse(log, source, js_options);
            const std::vector<logger::Msg> msgs = log.done();

            // The tree is described by its parts and its symbols. A part is a
            // top-level item — a statement, a declaration, an import — and a
            // symbol is a named thing in scope, so together they are the answer
            // to "what did the parser make of this file" without walking a graph
            // of variant-typed expression nodes that has no useful flat form.
            std::vector<service::Value> symbols;
            symbols.reserve(ast.symbols.size());
            for (const compiler::Symbol& symbol : ast.symbols) {
                Object entry;
                entry.Set("name", service::Value::String(symbol.original_name));
                entry.Set("useCount", service::Value::Number(ToWireCount(symbol.use_count_estimate)));
                symbols.push_back(std::move(entry).Build());
            }

            const size_t part_count = ast.parts.size();

            AstEntry registry_entry;
            registry_entry.ast = ast;
            registry_entry.language = "js";
            registry_entry.source_name = name;
            const int32_t id = AstRegistry::Instance().Put(std::move(registry_entry));

            Object out;
            out.Set("id", service::Value::Number(id));
            out.Set("ok", service::Value::Bool(ok));
            out.Set("partCount", service::Value::Number(ToWireCount(part_count)));
            out.Set("symbols", service::Value::Array(std::move(symbols)));
            AppendDiagnostics(out, msgs);
            return out.Build();
        }

        service::Value HandlePrintJS(const service::Value& request) {
            int32_t key = 0;
            if (!AstKeyFrom(request, key)) {
                return CompileError("invalid-compile-request",
                    "printJS needs the id of a JavaScript tree from parseJS");
            }

            AstEntry* entry = AstRegistry::Instance().Get(key);
            if (entry == nullptr || !std::holds_alternative<javascript::AST>(entry->ast)) {
                return UnknownAstResponse();
            }

            javascript::AST& ast = std::get<javascript::AST>(entry->ast);

            // A single-source symbol map and a no-op renamer, which together mean
            // "print these names as they are". The other renamers are about
            // assigning short names, and that is a linking decision: a standalone
            // tree has no other file whose names it could collide with, so there
            // is nothing to shorten it against.
            compiler::SymbolMap symbols = compiler::NewSymbolMap(1);
            symbols.symbols_for_source[0] = ast.symbols;
            std::unique_ptr<javascript::Renamer> renamer =
                javascript::NewNoOpRenamer(symbols);

            javascript::PrinterOptions options;
            options.minify_whitespace = BoolFrom(request, "minifyWhitespace", false);
            options.minify_syntax = BoolFrom(request, "minifySyntax", false);
            options.minify_identifiers = BoolFrom(request, "minifyIdentifiers", false);
            options.ascii_only = BoolFrom(request, "asciiOnly", false);
            // The runtime is omitted because a standalone print has no module to
            // import it from. A caller printing one file is reading what the
            // parser made of it, and a page of runtime helpers at the top of that
            // output is not an answer to that question. This is the same flag the
            // parser's own tests print with, for the same reason.
            options.omit_runtime_for_tests = true;

            const javascript::PrintResult result = javascript::Print(ast, symbols, *renamer, options);

            Object out;
            out.Set("code", service::Value::String(result.js));
            return out.Build();
        }

        // =========================================================================
        // Transform
        // =========================================================================

        // The transform stage, for CSS.
        //
        // "transform" is the stage between parsing and printing, and what runs in
        // it is mostly not a separate pass: the JavaScript parser lowers as it
        // parses, the CSS parser lowers nesting as it parses, and the HTML
        // bridge collects import records as it parses. A transform over an
        // already-parsed tree therefore has exactly one thing to do that really
        // is a pass over a tree, and it is the one the engine has.
        //
        // Every transform answers with "passes", the list of what actually ran,
        // so a caller can tell a transform that did something from one that had
        // nothing to do, without having to guess from the output.
        service::Value HandleTransformCSS(const service::Value& request) {
            int32_t key = 0;
            if (!AstKeyFrom(request, key)) {
                return CompileError("invalid-compile-request",
                    "transformCSS needs the id of a CSS tree from parseCSS");
            }

            // The input handle is consumed rather than left in place, because the
            // tree it names is the tree being changed and a caller that still
            // held it would be holding a tree that no longer exists anywhere.
            std::optional<AstEntry> taken = AstRegistry::Instance().Take(key);
            if (!taken.has_value() || !std::holds_alternative<css::AST>(taken->ast)) {
                return UnknownAstResponse();
            }
            AstEntry entry = std::move(*taken);
            css::AST ast = std::get<css::AST>(std::move(entry.ast));

            logger::Log log = NewCollectingLog();

            // Dead rule removal answers "can this selector match anything", and
            // in a bundle the answer comes from the HTML that uses it. A
            // standalone stylesheet has no HTML, so every selector is unproven and
            // removing on that basis would empty the file. The pass therefore
            // runs only where a caller has said it knows what it is doing, and
            // the count of what it did is reported either way.
            size_t removed = 0;
            std::vector<service::Value> passes;
            if (BoolFrom(request, "removeDeadRules", false)) {
                const size_t before = ast.rules.size();
                css::DeadRuleRemover remover(compiler::NewSymbolMap(1));
                ast.rules = remover.RemoveDeadRulesInPlace(
                    0, std::move(ast.rules), ast.import_records);
                removed = before - ast.rules.size();
                passes.push_back(service::Value::String("removeDeadRules"));
            }

            const size_t remaining = ast.rules.size();
            const std::vector<logger::Msg> msgs = log.done();

            // A new handle rather than the same one: the tree was changed, and
            // returning a new id says so. A caller holding the old handle no
            // longer has a tree at all, which is the guarantee a functional API
            // gives and the reason transform is not in-place.
            AstEntry out_entry;
            out_entry.ast = std::move(ast);
            out_entry.language = "css";
            out_entry.source_name = std::move(entry.source_name);
            const int32_t id = AstRegistry::Instance().Put(std::move(out_entry));

            Object out;
            out.Set("id", service::Value::Number(id));
            out.Set("passes", service::Value::Array(std::move(passes)));
            out.Set("removed", service::Value::Number(ToWireCount(removed)));
            out.Set("ruleCount", service::Value::Number(ToWireCount(remaining)));
            AppendDiagnostics(out, msgs);
            return out.Build();
        }

        // The transform stage, for a language whose transforms are fused into
        // parsing: the identity pass, and the notice that it ran.
        //
        // HTML and JavaScript have no standalone tree pass: the parser lowers
        // as it parses, so the tree a parse produces is already the transformed
        // tree. A transform over such a tree therefore has nothing to change,
        // and it is still a real command: it consumes the handle it was given,
        // answers with a new one naming the same tree, and says in "passes"
        // that nothing ran and in "note" why. That is the honest form of an
        // AST-to-AST transform for a language that has no passes to run — the
        // call works, the handle moves on, and no caller is made to believe a
        // tree was rewritten when it was not.
        service::Value HandleIdentityTransform(const service::Value& request,
                                                const char* language,
                                                const char* display_name) {
            int32_t key = 0;
            if (!AstKeyFrom(request, key)) {
                const std::string command = std::string("transform") + display_name;
                return CompileError("invalid-compile-request",
                    command + " needs the id of a " + language + " tree from parse" + display_name);
            }

            std::optional<AstEntry> taken = AstRegistry::Instance().Take(key);
            if (!taken.has_value() || taken->language != language) {
                return UnknownAstResponse();
            }

            const std::string source_name = std::move(taken->source_name);
            taken->source_name.clear();

            const int32_t id = AstRegistry::Instance().Put(std::move(*taken));

            Object out;
            out.Set("id", service::Value::Number(id));
            out.Set("passes", service::Value::Array(std::vector<service::Value>{}));
            out.Set("note", service::Value::String(
                display_name + std::string(" transforms as it parses: the parsed tree is already "
                "the transformed tree, so no standalone passes ran and the tree is unchanged")));
            out.Set("sourcefile", service::Value::String(source_name));
            AppendDiagnostics(out, {});
            return out.Build();
        }

        // =========================================================================
        // Dispatch
        // =========================================================================

        // The command table for the compile half of the service.
        //
        // Four commands per language, named "<stage>-<language>", because a host
        // reads a command name and the name is the cheapest documentation there
        // is. A request that does not name one of them is a protocol error, and
        // it is reported with the same wording as any other unknown command so a
        // host has one thing to read for "I asked for something that is not
        // here".
        service::Value DispatchCompile(const std::string& name, const service::Value& request) {
            if (name == "lex-html") return HandleLexHTML(request);
            if (name == "parse-html") return HandleParseHTML(request);
            if (name == "print-html") return HandlePrintHTML(request);
            if (name == "lex-css") return HandleLexCSS(request);
            if (name == "parse-css") return HandleParseCSS(request);
            if (name == "print-css") return HandlePrintCSS(request);
            if (name == "transform-css") return HandleTransformCSS(request);
            if (name == "lex-js") return HandleLexJS(request);
            if (name == "parse-js") return HandleParseJS(request);
            if (name == "print-js") return HandlePrintJS(request);

            if (name == "transform-html") return HandleIdentityTransform(request, "html", "HTML");
            if (name == "transform-js") return HandleIdentityTransform(request, "js", "JS");

            return CompileError("unknown-command", "unknown command: " + name);
        }

        // Whether a name belongs to this table. Used by the main dispatch to tell
        // a compile command from a build command, and to keep the two tables from
        // both claiming a name.
        bool IsCompileCommand(const std::string& name) {
            return name.rfind("lex-", 0) == 0
                || name.rfind("parse-", 0) == 0
                || name.rfind("print-", 0) == 0
                || name.rfind("transform-", 0) == 0;
        }

    } // namespace

    // The entry point the main service dispatch calls. Declared in cli.hpp
    // alongside RunServiceRequest so that the table stays in one place.
    service::Value RunServiceCompileRequest(const std::string& name, const service::Value& request) {
        return DispatchCompile(name, request);
    }

    bool IsServiceCompileCommand(const std::string& name) {
        return IsCompileCommand(name);
    }

} // namespace guchho::cli
