// Tests for the compiler half of the service: the twelve lex, parse, transform
// and print commands.
//
// What matters about these commands is that they are the engine's own, reached
// without a build around them. That is easy to claim and easy to get wrong: a
// handler that constructs its own parser, or that runs a different configuration
// from the one a build would use, still produces plausible tokens and plausible
// trees. So the tests here check against what the engines are documented to
// produce — a name the lexer spells a particular way, a document the HTML parser
// wraps in html/head/body, a stylesheet that prints back to the same declaration
// — rather than merely checking that something came back.
//
// The handle is the other thing worth testing, because it is the whole reason a
// parse and a print are two commands rather than one. A handle has to be stable
// until the tree it names is transformed, has to answer for exactly one
// language, and has to be gone afterwards: a handle that survives its own
// transform is a handle two trees can be reached through, which is how a caller
// ends up printing a tree that no longer exists.
//
// Every test goes through RunServiceRequest, so none of them starts a process
// or opens a pipe. What happens in the frames is the npm suite's problem,
// because the other side of it is JavaScript.

#include "test/guchho_test.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "guchho/cli.hpp"
#include "guchho/service.hpp"

namespace cli::test {

namespace {

using guchho::service::Value;

// A compile request: the command, the source text, and whatever else the caller
// wanted to say. Built as an object because a request is a document someone else
// wrote, and the options belong on it rather than in the command name.
Value CompileRequest(const std::string& command, const std::string& input) {
    return Value::Object({
        {"command", Value::String(command)},
        {"input", Value::String(input)},
    });
}

// Runs one command and hands back what the service answered.
Value Run(const std::string& command, const std::string& input) {
    return guchho::cli::RunServiceRequest(CompileRequest(command, input));
}

// The "error" string on a response, or "" when there is none. A refusal is a
// refusal and says something, and these tests care about which of the two a
// command produced.
std::string ErrorOf(const Value& response) {
    const Value* error = response.Find("error");
    return error != nullptr && error->IsString() ? error->AsString() : std::string();
}

// The number of items in an array field, or -1 when the field is missing or is
// not an array. -1 rather than 0 so a missing field is a failure rather than a
// silent zero.
int CountOf(const Value& response, const std::string& key) {
    const Value* field = response.Find(key);
    if (field == nullptr || !field->IsArray()) return -1;
    return static_cast<int>(field->AsArray().size());
}

// The AST id a parse answered with, or -1 when it did not answer with one.
int IdOf(const Value& response) {
    const Value* id = response.Find("id");
    if (id == nullptr || !id->IsNumber()) return -1;
    return id->AsNumber();
}

// The "code" a print answered with.
std::string CodeOf(const Value& response) {
    const Value* code = response.Find("code");
    if (code == nullptr || !code->IsString()) return std::string();
    return code->AsString();
}

// A number field, or -1 when it is missing. Used for the counts a parse reports.
int32_t NumberOf(const Value& response, const std::string& key) {
    const Value* value = response.Find(key);
    if (value == nullptr || !value->IsNumber()) return -1;
    return value->AsNumber();
}

// The text of the "kind" field of the token at "index", or "" when there is no
// such token. Reading it by index rather than by name is deliberate: the order
// of the stream is part of what a lexer promises, and a lookup that ignored the
// order would pass on a stream that was reversed.
std::string KindOfToken(const Value& response, size_t index) {
    const Value* tokens = response.Find("tokens");
    if (tokens == nullptr || !tokens->IsArray()) return std::string();
    const Value* token = tokens->At(index);
    if (token == nullptr) return std::string();
    const Value* kind = token->Find("kind");
    return kind != nullptr && kind->IsString() ? kind->AsString() : std::string();
}

// The "value" field of the token at "index".
std::string ValueOfToken(const Value& response, size_t index) {
    const Value* tokens = response.Find("tokens");
    if (tokens == nullptr || !tokens->IsArray()) return std::string();
    const Value* token = tokens->At(index);
    if (token == nullptr) return std::string();
    const Value* value = token->Find("value");
    return value != nullptr && value->IsString() ? value->AsString() : std::string();
}

// The start offset of the token at "index", or -1 when it has none.
int32_t StartOfToken(const Value& response, size_t index) {
    const Value* tokens = response.Find("tokens");
    if (tokens == nullptr || !tokens->IsArray()) return -1;
    const Value* token = tokens->At(index);
    if (token == nullptr) return -1;
    const Value* start = token->Find("start");
    return start != nullptr && start->IsNumber() ? start->AsNumber() : -1;
}

// The "tag" of the node at "index" in a parsed HTML tree, or "" when the node
// is not an element.
std::string TagOfNode(const Value& response, size_t index) {
    const Value* nodes = response.Find("nodes");
    if (nodes == nullptr || !nodes->IsArray()) return std::string();
    const Value* node = nodes->At(index);
    if (node == nullptr) return std::string();
    const Value* tag = node->Find("tag");
    return tag != nullptr && tag->IsString() ? tag->AsString() : std::string();
}

// The depth of the node at "index" in a parsed HTML tree.
int32_t DepthOfNode(const Value& response, size_t index) {
    const Value* nodes = response.Find("nodes");
    if (nodes == nullptr || !nodes->IsArray()) return -1;
    const Value* node = nodes->At(index);
    if (node == nullptr) return -1;
    const Value* depth = node->Find("depth");
    return depth != nullptr && depth->IsNumber() ? depth->AsNumber() : -1;
}

// A print or transform request for a tree under a given handle.
Value RequestFor(const std::string& command, int32_t id) {
    return Value::Object({
        {"command", Value::String(command)},
        {"ast", Value::Number(id)},
    });
}

} // namespace

// ---------------------------------------------------------------------------
// Which names belong to this half of the service
// ---------------------------------------------------------------------------

// The four stages of one language are all recognised, and nothing else is.
TEST(CliServiceCompile, TheTwelveCommandsAreRecognisedByTheirShape) {
    EXPECT_TRUE(guchho::cli::IsServiceCompileCommand("lex-html"));
    EXPECT_TRUE(guchho::cli::IsServiceCompileCommand("parse-css"));
    EXPECT_TRUE(guchho::cli::IsServiceCompileCommand("print-js"));
    EXPECT_TRUE(guchho::cli::IsServiceCompileCommand("transform-css"));

    // The build commands are not, and this is the property that matters: the two
    // tables must not both claim a name, or a build would be answerable by a
    // lexer.
    EXPECT_FALSE(guchho::cli::IsServiceCompileCommand("build"));
    EXPECT_FALSE(guchho::cli::IsServiceCompileCommand("transform"));
    EXPECT_FALSE(guchho::cli::IsServiceCompileCommand("not-a-command"));
}

// A name that looks like a compile command but names no stage is still refused
// by the table rather than accepted, because the prefix is what routes a request
// here and the table is what has to say no.
TEST(CliServiceCompile, ACommandInThePrefixThatNamesNoStageIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(CompileRequest("lex-fortran", "x"));
    EXPECT_FALSE(ErrorOf(response).empty());
}

// A request with no source at all is refused, and the empty string is not.
TEST(CliServiceCompile, NoInputIsRefusedAndEmptyInputIsNot) {
    const Value missing = guchho::cli::RunServiceRequest(
        Value::Object({{"command", Value::String("lex-html")}}));
    EXPECT_FALSE(ErrorOf(missing).empty());

    // Lexing nothing is a real operation: the answer is the end-of-file token
    // and nothing else. A request that said "here is nothing" and got nothing
    // back could not be told apart from one that never arrived.
    const Value empty = Run("lex-html", "");
    EXPECT_TRUE(ErrorOf(empty).empty()) << ErrorOf(empty);
    EXPECT_EQ(NumberOf(empty, "count"), 1);
}

// ---------------------------------------------------------------------------
// HTML
// ---------------------------------------------------------------------------

// The HTML tokenizer is the engine's, so it says what the engine says: a start
// tag, a character run, an end tag, then end of input.
TEST(CliServiceCompile, HtmlLexesIntoTheTokensTheTokenizerNames) {
    const Value response = Run("lex-html", "<p>hi</p>");

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(KindOfToken(response, 0), "start-tag");
    EXPECT_EQ(ValueOfToken(response, 0), "p");
    EXPECT_EQ(StartOfToken(response, 0), 0);
    EXPECT_EQ(KindOfToken(response, 1), "character");
    EXPECT_EQ(ValueOfToken(response, 1), "hi");
    EXPECT_EQ(KindOfToken(response, 2), "end-tag");
    EXPECT_EQ(KindOfToken(response, 3), "eof");
    EXPECT_EQ(NumberOf(response, "count"), CountOf(response, "tokens"));
}

// Offsets are counted in the source's own bytes, so a second line's token says
// line 2 and a column relative to that line. The tokens of "<a>\n<b>" are a
// start tag, the whitespace run, the b start tag, then end of input: the b
// start tag is the one at index 2.
TEST(CliServiceCompile, HtmlTokenPositionsCountLinesAndColumns) {
    const Value response = Run("lex-html", "<a>\n<b>");
    const Value* tokens = response.Find("tokens");
    ASSERT_TRUE(tokens != nullptr && tokens->IsArray());
    const Value* second_tag = tokens->At(2);
    ASSERT_TRUE(second_tag != nullptr);
    EXPECT_EQ(second_tag->Find("line")->AsNumber(), 2);
    EXPECT_EQ(second_tag->Find("column")->AsNumber(), 0);
    EXPECT_EQ(second_tag->Find("start")->AsNumber(), 4);
}

// A parse answers with a handle and a flattened tree, and the depths are what
// make the flat list a tree. The document node is the root of the list, so the
// whole shape of "#document → html → (head, body) → div → span → text" reads
// as seven entries with the div three deep.
TEST(CliServiceCompile, HtmlParseAnswersWithAHandleAndADepthList) {
    const Value response = Run("parse-html", "<div id=\"x\"><span>t</span></div>");

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_GE(IdOf(response), 0);

    // The html/head/body wrappers are the parser's doing, not the input's: a
    // conforming HTML parser synthesizes them around a bare fragment, so a tree
    // that did not have them would mean a different parser produced it.
    EXPECT_EQ(CountOf(response, "nodes"), 7);
    EXPECT_EQ(DepthOfNode(response, 0), 0);
    EXPECT_EQ(TagOfNode(response, 1), "html");
    EXPECT_EQ(DepthOfNode(response, 1), 1);
    EXPECT_EQ(TagOfNode(response, 4), "div");
    EXPECT_EQ(DepthOfNode(response, 4), 3);
    EXPECT_EQ(NumberOf(response, "nodeCount"), 7);
}

// The div's own attributes are reported by name, and the value is not in the
// summary: a summary is the shape of the tree, and an attribute value can be
// the whole body of a data island.
TEST(CliServiceCompile, HtmlNodesCarryElementAttributeNamesButNotValues) {
    const Value response = Run("parse-html", "<div id=\"x\" data-v=\"1\"></div>");
    const Value* nodes = response.Find("nodes");
    ASSERT_TRUE(nodes != nullptr && nodes->IsArray());
    const Value* attrs = nodes->At(4)->Find("attributes");
    ASSERT_TRUE(attrs != nullptr && attrs->IsArray());
    ASSERT_EQ(attrs->AsArray().size(), 2u);
    EXPECT_EQ(attrs->AsArray().at(0).AsString(), "id");
    EXPECT_EQ(attrs->AsArray().at(1).AsString(), "data-v");
}

// A print goes back to the handle a parse gave, and it is the same printer a
// build uses: the document comes back wrapped, exactly as it was parsed.
TEST(CliServiceCompile, HtmlPrintAnswersForTheHandleAParseGave) {
    const int32_t id = IdOf(Run("parse-html", "<div id=\"x\">t</div>"));
    ASSERT_GE(id, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-html", id));
    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(CodeOf(response), "<html><head></head><body><div id=\"x\">t</div></body></html>");
}

// A fragment parse is a real thing for a caller that wants the source without
// the document the parser otherwise builds: it parses to a fragment root, and
// printing that root prints the fragment, not the html/head/body document.
TEST(CliServiceCompile, AFragmentParsePrintsTheFragment) {
    Value request = Value::Object({
        {"command", Value::String("parse-html")},
        {"input", Value::String("<div id=\"x\">t</div>")},
        {"fragment", Value::Bool(true)},
    });

    const int32_t id = IdOf(guchho::cli::RunServiceRequest(request));
    ASSERT_GE(id, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-html", id));
    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(CodeOf(response), "<div id=\"x\">t</div>");
}

// A handle that names nothing is refused, rather than printing an empty
// document and letting a caller believe the tree was empty.
TEST(CliServiceCompile, AHandleThatNamesNothingIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-html", 999999));
    EXPECT_FALSE(ErrorOf(response).empty());
}

// A print with no handle at all is refused: there is no tree to print.
TEST(CliServiceCompile, APrintWithNoHandleIsRefused) {
    const Value response = guchho::cli::RunServiceRequest(
        Value::Object({{"command", Value::String("print-html")}}));
    EXPECT_FALSE(ErrorOf(response).empty());
}

// A handle belongs to one language. Printing a CSS tree as HTML is a mistake in
// the host, and it is a mistake that has to be reported rather than reinterpreted.
TEST(CliServiceCompile, AHandleFromOneLanguageIsNotAcceptedByAnother) {
    const int32_t css = IdOf(Run("parse-css", ".a{color:red}"));
    ASSERT_GE(css, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-html", css));
    EXPECT_FALSE(ErrorOf(response).empty());
}

// ---------------------------------------------------------------------------
// CSS
// ---------------------------------------------------------------------------

// The CSS token names are the enum's own, written in the camel case a host would
// use. The engine's own table would say "identifier" for both kIdent and
// kSymbol, which is a diagnostic's wording rather than an API's.
TEST(CliServiceCompile, CssLexesIntoTheNamedKinds) {
    const Value response = Run("lex-css", ".a{color:red}");

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_EQ(KindOfToken(response, 0), "dot");
    EXPECT_EQ(ValueOfToken(response, 0), ".");
    EXPECT_EQ(KindOfToken(response, 1), "ident");
    EXPECT_EQ(ValueOfToken(response, 1), "a");
    EXPECT_EQ(KindOfToken(response, 2), "openBrace");
    EXPECT_EQ(KindOfToken(response, 3), "ident");
    EXPECT_EQ(ValueOfToken(response, 3), "color");
    EXPECT_EQ(KindOfToken(response, 5), "ident");
    EXPECT_EQ(ValueOfToken(response, 5), "red");
    EXPECT_EQ(KindOfToken(response, 6), "closeBrace");
}

// Comments are not tokens in the CSS lexer: there is no comment kind in its
// token table. A lexer asked what it saw answers with the stream and the
// comments separately, and the two together are the source.
TEST(CliServiceCompile, CssCommentsAreInTheirOwnListUnlessAskedOtherwise) {
    const Value with = Run("lex-css", "/* hi */.a{}");
    EXPECT_TRUE(ErrorOf(with).empty()) << ErrorOf(with);
    EXPECT_EQ(CountOf(with, "comments"), 1);
    // ".", "a", "{", "}" — four tokens, with the comment kept out of the
        // stream.
    EXPECT_EQ(CountOf(with, "tokens"), 4);
    const Value* comments = with.Find("comments");
    ASSERT_TRUE(comments != nullptr && comments->IsArray());
    EXPECT_EQ(comments->AsArray().at(0).Find("text")->AsString(), "/* hi */");

    Value request = Value::Object({
        {"command", Value::String("lex-css")},
        {"input", Value::String("/* hi */.a{}")},
        {"includeComments", Value::Bool(false)},
    });
    const Value without = guchho::cli::RunServiceRequest(request);
    EXPECT_EQ(CountOf(without, "comments"), 0);
}

// A stylesheet parses to a list of named rules with the source offset each one
// started at, which is the flat answer to "what did the parser make of this".
TEST(CliServiceCompile, CssParseAnswersWithNamedRulesAndOffsets) {
    const Value response = Run("parse-css", ".a{color:red}");

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_GE(IdOf(response), 0);
    EXPECT_EQ(NumberOf(response, "ruleCount"), 1);
    EXPECT_EQ(CountOf(response, "rules"), 1);

    const Value* rules = response.Find("rules");
    ASSERT_TRUE(rules != nullptr && rules->IsArray());
    EXPECT_EQ(rules->AsArray().at(0).Find("kind")->AsString(), "selector");
    EXPECT_EQ(rules->AsArray().at(0).Find("start")->AsNumber(), 0);
}

// Round trip: what a stylesheet parses to, it prints back to. The declaration
// is what matters — a printer that dropped it would still return a string.
TEST(CliServiceCompile, CssPrintsBackWhatAParseMade) {
    const int32_t id = IdOf(Run("parse-css", ".a{color:red}"));
    ASSERT_GE(id, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-css", id));
    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_NE(CodeOf(response).find("color"), std::string::npos) << CodeOf(response);
    EXPECT_NE(CodeOf(response).find("red"), std::string::npos) << CodeOf(response);
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

// A transform consumes the handle it was given and answers with a new one.
//
// This is the whole reason transform is not in-place. A caller that still held
// the old handle would be holding a tree that had been changed, and two handles
// naming two different things would have been the cheaper design. So the old
// handle stops answering, and the count of what changed is reported.
TEST(CliServiceCompile, ACssTransformConsumesItsHandleAndAnswersWithANewOne) {
    const int32_t parsed = IdOf(Run("parse-css", ".a{color:red}.b{color:blue}"));
    ASSERT_GE(parsed, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("transform-css", parsed));
    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);

    const int32_t transformed = IdOf(response);
    EXPECT_GE(transformed, 0);
    EXPECT_NE(transformed, parsed) << "a transform that reuses the handle cannot say it changed anything";
    EXPECT_EQ(NumberOf(response, "ruleCount"), 2);
    EXPECT_EQ(CountOf(response, "passes"), 0) << "no options ran, so no passes ran";

    // The old handle is gone. This is the part that would be missed by a test
    // that only checked the new one answered.
    const Value reused = guchho::cli::RunServiceRequest(RequestFor("print-css", parsed));
    EXPECT_FALSE(ErrorOf(reused).empty()) << "a consumed handle still answers";
}

// A transform with nothing to do is still a transform: it consumes its handle
// and reports that it removed nothing. A pass that had removed rules by default
// would empty a standalone stylesheet, so it is opt-in, and "passes" says which
// ones actually ran in the one place a caller can tell.
TEST(CliServiceCompile, ACssTransformWithNoPassesReportsRemovingNothing) {
    const int32_t parsed = IdOf(Run("parse-css", ".a{color:red}.b{color:blue}"));
    ASSERT_GE(parsed, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("transform-css", parsed));
    EXPECT_EQ(NumberOf(response, "removed"), 0);
    EXPECT_EQ(NumberOf(response, "ruleCount"), 2);
    EXPECT_EQ(CountOf(response, "passes"), 0);

    Value request = Value::Object({
        {"command", Value::String("transform-css")},
        {"ast", Value::Number(NumberOf(response, "id"))},
        {"removeDeadRules", Value::Bool(true)},
    });
    const Value with_pass = guchho::cli::RunServiceRequest(request);
    EXPECT_EQ(NumberOf(with_pass, "removed") + NumberOf(with_pass, "ruleCount"), 2)
        << "removed and remaining must account for every rule";
    EXPECT_EQ(CountOf(with_pass, "passes"), 1);
    const Value* passes = with_pass.Find("passes");
    ASSERT_TRUE(passes != nullptr && passes->IsArray());
    EXPECT_EQ(passes->AsArray().at(0).AsString(), "removeDeadRules");
}

// The new handle is a tree in its own right, not a spent ticket.
TEST(CliServiceCompile, TheTreeACssTransformAnswersWithCanBePrinted) {
    const int32_t parsed = IdOf(Run("parse-css", ".a{color:red}"));
    ASSERT_GE(parsed, 0);

    const int32_t transformed = IdOf(
        guchho::cli::RunServiceRequest(RequestFor("transform-css", parsed)));
    ASSERT_GE(transformed, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-css", transformed));
    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_NE(CodeOf(response).find("color"), std::string::npos) << CodeOf(response);
}

// HTML and JavaScript have no standalone tree pass: their transforms run as
// they parse. An identity transform is the honest shape of that: it consumes
// the handle, answers with a new one naming the same tree, and says in one
// place that it ran no passes and in another why. The two handles are the
// AST-to-AST contract, the empty list is the truth about what was done, and a
// caller can print the new handle and get the source back.
TEST(CliServiceCompile, AnIdentityTransformConsumesItsHandleSaysNoPassesRanAndCanBePrinted) {
    struct Case {
        const char* command;
        const char* parse_command;
        const char* print_command;
        const char* source;
    };
    const Case cases[] = {
        {"transform-html", "parse-html", "print-html", "<p>x</p>"},
        {"transform-js", "parse-js", "print-js", "const x = 1;"},
    };

    for (const Case& c : cases) {
        const int32_t parsed = IdOf(Run(c.parse_command, c.source));
        ASSERT_GE(parsed, 0);

        const Value response = guchho::cli::RunServiceRequest(RequestFor(c.command, parsed));
        EXPECT_TRUE(ErrorOf(response).empty()) << c.command << " answered " << ErrorOf(response);

        const int32_t transformed = IdOf(response);
        EXPECT_GE(transformed, 0);
        EXPECT_NE(transformed, parsed) << c.command << " reused the handle";
        EXPECT_EQ(CountOf(response, "passes"), 0) << c.command << " claimed a pass ran";
        const Value* note = response.Find("note");
        ASSERT_TRUE(note != nullptr && note->IsString());
        EXPECT_FALSE(note->AsString().empty()) << c.command << " did not say why nothing ran";

        const Value printed = guchho::cli::RunServiceRequest(
            RequestFor(c.print_command, transformed));
        EXPECT_TRUE(ErrorOf(printed).empty()) << c.command << " new handle " << ErrorOf(printed);

        const Value reused = guchho::cli::RunServiceRequest(RequestFor(c.command, parsed));
        EXPECT_FALSE(ErrorOf(reused).empty()) << c.command << " old handle still answers";
    }
}

// ---------------------------------------------------------------------------
// JavaScript
// ---------------------------------------------------------------------------

// The JS lexer is driven to end of input rather than a count, so the last token
// before the loop ends is the one before end of file.
TEST(CliServiceCompile, JsLexesToEndOfInput) {
    const Value response = Run("lex-js", "let a = 1;");

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_GE(NumberOf(response, "count"), 5);
    EXPECT_EQ(ValueOfToken(response, 0), "let");
    EXPECT_EQ(ValueOfToken(response, 1), "a");
}

// A parse answers with the file's top-level parts and the symbols in scope,
// which together say what the parser made of the file without walking its
// expression graph.
TEST(CliServiceCompile, JsParseAnswersWithPartsAndSymbols) {
    const Value response = Run("parse-js", "const answer = 42;");

    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_GE(IdOf(response), 0);

    const Value* ok = response.Find("ok");
    ASSERT_TRUE(ok != nullptr && ok->IsBool());
    EXPECT_TRUE(ok->AsBool()) << "a valid program should parse";

    // The declaration and the file's synthesized wrapper make the parts; the
    // symbols include the ones the parser minted (intrinsics, the wrapper's
    // helpers) as well as the identifier the source declared, so the assertion
    // is that "answer" is among them rather than that it is the only one.
    EXPECT_GE(NumberOf(response, "partCount"), 1);
    EXPECT_GE(CountOf(response, "symbols"), 1);
    const Value* symbols = response.Find("symbols");
    ASSERT_TRUE(symbols != nullptr && symbols->IsArray());
    bool named = false;
    for (const Value& symbol : symbols->AsArray()) {
        const Value* name = symbol.Find("name");
        if (name != nullptr && name->IsString() && name->AsString() == "answer") named = true;
    }
    EXPECT_TRUE(named) << "the declared identifier should be among the symbols";
}

// A print is the engine's printer, so the program's own text comes back, and
// without the runtime preamble a single file has no module to import it from.
TEST(CliServiceCompile, JsPrintsBackWhatAParseMade) {
    const int32_t id = IdOf(Run("parse-js", "const answer = 42;"));
    ASSERT_GE(id, 0);

    const Value response = guchho::cli::RunServiceRequest(RequestFor("print-js", id));
    EXPECT_TRUE(ErrorOf(response).empty()) << ErrorOf(response);
    EXPECT_NE(CodeOf(response).find("answer"), std::string::npos) << CodeOf(response);
}

// A syntax error is a diagnostic, not a refusal and not a crash. The service is
// shared by every request that follows, so a program that does not parse has to
// come back as an answer the host can render.
TEST(CliServiceCompile, ASyntaxErrorIsAnAnswerAndNotARefusal) {
    const Value response = Run("lex-js", "const a = (;");

    // The lexer reports through the same two lists every other command does,
    // so a host has one place to read problems from whichever stage produced
    // them.
    EXPECT_TRUE(response.Find("errors") != nullptr);
    EXPECT_TRUE(response.Find("warnings") != nullptr);
    EXPECT_TRUE(response.Find("errors")->IsArray());
    EXPECT_TRUE(response.Find("warnings")->IsArray());
}

// Every compile answer carries the two diagnostic lists, whether or not
// anything went wrong, so a host never has to ask whether the field is there.
TEST(CliServiceCompile, EveryCompileAnswerCarriesBothDiagnosticLists) {
    for (const char* command : {"lex-html", "lex-css", "lex-js",
                                 "parse-html", "parse-css", "parse-js"}) {
        const Value response = Run(command, "x");
        EXPECT_TRUE(ErrorOf(response).empty()) << command;
        EXPECT_TRUE(response.Find("errors") != nullptr) << command;
        EXPECT_TRUE(response.Find("warnings") != nullptr) << command;
    }
}

} // namespace cli::test
