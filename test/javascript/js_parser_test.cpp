#include "test/helpers/javascript_test.hpp"

TEST(JsParser, TestUnOp) {
    expectPrinted("let x; void 0; x", "let x;\nx;\n");
    expectPrinted("let x; void x; x", "let x;\nvoid x;\nx;\n");
}

TEST(JsParser, TestBinOp) {
    for (size_t i = 0; i <= static_cast<size_t>(js::OpCode::kBinOpLogicalAndAssign); i++) {
        js::OpCode op_code = static_cast<js::OpCode>(i);
        std::string op(js::kOpTable[i].text);

        if (js::IsLeftAssociative(op_code)) {
            expectPrinted("a " + op + " b " + op + " c", "a " + op + " b " + op + " c;\n");
            expectPrinted("(a " + op + " b) " + op + " c", "a " + op + " b " + op + " c;\n");
            expectPrinted("a " + op + " (b " + op + " c)", "a " + op + " (b " + op + " c);\n");
        }

        if (js::IsRightAssociative(op_code)) {
            expectPrinted("a " + op + " b " + op + " c", "a " + op + " b " + op + " c;\n");

            // Avoid errors about invalid assignment targets
            if (js::BinaryAssignTarget(op_code) == js::AssignTarget::kNone) {
                expectPrinted("(a " + op + " b) " + op + " c", "(a " + op + " b) " + op + " c;\n");
            }

            expectPrinted("a " + op + " (b " + op + " c)", "a " + op + " b " + op + " c;\n");
        }
    }
}

TEST(JsParser, TestComments) {
    expectParseError("throw //\n x", "<stdin>: ERROR: Unexpected newline after \"throw\"\n");
    expectParseError("throw /**/\n x", "<stdin>: ERROR: Unexpected newline after \"throw\"\n");
    expectParseError("throw <!--\n x",
        "<stdin>: ERROR: Unexpected newline after \"throw\"\n"
        "<stdin>: WARNING: Treating \"<!--\" as the start of a legacy HTML single-line comment\n");
    expectParseError("throw -->\n x", "<stdin>: ERROR: Unexpected \">\"\n");

    expectParseError("export {}\n<!--",
        "<stdin>: ERROR: Legacy HTML single-line comments are not allowed in ECMAScript modules\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n"
        "<stdin>: WARNING: Treating \"<!--\" as the start of a legacy HTML single-line comment\n");

    expectParseError("export {}\n-->",
        "<stdin>: ERROR: Legacy HTML single-line comments are not allowed in ECMAScript modules\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n"
        "<stdin>: WARNING: Treating \"-->\" as the start of a legacy HTML single-line comment\n");

    expectPrinted("return //\n x", "return;\nx;\n");
    expectPrinted("return /**/\n x", "return;\nx;\n");
    expectPrinted("return <!--\n x", "return;\nx;\n");
    expectPrinted("-->\nx", "x;\n");
    expectPrinted("x\n-->\ny", "x;\ny;\n");
    expectPrinted("x\n -->\ny", "x;\ny;\n");
    expectPrinted("x\n/**/-->\ny", "x;\ny;\n");
    expectPrinted("x/*\n*/-->\ny", "x;\ny;\n");
    expectPrinted("x\n/**/ /**/-->\ny", "x;\ny;\n");
    expectPrinted("if(x-->y)z", "if (x-- > y) z;\n");
}

TEST(JsParser, TestStrictModeNonSimple) {

    const std::string nonSimple = "<stdin>: ERROR: Cannot use a \"use strict\" directive in a function with a non-simple parameter list\n";
    
    expectParseError("function f() { 'use strict' }", "");
    expectParseError("function f(x) { 'use strict' }", "");
    expectParseError("function f([x]) { 'use strict' }", nonSimple);
    expectParseError("function f({x}) { 'use strict' }", nonSimple);
    expectParseError("function f(x = 1) { 'use strict' }", nonSimple);
    expectParseError("function f(x, ...y) { 'use strict' }", nonSimple);
    expectParseError("(function() { 'use strict' })", "");
    expectParseError("(function(x) { 'use strict' })", "");
    expectParseError("(function([x]) { 'use strict' })", nonSimple);
    expectParseError("(function({x}) { 'use strict' })", nonSimple);
    expectParseError("(function(x = 1) { 'use strict' })", nonSimple);
    expectParseError("(function(x, ...y) { 'use strict' })", nonSimple);
    expectParseError("() => { 'use strict' }", "");
    expectParseError("(x) => { 'use strict' }", "");
    expectParseError("([x]) => { 'use strict' }", nonSimple);
    expectParseError("({x}) => { 'use strict' }", nonSimple);
    expectParseError("(x = 1) => { 'use strict' }", nonSimple);
    expectParseError("(x, ...y) => { 'use strict' }", nonSimple);
    expectParseError("(x, ...y) => { //! @license comment\n 'use strict' }", nonSimple);

}

TEST(JsParser, TestStrictModeUseStrict) {
    const std::string useStrict = "<stdin>: NOTE: Strict mode is triggered by the \"use strict\" directive here:\n";

    expectParseError("'use strict'; '\\00'", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; '\\08'", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; '\\008'", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'\\00'; 'use strict';", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'\\08'; 'use strict';", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'\\008'; 'use strict';", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; for (var x = y in z) ;",
        "<stdin>: ERROR: Variable initializers inside for-in loops cannot be used in strict mode\n" + useStrict);

    expectParseError("'use strict'; let x = '\\00'", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let x = '\\08'", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let x = '\\008'", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in strict mode\n" + useStrict);

    expectParseError("'use strict'; if (0) function f() {}",
        "<stdin>: ERROR: Function declarations inside if statements cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; if (0) ; else function f() {}",
        "<stdin>: ERROR: Function declarations inside if statements cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; x: function f() {}",
        "<stdin>: ERROR: Function declarations inside labels cannot be used in strict mode\n" + useStrict);
    
    expectParseError("'use strict'; let protected",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let protecte\\u0064",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let x = protected",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let x = protecte\\u0064",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; protected: 0",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; protecte\\u0064: 0",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; function protected() {}",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; function protecte\\u0064() {}",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; (function protected() {})",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; (function protecte\\u0064() {})",
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + useStrict);
    
    expectParseError("'use strict'; 0123",
        "<stdin>: ERROR: Legacy octal literals cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; ({0123: 4})",
        "<stdin>: ERROR: Legacy octal literals cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let {0123: x} = y",
        "<stdin>: ERROR: Legacy octal literals cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; 08",
        "<stdin>: ERROR: Legacy octal literals cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; ({08: 4})",
        "<stdin>: ERROR: Legacy octal literals cannot be used in strict mode\n" + useStrict);
    expectParseError("'use strict'; let {08: x} = y",
        "<stdin>: ERROR: Legacy octal literals cannot be used in strict mode\n" + useStrict);
    
    expectParseError("\"use strict\"; with (x) y", "<stdin>: ERROR: With statements cannot be used in strict mode\n" + useStrict);
    expectParseError("function f() { 'use strict'; with (x) y }", "<stdin>: ERROR: With statements cannot be used in strict mode\n" + useStrict);
    expectParseError("function f() { 'use strict'; function y() { with (x) y } }", "<stdin>: ERROR: With statements cannot be used in strict mode\n" + useStrict);

    expectParseError("'use strict'; delete x", "<stdin>: ERROR: Delete of a bare identifier cannot be used in strict mode\n" + useStrict);

    expectParseError("'use strict'; with (x) y", "<stdin>: ERROR: With statements cannot be used in strict mode\n" + useStrict);

}

TEST(JsParser, TestStrictModeWhy) {
    const std::string why = "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n";

    expectParseError("let x = '\\00'; export {}", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in an ECMAScript module\n" + why);
    expectParseError("let x = '\\09'; export {}", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in an ECMAScript module\n" + why);
    expectParseError("let x = '\\009'; export {}", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in an ECMAScript module\n" + why);

    expectParseError("'\\00'; export {}", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in an ECMAScript module\n" + why);
    expectParseError("'\\09'; export {}", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in an ECMAScript module\n" + why);
    expectParseError("'\\009'; export {}", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in an ECMAScript module\n" + why);

    expectParseError("with (x) y; export {}", "<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n" + why);

    expectParseError("for (var x = y in z) ; export {}",
        "<stdin>: ERROR: Variable initializers inside for-in loops cannot be used in an ECMAScript module\n" + why);

    expectParseError("if (0) function f() {} export {}",
        "<stdin>: ERROR: Function declarations inside if statements cannot be used in an ECMAScript module\n" + why);
    expectParseError("if (0) ; else function f() {} export {}",
        "<stdin>: ERROR: Function declarations inside if statements cannot be used in an ECMAScript module\n" + why);
    expectParseError("x: function f() {} export {}",
        "<stdin>: ERROR: Function declarations inside labels cannot be used in an ECMAScript module\n" + why);

}

TEST(JsParser, TestStrictModeBindingError) {
    const std::string bindingError =
        "<stdin>: ERROR: \"a\" cannot be bound multiple times in the same parameter list\n"
        "<stdin>: NOTE: The name \"a\" was originally bound here:\n";

    expectParseError("function f(a, a) { 'use strict' }", bindingError);
    expectParseError("function *f(a, a) { 'use strict' }", bindingError);
    expectParseError("async function f(a, a) { 'use strict' }", bindingError);
    expectParseError("(function(a, a) { 'use strict' })", bindingError);
    expectParseError("(function*(a, a) { 'use strict' })", bindingError);
    expectParseError("(async function(a, a) { 'use strict' })", bindingError);
    expectParseError("function f(a, [a]) {}", bindingError);
    expectParseError("function f([a], a) {}", bindingError);
    expectParseError("'use strict'; function f(a, a) {}", bindingError);
    expectParseError("'use strict'; (function(a, a) {})", bindingError);
    expectParseError("'use strict'; ((a, a) => {})", bindingError);
    expectParseError("function f(a, a) {}; export {}", bindingError);
    expectParseError("(function(a, a) {}); export {}", bindingError);
    expectParseError("(function(a, [a]) {})", bindingError);
    expectParseError("({ f(a, a) {} })", bindingError);
    expectParseError("({ *f(a, a) {} })", bindingError);
    expectParseError("({ async f(a, a) {} })", bindingError);
    expectParseError("(a, a) => {}", bindingError);
}

TEST(JsParser, TestStrictModeDecl) {
    const std::string useStrict = "<stdin>: NOTE: Strict mode is triggered by the \"use strict\" directive here:\n";
    const std::string evalDecl = "<stdin>: ERROR: Declarations with the name \"eval\" cannot be used in strict mode\n" + useStrict;
    const std::string argsDecl = "<stdin>: ERROR: Declarations with the name \"arguments\" cannot be used in strict mode\n" + useStrict;

    expectPrinted("function eval() {}", "function eval() {\n}\n");
    expectPrinted("function arguments() {}", "function arguments() {\n}\n");
    expectPrinted("function f(eval) {}", "function f(eval) {\n}\n");
    expectPrinted("function f(arguments) {}", "function f(arguments) {\n}\n");
    expectPrinted("({ f(eval) {} })", "({ f(eval) {\n} });\n");
    expectPrinted("({ f(arguments) {} })", "({ f(arguments) {\n} });\n");
    expectParseError("'use strict'; function eval() {}", evalDecl);
    expectParseError("'use strict'; function arguments() {}", argsDecl);
    expectParseError("'use strict'; function f(eval) {}", evalDecl);
    expectParseError("'use strict'; function f(arguments) {}", argsDecl);
    expectParseError("function eval() { 'use strict' }", evalDecl);
    expectParseError("function arguments() { 'use strict' }", argsDecl);
    expectParseError("function f(eval) { 'use strict' }", evalDecl);
    expectParseError("function f(arguments) { 'use strict' }", argsDecl);
    expectParseError("({ f(eval) { 'use strict' } })", evalDecl);
    expectParseError("({ f(arguments) { 'use strict' } })", argsDecl);
    expectParseError("'use strict'; class eval {}", evalDecl);
    expectParseError("'use strict'; class arguments {}", argsDecl);
}


TEST(JsParser, TestStrictMode) {
    expectPrinted("'use strict'", "\"use strict\";\n");
    expectPrinted("`use strict`", "`use strict`;\n");
    expectPrinted("//! @legal comment\n 'use strict'", "\"use strict\";\n//! @legal comment\n");
    expectPrinted("/*! @legal comment */ 'use strict'", "\"use strict\";\n/*! @legal comment */\n");
    expectPrinted("function f() { //! @legal comment\n 'use strict' }", "function f() {\n  //! @legal comment\n  \"use strict\";\n}\n");
    expectPrinted("function f() { /*! @legal comment */ 'use strict' }", "function f() {\n  /*! @legal comment */\n  \"use strict\";\n}\n");
    expectParseError("//! @legal comment\n 'use strict'", "");
    expectParseError("/*! @legal comment */ 'use strict'", "");
    expectParseError("function f() { //! @legal comment\n 'use strict' }", "");
    expectParseError("function f() { /*! @legal comment */ 'use strict' }", "");

   
    expectPrinted("let x = '\\0'", "let x = \"\\0\";\n");
    expectPrinted("let x = '\\00'", "let x = \"\\0\";\n");
    expectPrinted("'use strict'; let x = '\\0'", "\"use strict\";\nlet x = \"\\0\";\n");
    expectPrinted("let x = '\\0'; export {}", "let x = \"\\0\";\nexport {};\n");

    expectPrinted("'\\0'", "\"\\0\";\n");
    expectPrinted("'\\00'", "\"\\0\";\n");
    expectPrinted("'use strict'; '\\0'", "\"use strict\";\n\"\\0\";\n");

    expectPrinted("with (x) y", "with (x) y;\n");
    expectPrinted("delete x", "delete x;\n");
    expectPrinted("for (var x = y in z) ;", "x = y;\nfor (var x in z) ;\n");
    

    expectPrinted("function f(a, a) {}", "function f(a, a) {\n}\n");
    expectPrinted("(function(a, a) {})", "(function(a, a) {\n});\n");
    expectPrinted("({ f: function(a, a) {} })", "({ f: function(a, a) {\n} });\n");
    expectPrinted("({ f: function*(a, a) {} })", "({ f: function* (a, a) {\n} });\n");
    expectPrinted("({ f: async function(a, a) {} })", "({ f: async function(a, a) {\n} });\n");


    expectPrinted("eval++", "eval++;\n");
    expectPrinted("eval = 0", "eval = 0;\n");
    expectPrinted("eval += 0", "eval += 0;\n");
    expectPrinted("[eval] = 0", "[eval] = 0;\n");
    expectPrinted("arguments++", "arguments++;\n");
    expectPrinted("arguments = 0", "arguments = 0;\n");
    expectPrinted("arguments += 0", "arguments += 0;\n");
    expectPrinted("[arguments] = 0", "[arguments] = 0;\n");
    expectParseError("'use strict'; eval++", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; eval = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; eval += 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; [eval] = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; arguments++", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; arguments = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; arguments += 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError("'use strict'; [arguments] = 0", "<stdin>: ERROR: Invalid assignment target\n");

    
    expectPrinted("let protected", "let protected;\n");
    expectPrinted("let protecte\\u0064", "let protected;\n");
    expectPrinted("let x = protected", "let x = protected;\n");
    expectPrinted("let x = protecte\\u0064", "let x = protected;\n");


    expectPrinted("0123", "83;\n");
    expectPrinted("({0123: 4})", "({ 83: 4 });\n");
    expectPrinted("let {0123: x} = y", "let { 83: x } = y;\n");
}

TEST(JsParser, TestStrictModeClassNote) {
    const std::string classNote = "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n";

    expectPrinted("function f() { 'use strict' } with (x) y", "function f() {\n  \"use strict\";\n}\nwith (x) y;\n");
    expectPrinted("with (x) y; function f() { 'use strict' }", "with (x) y;\nfunction f() {\n  \"use strict\";\n}\n");
    expectPrinted("class f {} with (x) y", "class f {\n}\nwith (x) y;\n");
    expectPrinted("with (x) y; class f {}", "with (x) y;\nclass f {\n}\n");
    expectPrinted("`use strict`; with (x) y", "`use strict`;\nwith (x) y;\n");
    expectPrinted("{ 'use strict'; with (x) y }", "{\n  \"use strict\";\n  with (x) y;\n}\n");
    expectPrinted("if (0) { 'use strict'; with (x) y }", "if (0) {\n  \"use strict\";\n  with (x) y;\n}\n");
    expectPrinted("while (0) { 'use strict'; with (x) y }", "while (0) {\n  \"use strict\";\n  with (x) y;\n}\n");
    expectPrinted("try { 'use strict'; with (x) y } catch {}", "try {\n  \"use strict\";\n  with (x) y;\n} catch {\n}\n");
    expectPrinted("try {} catch { 'use strict'; with (x) y }", "try {\n} catch {\n  \"use strict\";\n  with (x) y;\n}\n");
    expectPrinted("try {} finally { 'use strict'; with (x) y }", "try {\n} finally {\n  \"use strict\";\n  with (x) y;\n}\n");

    expectParseError("class f { x() { with (x) y } }", "<stdin>: ERROR: With statements cannot be used in strict mode\n" + classNote);
    expectParseError("class f { x() { function y() { with (x) y } } }", "<stdin>: ERROR: With statements cannot be used in strict mode\n" + classNote);
    expectParseError("class f { x() { function protected() {} } }", "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in strict mode\n" + classNote);
}

TEST(JsParser, TestStrictModeReservedWordExport) {
    const std::string why = "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n";
    const std::string reservedWordExport =
        "<stdin>: ERROR: \"protected\" is a reserved word and cannot be used in an ECMAScript module\n" +
        why;

    expectParseError("var protected; export {}", reservedWordExport);
    expectParseError("class protected {} export {}", reservedWordExport);
    expectParseError("(class protected {}); export {}", reservedWordExport);
    expectParseError("function protected() {} export {}", reservedWordExport);
    expectParseError("(function protected() {}); export {}", reservedWordExport);

}

TEST(JsParser, TestStrictModeImport) {
    const std::string importMeta =
        std::string("<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n") +
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the use of \"import.meta\" here:\n";
    const std::string importStatement =
        std::string("<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n") +
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"import\" keyword here:\n";

    expectPrinted("import(x); with (y) z", "import(x);\nwith (y) z;\n");
    expectPrinted("import('x'); with (y) z", "import(\"x\");\nwith (y) z;\n");
    expectPrinted("with (y) z; import(x)", "with (y) z;\nimport(x);\n");
    expectPrinted("with (y) z; import('x')", "with (y) z;\nimport(\"x\");\n");
    expectPrinted("(import(x)); with (y) z", "import(x);\nwith (y) z;\n");
    expectPrinted("(import('x')); with (y) z", "import(\"x\");\nwith (y) z;\n");
    expectPrinted("with (y) z; (import(x))", "with (y) z;\nimport(x);\n");
    expectPrinted("with (y) z; (import('x'))", "with (y) z;\nimport(\"x\");\n");

    expectParseError("import.meta; with (y) z", importMeta);
    expectParseError("with (y) z; import.meta", importMeta);
    expectParseError("(import.meta); with (y) z", importMeta);
    expectParseError("with (y) z; (import.meta)", importMeta);
    expectParseError("import 'x'; with (y) z", importStatement);
    expectParseError("import * as x from 'x'; with (y) z", importStatement);
    expectParseError("import x from 'x'; with (y) z", importStatement);
    expectParseError("import {x} from 'x'; with (y) z", importStatement);
}

TEST(JsParser, TestStrictModeKeyword) {
    const std::string exportKeyword =
        std::string("<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n") +
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n";
    const std::string tlaKeyword =
        std::string("<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n") +
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n";
    expectParseError("export {}; with (y) z", exportKeyword);
    expectParseError("export let x; with (y) z", exportKeyword);
    expectParseError("export function x() {} with (y) z", exportKeyword);
    expectParseError("export class x {} with (y) z", exportKeyword);

    expectParseError("await 0; with (y) z", tlaKeyword);
    expectParseError("with (y) z; await 0", tlaKeyword);
    expectParseError("for await (x of y); with (y) z", tlaKeyword);
    expectParseError("with (y) z; for await (x of y);", tlaKeyword);
    expectParseError("await using x = _; with (y) z", tlaKeyword);
    expectParseError("with (y) z; await using x = _", tlaKeyword);
    expectParseError("for (await using x of _) ; with (y) z", tlaKeyword);
    expectParseError("with (y) z; for (await using x of _) ;", tlaKeyword);
}

TEST(JsParser, TestStrictModeDeclaredError) {
    const std::string fAlreadyDeclaredError =
        std::string("<stdin>: ERROR: The symbol \"f\" has already been declared\n") +
        "<stdin>: NOTE: The symbol \"f\" was originally declared here:\n";
    const std::string nestedNote = "<stdin>: NOTE: Duplicate function declarations are not allowed in nested blocks";
    const std::string moduleNote =
        std::string("<stdin>: NOTE: Duplicate top-level function declarations are not allowed in an ECMAScript module. ") +
        "This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n";

    const std::vector<std::string> cases = {
        "function f() {} function f() {}",
        "function f() {} function *f() {}",
        "function *f() {} function f() {}",
        "function f() {} async function f() {}",
        "async function f() {} function f() {}",
        "function f() {} async function *f() {}",
        "async function *f() {} function f() {}",
    };

    for (const std::string& c : cases) {
        expectParseError(c, "");
        expectParseError("'use strict'; " + c, "");
        expectParseError("function foo() { 'use strict'; " + c + " }", "");
    }

    expectParseError("function f() {} function f() {} export {}", fAlreadyDeclaredError + moduleNote);
    expectParseError("function f() {} function *f() {} export {}", fAlreadyDeclaredError + moduleNote);
    expectParseError("function f() {} async function f() {} export {}", fAlreadyDeclaredError + moduleNote);
    expectParseError("function *f() {} function f() {} export {}", fAlreadyDeclaredError + moduleNote);
    expectParseError("async function f() {} function f() {} export {}", fAlreadyDeclaredError + moduleNote);

    expectParseError("'use strict'; { function f() {} function f() {} }",
        fAlreadyDeclaredError + nestedNote + " in strict mode. Strict mode is triggered by the \"use strict\" directive here:\n");
    expectParseError("'use strict'; switch (0) { case 1: function f() {} default: function f() {} }",
        fAlreadyDeclaredError + nestedNote + " in strict mode. Strict mode is triggered by the \"use strict\" directive here:\n");

    expectParseError("function foo() { 'use strict'; { function f() {} function f() {} } }",
        fAlreadyDeclaredError + nestedNote + " in strict mode. Strict mode is triggered by the \"use strict\" directive here:\n");
    expectParseError("function foo() { 'use strict'; switch (0) { case 1: function f() {} default: function f() {} } }",
        fAlreadyDeclaredError + nestedNote + " in strict mode. Strict mode is triggered by the \"use strict\" directive here:\n");

    expectParseError("{ function f() {} function f() {} } export {}",
        fAlreadyDeclaredError + nestedNote + " in an ECMAScript module. This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n");
    expectParseError("switch (0) { case 1: function f() {} default: function f() {} } export {}",
        fAlreadyDeclaredError + nestedNote + " in an ECMAScript module. This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n");

    expectParseError("var x; var x", "");
    expectParseError("'use strict'; var x; var x", "");
    expectParseError("var x; var x; export {}", "");
}

TEST(JsParser, TestExponentiation) {
    expectPrinted("--x ** 2", "--x ** 2;\n");
    expectPrinted("++x ** 2", "++x ** 2;\n");
    expectPrinted("x-- ** 2", "x-- ** 2;\n");
    expectPrinted("x++ ** 2", "x++ ** 2;\n");

    expectPrinted("(-x) ** 2", "(-x) ** 2;\n");
    expectPrinted("(+x) ** 2", "(+x) ** 2;\n");
    expectPrinted("(~x) ** 2", "(~x) ** 2;\n");
    expectPrinted("(!x) ** 2", "(!x) ** 2;\n");
    expectPrinted("(-1) ** 2", "(-1) ** 2;\n");
    expectPrinted("(+1) ** 2", "1 ** 2;\n");
    expectPrinted("(~1) ** 2", "(~1) ** 2;\n");
    expectPrinted("(!1) ** 2", "false ** 2;\n");
    expectPrinted("(void x) ** 2", "(void x) ** 2;\n");
    expectPrinted("(delete x) ** 2", "(delete x) ** 2;\n");
    expectPrinted("(typeof x) ** 2", "(typeof x) ** 2;\n");
    expectPrinted("undefined ** 2", "(void 0) ** 2;\n");

    expectParseError("-x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("+x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("~x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("!x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("void x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("delete x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("typeof x ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");

    expectParseError("-x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("+x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("~x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("!x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("void x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("delete x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("typeof x.y() ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");

    expectParseError("delete x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("delete x.prop ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("delete x[0] ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("delete x?.prop ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("void x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("typeof x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("+x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("-x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("~x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("!x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("await x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectParseError("await -x ** 0", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectPrinted("(delete x) ** 0", "(delete x) ** 0;\n");
    expectPrinted("(delete x.prop) ** 0", "(delete x.prop) ** 0;\n");
    expectPrinted("(delete x[0]) ** 0", "(delete x[0]) ** 0;\n");
    expectPrinted("(delete x?.prop) ** 0", "(delete x?.prop) ** 0;\n");
    expectPrinted("(void x) ** 0", "(void x) ** 0;\n");
    expectPrinted("(typeof x) ** 0", "(typeof x) ** 0;\n");
    expectPrinted("(+x) ** 0", "(+x) ** 0;\n");
    expectPrinted("(-x) ** 0", "(-x) ** 0;\n");
    expectPrinted("(~x) ** 0", "(~x) ** 0;\n");
    expectPrinted("(!x) ** 0", "(!x) ** 0;\n");
    expectPrinted("(await x) ** 0", "(await x) ** 0;\n");
    expectPrinted("(await -x) ** 0", "(await -x) ** 0;\n");
}

TEST(JsParser, TestAwait) {
    expectPrinted("await x", "await x;\n");
    expectPrinted("await +x", "await +x;\n");
    expectPrinted("await -x", "await -x;\n");
    expectPrinted("await ~x", "await ~x;\n");
    expectPrinted("await !x", "await !x;\n");
    expectPrinted("await --x", "await --x;\n");
    expectPrinted("await ++x", "await ++x;\n");
    expectPrinted("await x--", "await x--;\n");
    expectPrinted("await x++", "await x++;\n");
    expectPrinted("await void x", "await void x;\n");
    expectPrinted("await typeof x", "await typeof x;\n");
    expectPrinted("await (x * y)", "await (x * y);\n");
    expectPrinted("await (x ** y)", "await (x ** y);\n");

    expectParseError("var { await } = {}", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("async function f() { var { await } = {} }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("async function* f() { var { await } = {} }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("class C { async f() { var { await } = {} } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("class C { async* f() { var { await } = {} } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("class C { static { var { await } = {} } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");

    expectParseError("var {} = { await }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("async function f() { var {} = { await } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("async function* f() { var {} = { await } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("class C { async f() { var {} = { await } } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("class C { async* f() { var {} = { await } } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError("class C { static { var {} = { await } } }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");

    expectParseError("await delete x",
        "<stdin>: ERROR: Delete of a bare identifier cannot be used in an ECMAScript module\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n");
    expectPrinted("async function f() { await delete x }", "async function f() {\n  await delete x;\n}\n");

    const std::string err = "<stdin>: ERROR: Top-level await is not available in the configured target environment\n";
    expectParseErrorWithUnsupportedFeatures(compat::JSFeature::kTopLevelAwait, "await x;", err);
    expectParseErrorWithUnsupportedFeatures(compat::JSFeature::kTopLevelAwait, "if (true) await x;", err);
    expectPrintedWithUnsupportedFeatures(compat::JSFeature::kTopLevelAwait, "if (false) await x;", "if (false) x;\n");
    expectParseErrorWithUnsupportedFeatures(compat::JSFeature::kTopLevelAwait, "with (x) y; if (false) await x;",
        "<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n");
}
TEST(JsParser, TestRegExp) {
    expectPrinted( "/x/d", "/x/d;\n");
    expectPrinted( "/x/g", "/x/g;\n");
    expectPrinted( "/x/i", "/x/i;\n");
    expectPrinted( "/x/m", "/x/m;\n");
    expectPrinted( "/x/s", "/x/s;\n");
    expectPrinted( "/x/u", "/x/u;\n");
    expectPrinted( "/x/y", "/x/y;\n");
    expectParseError( "/)/", "<stdin>: ERROR: Unexpected \")\" in regular expression\n");
    expectPrinted( "/[\\])]/", "/[\\])]/;\n");
    expectParseError( "/x/msuygig",
        "<stdin>: ERROR: Duplicate flag \"g\" in regular expression\n"
        "<stdin>: NOTE: The first \"g\" was here:\n"
        "");
}

TEST(JsParser, TestUnicodeIdentifierNames) {
    // There are two code points that are valid in identifiers in ES5 but not in ES6+:
    //
    //   U+30FB KATAKANA MIDDLE DOT
    //   U+FF65 HALFWIDTH KATAKANA MIDDLE DOT
    //
    expectPrinted( "x = {x・: 0}", "x = { \"x・\": 0 };\n");
    expectPrinted( "x = {x･: 0}", "x = { \"x･\": 0 };\n");
    expectPrinted( "x = {xπ: 0}", "x = { xπ: 0 };\n");
    expectPrinted( "x = y.x・", "x = y[\"x・\"];\n");
    expectPrinted( "x = y.x･", "x = y[\"x･\"];\n");
    expectPrinted( "x = y.xπ", "x = y.xπ;\n");
}

TEST(JsParser, TestIdentifierEscapes) {
    expectPrinted( "var _\\u0076\\u0061\\u0072", "var _var;\n");
    expectParseError( "var \\u0076\\u0061\\u0072", "<stdin>: ERROR: Expected identifier but found \"\\\\u0076\\\\u0061\\\\u0072\"\n");
    expectParseError( "\\u0076\\u0061\\u0072 foo", "<stdin>: ERROR: Unexpected \"\\\\u0076\\\\u0061\\\\u0072\"\n");
    expectPrinted( "foo._\\u0076\\u0061\\u0072", "foo._var;\n");
    expectPrinted( "foo.\\u0076\\u0061\\u0072", "foo.var;\n");
    expectParseError( "\u200Ca", "<stdin>: ERROR: Unexpected \"\\u200c\"\n");
    expectParseError( "\u200Da", "<stdin>: ERROR: Unexpected \"\\u200d\"\n");
}

TEST(JsParser, TestSpecialIdentifiers) {
    expectPrinted( "exports", "exports;\n");
    expectPrinted( "require", "require;\n");
    expectPrinted( "module", "module;\n");
}

TEST(JsParser, TestDecls) {
    expectParseError( "var x = 0", "");
    expectParseError( "let x = 0", "");
    expectParseError( "const x = 0", "");
    expectParseError( "for (var x = 0;;) ;", "");
    expectParseError( "for (let x = 0;;) ;", "");
    expectParseError( "for (const x = 0;;) ;", "");
    expectParseError( "for (var x in y) ;", "");
    expectParseError( "for (let x in y) ;", "");
    expectParseError( "for (const x in y) ;", "");
    expectParseError( "for (var x of y) ;", "");
    expectParseError( "for (let x of y) ;", "");
    expectParseError( "for (const x of y) ;", "");
    expectParseError( "var x", "");
    expectParseError( "let x", "");
    expectParseError( "const x", "<stdin>: ERROR: The constant \"x\" must be initialized\n");
    expectParseError( "const {}", "<stdin>: ERROR: This constant must be initialized\n");
    expectParseError( "const []", "<stdin>: ERROR: This constant must be initialized\n");
    expectParseError( "for (var x;;) ;", "");
    expectParseError( "for (let x;;) ;", "");
    expectParseError( "for (const x;;) ;", "<stdin>: ERROR: The constant \"x\" must be initialized\n");
    expectParseError( "for (const {};;) ;", "<stdin>: ERROR: This constant must be initialized\n");
    expectParseError( "for (const [];;) ;", "<stdin>: ERROR: This constant must be initialized\n");
    // Make sure bindings are visited during parsing
    expectPrinted( "var {[x]: y} = {}", "var { [x]: y } = {};\n");
    expectPrinted( "var {...x} = {}", "var { ...x } = {};\n");
    // Test destructuring patterns
    expectPrinted( "var [...x] = []", "var [...x] = [];\n");
    expectPrinted( "var {...x} = {}", "var { ...x } = {};\n");
    expectPrinted( "([...x] = []) => {}", "([...x] = []) => {\n};\n");
    expectPrinted( "({...x} = {}) => {}", "({ ...x } = {}) => {\n};\n");
    expectParseError( "var [...x,] = []", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "var {...x,} = {}", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "([...x,] = []) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "({...x,} = {}) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectPrinted( "[b, ...c] = d", "[b, ...c] = d;\n");
    expectPrinted( "([b, ...c] = d)", "[b, ...c] = d;\n");
    expectPrinted( "({b, ...c} = d)", "({ b, ...c } = d);\n");
    expectPrinted( "({a = b} = c)", "({ a = b } = c);\n");
    expectPrinted( "({a: b = c} = d)", "({ a: b = c } = d);\n");
    expectPrinted( "({a: b.c} = d)", "({ a: b.c } = d);\n");
    expectPrinted( "[a = {}] = b", "[a = {}] = b;\n");
    expectPrinted( "[[...a, b].x] = c", "[[...a, b].x] = c;\n");
    expectPrinted( "[{...a, b}.x] = c", "[{ ...a, b }.x] = c;\n");
    expectPrinted( "({x: [...a, b].x} = c)", "({ x: [...a, b].x } = c);\n");
    expectPrinted( "({x: {...a, b}.x} = c)", "({ x: { ...a, b }.x } = c);\n");
    expectPrinted( "[x = [...a, b]] = c", "[x = [...a, b]] = c;\n");
    expectPrinted( "[x = {...a, b}] = c", "[x = { ...a, b }] = c;\n");
    expectPrinted( "({x = [...a, b]} = c)", "({ x = [...a, b] } = c);\n");
    expectPrinted( "({x = {...a, b}} = c)", "({ x = { ...a, b } } = c);\n");
    expectPrinted( "(x = y)", "x = y;\n");
    expectPrinted( "([] = [])", "[] = [];\n");
    expectPrinted( "({} = {})", "({} = {});\n");
    expectPrinted( "([[]] = [[]])", "[[]] = [[]];\n");
    expectPrinted( "({x: {}} = {x: {}})", "({ x: {} } = { x: {} });\n");
    expectPrinted( "(x) = y", "x = y;\n");
    expectParseError( "([]) = []", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "({}) = {}", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "[([])] = [[]]", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "({x: ({})} = {x: {}})", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "(([]) = []) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "(({}) = {}) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "function f(([]) = []) {}", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "function f(({}) = {}) {}", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectPrinted( "for (x in y) ;", "for (x in y) ;\n");
    expectPrinted( "for ([] in y) ;", "for ([] in y) ;\n");
    expectPrinted( "for ({} in y) ;", "for ({} in y) ;\n");
    expectPrinted( "for ((x) in y) ;", "for (x in y) ;\n");
    expectParseError( "for (([]) in y) ;", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "for (({}) in y) ;", "<stdin>: ERROR: Invalid assignment target\n");
    expectPrinted( "for (x of y) ;", "for (x of y) ;\n");
    expectPrinted( "for ([] of y) ;", "for ([] of y) ;\n");
    expectPrinted( "for ({} of y) ;", "for ({} of y) ;\n");
    expectPrinted( "for ((x) of y) ;", "for (x of y) ;\n");
    expectParseError( "for (([]) of y) ;", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "for (({}) of y) ;", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "[[...a, b]] = c", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "[{...a, b}] = c", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "({x: [...a, b]} = c)", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "({x: {...a, b}} = c)", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "[b, ...c,] = d", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "([b, ...c,] = d)", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "({b, ...c,} = d)", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "({a = b})", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectParseError( "({x = {a = b}} = c)", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectParseError( "[a = {b = c}] = d", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectPrinted( "for ([{a = {}}] in b) {}", "for ([{ a = {} }] in b) {\n}\n");
    expectPrinted( "for ([{a = {}}] of b) {}", "for ([{ a = {} }] of b) {\n}\n");
    expectPrinted( "for ({a = {}} in b) {}", "for ({ a = {} } in b) {\n}\n");
    expectPrinted( "for ({a = {}} of b) {}", "for ({ a = {} } of b) {\n}\n");
    expectParseError( "({a = {}} in b)", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectParseError( "[{a = {}}]\nof()", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectParseError( "for ([...a, b] in c) {}", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "for ([...a, b] of c) {}", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
}

TEST(JsParser, TestBreakAndContinue) {
    expectParseError( "break", "<stdin>: ERROR: Cannot use \"break\" here:\n");
    expectParseError( "continue", "<stdin>: ERROR: Cannot use \"continue\" here:\n");
    expectParseError( "x: { break }", "<stdin>: ERROR: Cannot use \"break\" here:\n");
    expectParseError( "x: { break x }", "");
    expectParseError( "x: { continue }", "<stdin>: ERROR: Cannot use \"continue\" here:\n");
    expectParseError( "x: { continue x }", "<stdin>: ERROR: Cannot continue to label \"x\"\n");
    expectParseError( "while (1) break", "");
    expectParseError( "while (1) continue", "");
    expectParseError( "while (1) { function foo() { break } }", "<stdin>: ERROR: Cannot use \"break\" here:\n");
    expectParseError( "while (1) { function foo() { continue } }", "<stdin>: ERROR: Cannot use \"continue\" here:\n");
    expectParseError( "x: while (1) break x", "");
    expectParseError( "x: while (1) continue x", "");
    expectParseError( "x: while (1) y: { break x }", "");
    expectParseError( "x: while (1) y: { continue x }", "");
    expectParseError( "x: while (1) y: { break y }", "");
    expectParseError( "x: while (1) y: { continue y }", "<stdin>: ERROR: Cannot continue to label \"y\"\n");
    expectParseError( "x: while (1) { function foo() { break x } }", "<stdin>: ERROR: There is no containing label named \"x\"\n");
    expectParseError( "x: while (1) { function foo() { continue x } }", "<stdin>: ERROR: There is no containing label named \"x\"\n");
    expectParseError( "switch (1) { case 1: break }", "");
    expectParseError( "switch (1) { case 1: continue }", "<stdin>: ERROR: Cannot use \"continue\" here:\n");
    expectParseError( "x: switch (1) { case 1: break x }", "");
    expectParseError( "x: switch (1) { case 1: continue x }", "<stdin>: ERROR: Cannot continue to label \"x\"\n");
}

TEST(JsParser, TestFor) {
    expectParseError( "for (; in x) ;", "<stdin>: ERROR: Unexpected \"in\"\n");
    expectParseError( "for (; of x) ;", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "for (; in; ) ;", "<stdin>: ERROR: Unexpected \"in\"\n");
    expectPrinted( "for (; of; ) ;", "for (; of; ) ;\n");
    expectPrinted( "for (a in b) ;", "for (a in b) ;\n");
    expectPrinted( "for (var a in b) ;", "for (var a in b) ;\n");
    expectPrinted( "for (let a in b) ;", "for (let a in b) ;\n");
    expectPrinted( "for (const a in b) ;", "for (const a in b) ;\n");
    expectPrinted( "for (a in b, c) ;", "for (a in b, c) ;\n");
    expectPrinted( "for (a in b = c) ;", "for (a in b = c) ;\n");
    expectPrinted( "for (var a in b, c) ;", "for (var a in b, c) ;\n");
    expectPrinted( "for (var a in b = c) ;", "for (var a in b = c) ;\n");
    expectParseError( "for (var a, b in b) ;", "<stdin>: ERROR: for-in loops must have a single declaration\n");
    expectParseError( "for (let a, b in b) ;", "<stdin>: ERROR: for-in loops must have a single declaration\n");
    expectParseError( "for (const a, b in b) ;", "<stdin>: ERROR: for-in loops must have a single declaration\n");
    expectPrinted( "for (a of b) ;", "for (a of b) ;\n");
    expectPrinted( "for (var a of b) ;", "for (var a of b) ;\n");
    expectPrinted( "for (let a of b) ;", "for (let a of b) ;\n");
    expectPrinted( "for (const a of b) ;", "for (const a of b) ;\n");
    expectPrinted( "for (a of b = c) ;", "for (a of b = c) ;\n");
    expectPrinted( "for (var a of b = c) ;", "for (var a of b = c) ;\n");
    expectParseError( "for (a of b, c) ;", "<stdin>: ERROR: Expected \")\" but found \",\"\n");
    expectParseError( "for (var a of b, c) ;", "<stdin>: ERROR: Expected \")\" but found \",\"\n");
    expectParseError( "for (var a, b of b) ;", "<stdin>: ERROR: for-of loops must have a single declaration\n");
    expectParseError( "for (let a, b of b) ;", "<stdin>: ERROR: for-of loops must have a single declaration\n");
    expectParseError( "for (const a, b of b) ;", "<stdin>: ERROR: for-of loops must have a single declaration\n");
    // Avoid the initializer starting with "let" token
    expectPrinted( "for ((let) of bar);", "for ((let) of bar) ;\n");
    expectPrinted( "for ((let).foo of bar);", "for ((let).foo of bar) ;\n");
    expectPrinted( "for ((let.foo) of bar);", "for ((let).foo of bar) ;\n");
    expectPrinted( "for ((let``.foo) of bar);", "for ((let)``.foo of bar) ;\n");
    expectParseError( "for (let.foo of bar);", "<stdin>: ERROR: \"let\" must be wrapped in parentheses to be used as an expression here:\n");
    expectParseError( "for (let().foo of bar);", "<stdin>: ERROR: \"let\" must be wrapped in parentheses to be used as an expression here:\n");
    expectParseError( "for (let``.foo of bar);", "<stdin>: ERROR: \"let\" must be wrapped in parentheses to be used as an expression here:\n");
    expectPrinted( "for (var x = 0 in y) ;", "x = 0;\nfor (var x in y) ;\n");// This is a weird special-case
    expectParseError( "for (let x = 0 in y) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (const x = 0 in y) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (var x = 0 of y) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (let x = 0 of y) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (const x = 0 of y) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (var [x] = y in z) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (let [x] = y in z) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (const [x] = y in z) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (var [x] = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (let [x] = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (const [x] = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (var {x} = y in z) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (let {x} = y in z) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (const {x} = y in z) ;", "<stdin>: ERROR: for-in loop variables cannot have an initializer\n");
    expectParseError( "for (var {x} = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (let {x} = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (const {x} = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    // Make sure "in" rules are enabled
    expectPrinted( "for (var x = () => a in b);", "x = () => a;\nfor (var x in b) ;\n");
    expectPrinted( "for (var x = a + b in c);", "x = a + b;\nfor (var x in c) ;\n");
    // Make sure "in" rules are disabled
    expectPrinted( "for (var x = `${y in z}`;;);", "for (var x = `${y in z}`; ; ) ;\n");
    expectPrinted( "for (var {[x in y]: z} = {};;);", "for (var { [x in y]: z } = {}; ; ) ;\n");
    expectPrinted( "for (var {x = y in z} = {};;);", "for (var { x = y in z } = {}; ; ) ;\n");
    expectPrinted( "for (var [x = y in z] = {};;);", "for (var [x = y in z] = {}; ; ) ;\n");
    expectPrinted( "for (var {x: y = z in w} = {};;);", "for (var { x: y = z in w } = {}; ; ) ;\n");
    expectPrinted( "for (var x = (a in b);;);", "for (var x = (a in b); ; ) ;\n");
    expectPrinted( "for (var x = [a in b];;);", "for (var x = [a in b]; ; ) ;\n");
    expectPrinted( "for (var x = y(a in b);;);", "for (var x = y(a in b); ; ) ;\n");
    expectPrinted( "for (var x = {y: a in b};;);", "for (var x = { y: a in b }; ; ) ;\n");
    expectPrinted( "for (a ? b in c : d;;);", "for (a ? b in c : d; ; ) ;\n");
    expectPrinted( "for (var x = () => { a in b };;);", "for (var x = () => {\n  a in b;\n}; ; ) ;\n");
    expectPrinted( "for (var x = async () => { a in b };;);", "for (var x = async () => {\n  a in b;\n}; ; ) ;\n");
    expectPrinted( "for (var x = function() { a in b };;);", "for (var x = function() {\n  a in b;\n}; ; ) ;\n");
    expectPrinted( "for (var x = async function() { a in b };;);", "for (var x = async function() {\n  a in b;\n}; ; ) ;\n");
    expectPrinted( "for (var x = class { [a in b]() {} };;);", "for (var x = class {\n  [a in b]() {\n  }\n}; ; ) ;\n");
    expectParseError( "for (var x = class extends a in b {};;);", "<stdin>: ERROR: Expected \"{\" but found \"in\"\n");
    const std::string errorText = "<stdin>: WARNING: This assignment will throw because \"x\" is a constant\n"
        "<stdin>: NOTE: The symbol \"x\" was declared a constant here:\n"
        "";
    expectParseError( "for (var x = 0; ; x = 1) ;", "");
    expectParseError( "for (let x = 0; ; x = 1) ;", "");
    expectParseError( "for (const x = 0; ; x = 1) ;", errorText);
    expectParseError( "for (var x = 0; ; x++) ;", "");
    expectParseError( "for (let x = 0; ; x++) ;", "");
    expectParseError( "for (const x = 0; ; x++) ;", errorText);
    expectParseError( "for (var x in y) x = 1", "");
    expectParseError( "for (let x in y) x = 1", "");
    expectParseError( "for (const x in y) x = 1", errorText);
    expectParseError( "for (var x in y) x++", "");
    expectParseError( "for (let x in y) x++", "");
    expectParseError( "for (const x in y) x++", errorText);
    expectParseError( "for (var x of y) x = 1", "");
    expectParseError( "for (let x of y) x = 1", "");
    expectParseError( "for (const x of y) x = 1", errorText);
    expectParseError( "for (var x of y) x++", "");
    expectParseError( "for (let x of y) x++", "");
    expectParseError( "for (const x of y) x++", errorText);
    expectPrinted( "async of => {}", "async (of) => {\n};\n");
    expectPrinted( "for ((async) of []) ;", "for ((async) of []) ;\n");
    expectPrinted( "for (async.x of []) ;", "for (async.x of []) ;\n");
    expectPrinted( "for (async of => {};;) ;", "for (async (of) => {\n}; ; ) ;\n");
    expectPrinted( "for (\\u0061sync of []) ;", "for ((async) of []) ;\n");
    expectPrinted( "for await (async of []) ;", "for await (async of []) ;\n");
    expectParseError( "for (async of []) ;", "<stdin>: ERROR: For loop initializers cannot start with \"async of\"\n");
    expectParseError( "for (async o\\u0066 []) ;", "<stdin>: ERROR: Expected \";\" but found \"o\\\\u0066\"\n");
    expectParseError( "for await (async of => {}) ;", "<stdin>: ERROR: Expected \"of\" but found \")\"\n");
    expectParseError( "for await (async of => {} of []) ;", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "for await (async o\\u0066 []) ;", "<stdin>: ERROR: Expected \"of\" but found \"o\\\\u0066\"\n");
    // Can't use await at the top-level without top-level await
    const std::string err = "<stdin>: ERROR: Top-level await is not available in the configured target environment\n";
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "for await (x of y);", err);
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "if (true) for await (x of y);", err);
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "if (false) for await (x of y);", "if (false) for (x of y) ;\n");
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "with (x) y; if (false) for await (x of y);",
        "<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n" "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n");
}

TEST(JsParser, TestScope) {
    const std::string errorText = "<stdin>: ERROR: The symbol \"x\" has already been declared\n"
        "<stdin>: NOTE: The symbol \"x\" was originally declared here:\n"
        "";
    expectParseError( "var x; var y", "");
    expectParseError( "var x; let y", "");
    expectParseError( "let x; var y", "");
    expectParseError( "let x; let y", "");
    expectParseError( "var x; var x", "");
    expectParseError( "var x; let x", errorText);
    expectParseError( "let x; var x", errorText);
    expectParseError( "let x; let x", errorText);
    expectParseError( "function x() {} let x", errorText);
    expectParseError( "let x; function x() {}", errorText);
    expectParseError( "var x; {var x}", "");
    expectParseError( "var x; {let x}", "");
    expectParseError( "let x; {var x}", errorText);
    expectParseError( "let x; {let x}", "");
    expectParseError( "let x; {function x() {}}", "");
    expectParseError( "{var x} var x", "");
    expectParseError( "{var x} let x", errorText);
    expectParseError( "{let x} var x", "");
    expectParseError( "{let x} let x", "");
    expectParseError( "{function x() {}} let x", "");
    expectParseError( "{var x; {var x}}", "");
    expectParseError( "{var x; {let x}}", "");
    expectParseError( "{let x; {var x}}", errorText);
    expectParseError( "{let x; {let x}}", "");
    expectParseError( "{let x; {function x() {}}}", "");
    expectParseError( "{{var x} var x}", "");
    expectParseError( "{{var x} let x}", errorText);
    expectParseError( "{{let x} var x}", "");
    expectParseError( "{{let x} let x}", "");
    expectParseError( "{{function x() {}} let x}", "");
    expectParseError( "{var x} {var x}", "");
    expectParseError( "{var x} {let x}", "");
    expectParseError( "{let x} {var x}", "");
    expectParseError( "{let x} {let x}", "");
    expectParseError( "{let x} {function x() {}}", "");
    expectParseError( "{function x() {}} {let x}", "");
    expectParseError( "function x() {} {var x}", "");
    expectParseError( "function *x() {} {var x}", "");
    expectParseError( "async function x() {} {var x}", "");
    expectParseError( "async function *x() {} {var x}", "");
    expectParseError( "{var x} function x() {}", "");
    expectParseError( "{var x} function *x() {}", "");
    expectParseError( "{var x} async function x() {}", "");
    expectParseError( "{var x} async function *x() {}", "");
    expectParseError( "{ function x() {} {var x} }", errorText);
    expectParseError( "{ function *x() {} {var x} }", errorText);
    expectParseError( "{ async function x() {} {var x} }", errorText);
    expectParseError( "{ async function *x() {} {var x} }", errorText);
    expectParseError( "{ {var x} function x() {} }", errorText);
    expectParseError( "{ {var x} function *x() {} }", errorText);
    expectParseError( "{ {var x} async function x() {} }", errorText);
    expectParseError( "{ {var x} async function *x() {} }", errorText);
    expectParseError( "function f() { function x() {} {var x} }", "");
    expectParseError( "function f() { function *x() {} {var x} }", "");
    expectParseError( "function f() { async function x() {} {var x} }", "");
    expectParseError( "function f() { async function *x() {} {var x} }", "");
    expectParseError( "function f() { {var x} function x() {} }", "");
    expectParseError( "function f() { {var x} function *x() {} }", "");
    expectParseError( "function f() { {var x} async function x() {} }", "");
    expectParseError( "function f() { {var x} async function *x() {} }", "");
    expectParseError( "function f() { { function x() {} {var x} } }", errorText);
    expectParseError( "function f() { { function *x() {} {var x} } }", errorText);
    expectParseError( "function f() { { async function x() {} {var x} } }", errorText);
    expectParseError( "function f() { { async function *x() {} {var x} } }", errorText);
    expectParseError( "function f() { { {var x} function x() {} } }", errorText);
    expectParseError( "function f() { { {var x} function *x() {} } }", errorText);
    expectParseError( "function f() { { {var x} async function x() {} } }", errorText);
    expectParseError( "function f() { { {var x} async function *x() {} } }", errorText);
    expectParseError( "var x=1, x=2", "");
    expectParseError( "let x=1, x=2", errorText);
    expectParseError( "const x=1, x=2", errorText);
    expectParseError( "function foo(x) { var x }", "");
    expectParseError( "function foo(x) { let x }", errorText);
    expectParseError( "function foo(x) { const x = 0 }", errorText);
    expectParseError( "function foo() { var foo }", "");
    expectParseError( "function foo() { let foo }", "");
    expectParseError( "function foo() { const foo = 0 }", "");
    expectParseError( "(function foo(x) { var x })", "");
    expectParseError( "(function foo(x) { let x })", errorText);
    expectParseError( "(function foo(x) { const x = 0 })", errorText);
    expectParseError( "(function foo() { var foo })", "");
    expectParseError( "(function foo() { let foo })", "");
    expectParseError( "(function foo() { const foo = 0 })", "");
    expectParseError( "var x; function x() {}", "");
    expectParseError( "var x; function *x() {}", "");
    expectParseError( "var x; async function x() {}", "");
    expectParseError( "let x; function x() {}", errorText);
    expectParseError( "function x() {} var x", "");
    expectParseError( "function* x() {} var x", "");
    expectParseError( "async function x() {} var x", "");
    expectParseError( "function x() {} let x", errorText);
    expectParseError( "function x() {} function x() {}", "");
    expectParseError( "var x; class x {}", errorText);
    expectParseError( "let x; class x {}", errorText);
    expectParseError( "class x {} var x", errorText);
    expectParseError( "class x {} let x", errorText);
    expectParseError( "class x {} class x {}", errorText);
    expectParseError( "function x() {} function x() {}", "");
    expectParseError( "function x() {} function *x() {}", "");
    expectParseError( "function x() {} async function x() {}", "");
    expectParseError( "function *x() {} function x() {}", "");
    expectParseError( "function *x() {} function *x() {}", "");
    expectParseError( "async function x() {} function x() {}", "");
    expectParseError( "async function x() {} async function x() {}", "");
    expectParseError( "function f() { function x() {} function x() {} }", "");
    expectParseError( "function f() { function x() {} function *x() {} }", "");
    expectParseError( "function f() { function x() {} async function x() {} }", "");
    expectParseError( "function f() { function *x() {} function x() {} }", "");
    expectParseError( "function f() { function *x() {} function *x() {} }", "");
    expectParseError( "function f() { async function x() {} function x() {} }", "");
    expectParseError( "function f() { async function x() {} async function x() {} }", "");
    const std::string text = "<stdin>: ERROR: The symbol \"x\" has already been declared\n<stdin>: NOTE: The symbol \"x\" was originally declared here:\n";
    for (const std::string& scope : std::vector<std::string>{"", "with (x)", "while (x)", "if (x)"}) {
        expectParseError( scope+"{ function x() {} function x() {} }", "");
        expectParseError( scope+"{ function x() {} function *x() {} }", text);
        expectParseError( scope+"{ function x() {} async function x() {} }", text);
        expectParseError( scope+"{ function *x() {} function x() {} }", text);
        expectParseError( scope+"{ function *x() {} function *x() {} }", text);
        expectParseError( scope+"{ async function x() {} function x() {} }", text);
        expectParseError( scope+"{ async function x() {} async function x() {} }", text);
    }
}

TEST(JsParser, TestASI) {
    expectParseError( "throw\n0", "<stdin>: ERROR: Unexpected newline after \"throw\"\n");
    expectParseError( "return\n0", "<stdin>: WARNING: The following expression is not returned because of an automatically-inserted semicolon\n");
    expectPrinted( "return\n0", "return;\n0;\n");
    expectPrinted( "0\n[1]", "0[1];\n");
    expectPrinted( "0\n(1)", "0(1);\n");
    expectPrinted( "new x\n(1)", "new x(1);\n");
    expectPrinted( "while (true) break\nx", "while (true) break;\nx;\n");
    expectPrinted( "x\n!y", "x;\n!y;\n");
    expectPrinted( "x\n++y", "x;\n++y;\n");
    expectPrinted( "x\n--y", "x;\n--y;\n");
    expectPrinted( "function* foo(){yield\na}", "function* foo() {\n  yield;\n  a;\n}\n");
    expectParseError( "function* foo(){yield\n*a}", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectPrinted( "function* foo(){yield*\na}", "function* foo() {\n  yield* a;\n}\n");
    expectPrinted( "async\nx => {}", "async;\n(x) => {\n};\n");
    expectPrinted( "async\nfunction foo() {}", "async;\nfunction foo() {\n}\n");
    expectPrinted( "export default async\nx => {}", "export default async;\n(x) => {\n};\n");
    expectPrinted( "export default async\nfunction foo() {}", "export default async;\nfunction foo() {\n}\n");
    expectParseError( "async\n() => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseError( "export async\nfunction foo() {}", "<stdin>: ERROR: Unexpected newline after \"async\"\n");
    expectParseError( "export default async\n() => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseError( "(async\nx => {})", "<stdin>: ERROR: Expected \")\" but found \"x\"\n");
    expectParseError( "(async\n() => {})", "<stdin>: ERROR: Expected \")\" but found \"=>\"\n");
    expectParseError( "(async\nfunction foo() {})", "<stdin>: ERROR: Expected \")\" but found \"function\"\n");
    expectPrinted( "if (0) let\nx = 0", "if (0) let;\nx = 0;\n");
    expectPrinted( "if (0) let\n{x}", "if (0) let;\n{\n  x;\n}\n");
    expectParseError( "if (0) let\n{x} = 0", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectParseError( "if (0) let\n[x] = 0", "<stdin>: ERROR: Cannot use a declaration in a single-statement context\n" "NOTE: Wrap this declaration in a block statement to use it here.\n");
    expectPrinted( "function *foo() { if (0) let\nyield 0 }", "function* foo() {\n  if (0) let;\n  yield 0;\n}\n");
    expectPrinted( "async function foo() { if (0) let\nawait 0 }", "async function foo() {\n  if (0) let;\n  await 0;\n}\n");
    expectPrinted( "let\nx = 0", "let x = 0;\n");
    expectPrinted( "let\n{x} = 0", "let { x } = 0;\n");
    expectPrinted( "let\n[x] = 0", "let [x] = 0;\n");
    expectParseError( "function *foo() { let\nyield 0 }",
        "<stdin>: ERROR: Cannot use \"yield\" as an identifier here:\n<stdin>: ERROR: Expected \";\" but found \"0\"\n");
    expectParseError( "async function foo() { let\nawait 0 }",
        "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n<stdin>: ERROR: Expected \";\" but found \"0\"\n");
    // This is a weird corner case where ASI applies without a newline
    expectPrinted( "do x;while(y)z", "do\n  x;\nwhile (y);\nz;\n");
    expectPrinted( "do x;while(y);z", "do\n  x;\nwhile (y);\nz;\n");
    expectPrinted( "{do x;while(y)}", "{\n  do\n    x;\n  while (y);\n}\n");
}

TEST(JsParser, TestLocal) {
    expectPrinted( "var let = 0", "var let = 0;\n");
    expectParseError( "let let = 0", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectParseError( "const let = 0", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectPrinted( "var\nlet = 0", "var let = 0;\n");
    expectParseError( "let\nlet = 0", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectParseError( "const\nlet = 0", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectPrinted( "for (var let in x) ;", "for (var let in x) ;\n");
    expectParseError( "for (let let in x) ;", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectParseError( "for (const let in x) ;", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectPrinted( "for (var let of x) ;", "for (var let of x) ;\n");
    expectParseError( "for (let let of x) ;", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    expectParseError( "for (const let of x) ;", "<stdin>: ERROR: Cannot use \"let\" as an identifier here:\n");
    const std::string errorText = "<stdin>: WARNING: This assignment will throw because \"x\" is a constant\n"
        "<stdin>: NOTE: The symbol \"x\" was declared a constant here:\n"
        "";
    expectParseError( "var x = 0; x = 1", "");
    expectParseError( "let x = 0; x = 1", "");
    expectParseError( "const x = 0; x = 1", errorText);
    expectParseError( "var x = 0; x++", "");
    expectParseError( "let x = 0; x++", "");
    expectParseError( "const x = 0; x++", errorText);
}

TEST(JsParser, TestArrays) {
    expectPrinted( "[]", "[];\n");
    expectPrinted( "[,]", "[,];\n");
    expectPrinted( "[1]", "[1];\n");
    expectPrinted( "[1,]", "[1];\n");
    expectPrinted( "[,1]", "[, 1];\n");
    expectPrinted( "[1,2]", "[1, 2];\n");
    expectPrinted( "[,1,2]", "[, 1, 2];\n");
    expectPrinted( "[1,,2]", "[1, , 2];\n");
    expectPrinted( "[1,2,]", "[1, 2];\n");
    expectPrinted( "[1,2,,]", "[1, 2, ,];\n");
}

TEST(JsParser, TestPattern) {
    expectPrinted( "let {if: x} = y", "let { if: x } = y;\n");
    expectParseError( "let {x: if} = y", "<stdin>: ERROR: Expected identifier but found \"if\"\n");
    expectPrinted( "let {1_2_3n: x} = y", "let { 123n: x } = y;\n");
    expectPrinted( "let {0x1_2_3n: x} = y", "let { 0x123n: x } = y;\n");
    expectParseError( "var [ (x) ] = 0", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "var [ ...(x) ] = 0", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "var { (x) } = 0", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "var { x: (y) } = 0", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "var { ...(x) } = 0", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
}

TEST(JsParser, TestAssignTarget) {
    expectParseError( "x = 0", "");
    expectParseError( "x.y = 0", "");
    expectParseError( "x[y] = 0", "");
    expectParseError( "[,] = 0", "");
    expectParseError( "[x] = 0", "");
    expectParseError( "[x = y] = 0", "");
    expectParseError( "[...x] = 0", "");
    expectParseError( "({...x} = 0)", "");
    expectParseError( "({x = 0} = 0)", "");
    expectParseError( "({x: y = 0} = 0)", "");
    expectParseError( "[ (y) ] = 0", "");
    expectParseError( "[ ...(y) ] = 0", "");
    expectParseError( "({ (y) } = 0)", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "({ y: (z) } = 0)", "");
    expectParseError( "({ ...(y) } = 0)", "");
    expectParseError( "[...x = y] = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "x() = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "x?.y = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "x?.[y] = 0", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "({x: 0} = 0)", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "({x() {}} = 0)", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "({x: 0 = y} = 0)", "<stdin>: ERROR: Invalid assignment target\n");
}

TEST(JsParser, TestObject) {
    expectPrinted( "({foo})", "({ foo });\n");
    expectPrinted( "({foo:0})", "({ foo: 0 });\n");
    expectPrinted( "({1e9:0})", "({ 1e9: 0 });\n");
    expectPrinted( "({1_2_3n:0})", "({ 123n: 0 });\n");
    expectPrinted( "({0x1_2_3n:0})", "({ 0x123n: 0 });\n");
    expectPrinted( "({foo() {}})", "({ foo() {\n} });\n");
    expectPrinted( "({*foo() {}})", "({ *foo() {\n} });\n");
    expectPrinted( "({get foo() {}})", "({ get foo() {\n} });\n");
    expectPrinted( "({set foo(x) {}})", "({ set foo(x) {\n} });\n");
    expectPrinted( "({if:0})", "({ if: 0 });\n");
    expectPrinted( "({if() {}})", "({ if() {\n} });\n");
    expectPrinted( "({*if() {}})", "({ *if() {\n} });\n");
    expectPrinted( "({get if() {}})", "({ get if() {\n} });\n");
    expectPrinted( "({set if(x) {}})", "({ set if(x) {\n} });\n");
    expectParseError( "({static foo() {}})", "<stdin>: ERROR: Expected \"}\" but found \"foo\"\n");
    expectParseError( "({`a`})", "<stdin>: ERROR: Expected identifier but found \"`a`\"\n");
    expectParseError( "({if})", "<stdin>: ERROR: Expected \":\" but found \"}\"\n");
    const std::string protoError = "<stdin>: ERROR: Cannot specify the \"__proto__\" property more than once per object\n" "<stdin>: NOTE: The earlier \"__proto__\" property is here:\n";
    expectParseError( "({__proto__: 1, __proto__: 2})", protoError);
    expectParseError( "({__proto__: 1, '__proto__': 2})", protoError);
    expectParseError( "({__proto__: 1, __proto__() {}})", "");
    expectParseError( "({__proto__: 1, get __proto__() {}})", "");
    expectParseError( "({__proto__: 1, set __proto__(x) {}})", "");
    expectParseError( "({__proto__: 1, ['__proto__']: 2})", "");
    expectParseError( "({__proto__, __proto__: 2})", "");
    expectParseError( "({__proto__: x, __proto__: y} = z)", "");
    expectPrintedMangle( "x = {['_proto_']: x}", "x = { _proto_: x };\n");
    expectPrintedMangle( "x = {['__proto__']: x}", "x = { [\"__proto__\"]: x };\n");
    expectParseError( "({set foo() {}})", "<stdin>: ERROR: Setter \"foo\" must have exactly one argument\n");
    expectParseError( "({get foo(x) {}})", "<stdin>: ERROR: Getter \"foo\" must have zero arguments\n");
    expectParseError( "({set foo(x, y) {}})", "<stdin>: ERROR: Setter \"foo\" must have exactly one argument\n");
    expectParseError( "(class {set #foo() {}})", "<stdin>: ERROR: Setter \"#foo\" must have exactly one argument\n");
    expectParseError( "(class {get #foo(x) {}})", "<stdin>: ERROR: Getter \"#foo\" must have zero arguments\n");
    expectParseError( "(class {set #foo(x, y) {}})", "<stdin>: ERROR: Setter \"#foo\" must have exactly one argument\n");
    expectParseError( "({set [foo]() {}})", "<stdin>: ERROR: Setter property must have exactly one argument\n");
    expectParseError( "({get [foo](x) {}})", "<stdin>: ERROR: Getter property must have zero arguments\n");
    expectParseError( "({set [foo](x, y) {}})", "<stdin>: ERROR: Setter property must have exactly one argument\n");
    const std::string duplicateWarning = "<stdin>: WARNING: Duplicate key \"x\" in object literal\n" "<stdin>: NOTE: The original key \"x\" is here:\n";
    expectParseError( "({x, x})", duplicateWarning);
    expectParseError( "({x() {}, x() {}})", duplicateWarning);
    expectParseError( "({get x() {}, get x() {}})", duplicateWarning);
    expectParseError( "({get x() {}, set x(y) {}, get x() {}})", duplicateWarning);
    expectParseError( "({get x() {}, set x(y) {}, set x(y) {}})", duplicateWarning);
    expectParseError( "({get x() {}, set x(y) {}})", "");
    expectParseError( "({set x(y) {}, get x() {}})", "");
    // Check the string-to-int optimization
    expectPrintedMangle( "x = { '0': y }", "x = { 0: y };\n");
    expectPrintedMangle( "x = { '123': y }", "x = { 123: y };\n");
    expectPrintedMangle( "x = { '-123': y }", "x = { \"-123\": y };\n");
    expectPrintedMangle( "x = { '-0': y }", "x = { \"-0\": y };\n");
    expectPrintedMangle( "x = { '01': y }", "x = { \"01\": y };\n");
    expectPrintedMangle( "x = { '-01': y }", "x = { \"-01\": y };\n");
    expectPrintedMangle( "x = { '0x1': y }", "x = { \"0x1\": y };\n");
    expectPrintedMangle( "x = { '-0x1': y }", "x = { \"-0x1\": y };\n");
    expectPrintedMangle( "x = { '2147483647': y }", "x = { 2147483647: y };\n");
    expectPrintedMangle( "x = { '2147483648': y }", "x = { \"2147483648\": y };\n");
    expectPrintedMangle( "x = { '-2147483648': y }", "x = { \"-2147483648\": y };\n");
    expectPrintedMangle( "x = { '-2147483649': y }", "x = { \"-2147483649\": y };\n");
    // See: https://github.com/microsoft/TypeScript/pull/60225
    expectPrinted( "x = { get \n x() {} }", "x = { get x() {\n} };\n");
    expectPrinted( "x = { set \n x(_) {} }", "x = { set x(_) {\n} };\n");
    expectParseError( "x = { get \n *x() {} }", "<stdin>: ERROR: Expected \"}\" but found \"*\"\n");
    expectParseError( "x = { set \n *x(_) {} }", "<stdin>: ERROR: Expected \"}\" but found \"*\"\n");
    expectParseError( "x = { get \n async x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectParseError( "x = { set \n async x(_) {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
}

TEST(JsParser, TestComputedProperty) {
    expectPrinted( "({[a]: foo})", "({ [a]: foo });\n");
    expectPrinted( "({[(a, b)]: foo})", "({ [(a, b)]: foo });\n");
    expectParseError( "({[a, b]: foo})", "<stdin>: ERROR: Expected \"]\" but found \",\"\n");
    expectPrinted( "({[a]: foo}) => {}", "({ [a]: foo }) => {\n};\n");
    expectPrinted( "({[(a, b)]: foo}) => {}", "({ [(a, b)]: foo }) => {\n};\n");
    expectParseError( "({[a, b]: foo}) => {}", "<stdin>: ERROR: Expected \"]\" but found \",\"\n");
    expectPrinted( "var {[a]: foo} = bar", "var { [a]: foo } = bar;\n");
    expectPrinted( "var {[(a, b)]: foo} = bar", "var { [(a, b)]: foo } = bar;\n");
    expectParseError( "var {[a, b]: foo} = bar", "<stdin>: ERROR: Expected \"]\" but found \",\"\n");
    expectPrinted( "class Foo {[a] = foo}", "class Foo {\n  [a] = foo;\n}\n");
    expectPrinted( "class Foo {[(a, b)] = foo}", "class Foo {\n  [(a, b)] = foo;\n}\n");
    expectParseError( "class Foo {[a, b] = foo}", "<stdin>: ERROR: Expected \"]\" but found \",\"\n");
}

TEST(JsParser, TestQuotedProperty) {
    expectPrinted( "x.x; y['y']", "x.x;\ny[\"y\"];\n");
    expectPrinted( "({y: y, 'z': z} = x)", "({ y, \"z\": z } = x);\n");
    expectPrinted( "var {y: y, 'z': z} = x", "var { y, \"z\": z } = x;\n");
    expectPrinted( "x = {y: 1, 'z': 2}", "x = { y: 1, \"z\": 2 };\n");
    expectPrinted( "x = {y() {}, 'z'() {}}", "x = { y() {\n}, \"z\"() {\n} };\n");
    expectPrinted( "x = {get y() {}, set 'z'(z) {}}", "x = { get y() {\n}, set \"z\"(z) {\n} };\n");
    expectPrinted( "x = class {y = 1; 'z' = 2}", "x = class {\n  y = 1;\n  \"z\" = 2;\n};\n");
    expectPrinted( "x = class {y() {}; 'z'() {}}", "x = class {\n  y() {\n  }\n  \"z\"() {\n  }\n};\n");
    expectPrinted( "x = class {get y() {}; set 'z'(z) {}}", "x = class {\n  get y() {\n  }\n  set \"z\"(z) {\n  }\n};\n");
    expectPrintedMangle( "x.x; y['y']", "x.x, y.y;\n");
    expectPrintedMangle( "({y: y, 'z': z} = x)", "({ y, z } = x);\n");
    expectPrintedMangle( "var {y: y, 'z': z} = x", "var { y, z } = x;\n");
    expectPrintedMangle( "x = {y: 1, 'z': 2}", "x = { y: 1, z: 2 };\n");
    expectPrintedMangle( "x = {y() {}, 'z'() {}}", "x = { y() {\n}, z() {\n} };\n");
    expectPrintedMangle( "x = {get y() {}, set 'z'(z) {}}", "x = { get y() {\n}, set z(z) {\n} };\n");
    expectPrintedMangle( "x = class {y = 1; 'z' = 2}", "x = class {\n  y = 1;\n  z = 2;\n};\n");
    expectPrintedMangle( "x = class {y() {}; 'z'() {}}", "x = class {\n  y() {\n  }\n  z() {\n  }\n};\n");
    expectPrintedMangle( "x = class {get y() {}; set 'z'(z) {}}", "x = class {\n  get y() {\n  }\n  set z(z) {\n  }\n};\n");
}

TEST(JsParser, TestLexicalDecl) {
    expectPrinted( "if (1) var x", "if (1) var x;\n");
    expectPrinted( "if (1) function x() {}", "if (1) {\n  let x = function() {\n  };\n  var x = x;\n}\n");
    expectPrinted( "if (1) {} else function x() {}", "if (1) {\n} else {\n  let x = function() {\n  };\n  var x = x;\n}\n");
    expectPrinted( "switch (1) { case 1: const x = 1 }", "switch (1) {\n  case 1:\n    const x = 1;\n}\n");
    expectPrinted( "switch (1) { default: const x = 1 }", "switch (1) {\n  default:\n    const x = 1;\n}\n");
    const std::vector<std::string> singleStmtContext = {
        "label: %s",
        "for (;;) %s",
        "if (1) %s",
        "while (1) %s",
        "with ({}) %s",
        "if (1) {} else %s",
        "do %s \n while(0)",

        "for (;;) label: %s",
        "if (1) label: %s",
        "while (1) label: %s",
        "with ({}) label: %s",
        "if (1) {} else label: %s",
        "do label: %s \n while(0)",

        "for (;;) label: label2: %s",
        "if (1) label: label2: %s",
        "while (1) label: label2: %s",
        "with ({}) label: label2: %s",
        "if (1) {} else label: label2: %s",
        "do label: label2: %s \n while(0)",
        };
    const std::string singleStmtError = "<stdin>: ERROR: Cannot use a declaration in a single-statement context\n" "NOTE: Wrap this declaration in a block statement to use it here.\n";
    for (const std::string& context : singleStmtContext) {
        expectParseError( SprintfReplace(context, "const x = 0"), singleStmtError);
        expectParseError( SprintfReplace(context, "let x"), singleStmtError);
        expectParseError( SprintfReplace(context, "class X {}"), singleStmtError);
        expectParseError( SprintfReplace(context, "function* x() {}"), singleStmtError);
        expectParseError( SprintfReplace(context, "async function x() {}"), singleStmtError);
        expectParseError( SprintfReplace(context, "async function* x() {}"), singleStmtError);
    }
    expectPrinted( "function f() {}", "function f() {\n}\n");
    expectPrinted( "{function f() {}} let f", "{\n  let f = function() {\n  };\n}\nlet f;\n");
    expectPrinted( "if (1) function f() {} let f", "if (1) {\n  let f = function() {\n  };\n}\nlet f;\n");
    expectPrinted( "if (0) ; else function f() {} let f", "if (0) ;\nelse {\n  let f = function() {\n  };\n}\nlet f;\n");
    expectPrinted( "x: function f() {}", "x: {\n  let f = function() {\n  };\n  var f = f;\n}\n");
    expectPrinted( "{function* f() {}} let f", "{\n  function* f() {\n  }\n}\nlet f;\n");
    expectPrinted( "{async function f() {}} let f", "{\n  async function f() {\n  }\n}\nlet f;\n");
    expectParseError( "if (1) label: function f() {} let f", singleStmtError);
    expectParseError( "if (1) label: label2: function f() {} let f", singleStmtError);
    expectParseError( "if (0) ; else label: function f() {} let f", singleStmtError);
    expectParseError( "if (0) ; else label: label2: function f() {} let f", singleStmtError);
    expectParseError( "for (;;) function f() {}", singleStmtError);
    expectParseError( "for (x in y) function f() {}", singleStmtError);
    expectParseError( "for (x of y) function f() {}", singleStmtError);
    expectParseError( "for await (x of y) function f() {}", singleStmtError);
    expectParseError( "with (1) function f() {}", singleStmtError);
    expectParseError( "while (1) function f() {}", singleStmtError);
    expectParseError( "do function f() {} while (0)", singleStmtError);
    const std::string fnLabelAwait = "<stdin>: ERROR: Function declarations inside labels cannot be used in an ECMAScript module\n" "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n";
    expectParseError( "for (;;) label: function f() {}", singleStmtError);
    expectParseError( "for (x in y) label: function f() {}", singleStmtError);
    expectParseError( "for (x of y) label: function f() {}", singleStmtError);
    expectParseError( "for await (x of y) label: function f() {}", singleStmtError+fnLabelAwait);
    expectParseError( "with (1) label: function f() {}", singleStmtError);
    expectParseError( "while (1) label: function f() {}", singleStmtError);
    expectParseError( "do label: function f() {} while (0)", singleStmtError);
    expectParseError( "for (;;) label: label2: function f() {}", singleStmtError);
    expectParseError( "for (x in y) label: label2: function f() {}", singleStmtError);
    expectParseError( "for (x of y) label: label2: function f() {}", singleStmtError);
    expectParseError( "for await (x of y) label: label2: function f() {}", singleStmtError+fnLabelAwait);
    expectParseError( "with (1) label: label2: function f() {}", singleStmtError);
    expectParseError( "while (1) label: label2: function f() {}", singleStmtError);
    expectParseError( "do label: label2: function f() {} while (0)", singleStmtError);
    // Test direct "eval"
    expectPrinted( "if (foo) { function x() {} }", "if (foo) {\n  let x = function() {\n  };\n  var x = x;\n}\n");
    expectPrinted( "if (foo) { function x() {} eval('') }", "if (foo) {\n  function x() {\n  }\n  eval(\"\");\n}\n");
    expectPrinted( "if (foo) { function x() {} if (bar) { eval('') } }", "if (foo) {\n  function x() {\n  }\n  if (bar) {\n    eval(\"\");\n  }\n}\n");
    expectPrinted( "if (foo) { eval(''); function x() {} }", "if (foo) {\n  function x() {\n  }\n  eval(\"\");\n}\n");
    expectPrinted( "'use strict'; if (foo) { function x() {} }", "\"use strict\";\nif (foo) {\n  let x = function() {\n  };\n}\n");
    expectPrinted( "'use strict'; if (foo) { function x() {} eval('') }", "\"use strict\";\nif (foo) {\n  function x() {\n  }\n  eval(\"\");\n}\n");
    expectPrinted( "'use strict'; if (foo) { function x() {} if (bar) { eval('') } }", "\"use strict\";\nif (foo) {\n  function x() {\n  }\n  if (bar) {\n    eval(\"\");\n  }\n}\n");
    expectPrinted( "'use strict'; if (foo) { eval(''); function x() {} }", "\"use strict\";\nif (foo) {\n  function x() {\n  }\n  eval(\"\");\n}\n");
}

TEST(JsParser, TestFunction) {
    expectPrinted( "function f() {} function f() {}", "function f() {\n}\nfunction f() {\n}\n");
    expectPrinted( "function f() {} function* f() {}", "function f() {\n}\nfunction* f() {\n}\n");
    expectPrinted( "function* f() {} function* f() {}", "function* f() {\n}\nfunction* f() {\n}\n");
    expectPrinted( "function f() {} async function f() {}", "function f() {\n}\nasync function f() {\n}\n");
    expectPrinted( "async function f() {} async function f() {}", "async function f() {\n}\nasync function f() {\n}\n");
    expectPrinted( "function arguments() {}", "function arguments() {\n}\n");
    expectPrinted( "(function arguments() {})", "(function arguments() {\n});\n");
    expectPrinted( "function foo(arguments) {}", "function foo(arguments) {\n}\n");
    expectPrinted( "(function foo(arguments) {})", "(function foo(arguments) {\n});\n");
    expectPrinted( "(function foo() { var arguments })", "(function foo() {\n  var arguments;\n});\n");
    expectPrinted( "(function foo() { { var arguments } })", "(function foo() {\n  {\n    var arguments;\n  }\n});\n");
    expectPrintedMangle( "function foo() { return undefined }", "function foo() {\n}\n");
    expectPrintedMangle( "function* foo() { return undefined }", "function* foo() {\n}\n");
    expectPrintedMangle( "async function foo() { return undefined }", "async function foo() {\n}\n");
    expectPrintedMangle( "async function* foo() { return undefined }", "async function* foo() {\n  return void 0;\n}\n");
    // Strip overwritten function declarations
    expectPrintedMangle( "function f() { x() } function f() { y() }", "function f() {\n  y();\n}\n");
    expectPrintedMangle( "function f() { x() } function *f() { y() }", "function* f() {\n  y();\n}\n");
    expectPrintedMangle( "function *f() { x() } function f() { y() }", "function f() {\n  y();\n}\n");
    expectPrintedMangle( "function *f() { x() } function *f() { y() }", "function* f() {\n  y();\n}\n");
    expectPrintedMangle( "function f() { x() } async function f() { y() }", "async function f() {\n  y();\n}\n");
    expectPrintedMangle( "async function f() { x() } function f() { y() }", "function f() {\n  y();\n}\n");
    expectPrintedMangle( "async function f() { x() } async function f() { y() }", "async function f() {\n  y();\n}\n");
    expectPrintedMangle( "var f; function f() {}", "var f;\nfunction f() {\n}\n");
    expectPrintedMangle( "function f() {} var f", "function f() {\n}\nvar f;\n");
    expectPrintedMangle( "var f; function f() { x() } function f() { y() }", "var f;\nfunction f() {\n  y();\n}\n");
    expectPrintedMangle( "function f() { x() } function f() { y() } var f", "function f() {\n  y();\n}\nvar f;\n");
    expectPrintedMangle( "function f() { x() } var f; function f() { y() }", "function f() {\n  x();\n}\nvar f;\nfunction f() {\n  y();\n}\n");
    const std::string redeclaredError = "<stdin>: ERROR: The symbol \"f\" has already been declared\n" "<stdin>: NOTE: The symbol \"f\" was originally declared here:\n";
    expectParseError( "function *f() {} function *f() {}", "");
    expectParseError( "function f() {} let f", redeclaredError);
    expectParseError( "function f() {} var f", "");
    expectParseError( "function *f() {} var f", "");
    expectParseError( "let f; function f() {}", redeclaredError);
    expectParseError( "var f; function f() {}", "");
    expectParseError( "var f; function *f() {}", "");
    expectParseError( "{ function *f() {} function *f() {} }", redeclaredError);
    expectParseError( "{ function f() {} let f }", redeclaredError);
    expectParseError( "{ function f() {} var f }", redeclaredError);
    expectParseError( "{ function *f() {} var f }", redeclaredError);
    expectParseError( "{ let f; function f() {} }", redeclaredError);
    expectParseError( "{ var f; function f() {} }", redeclaredError);
    expectParseError( "{ var f; function *f() {} }", redeclaredError);
    expectParseError( "switch (0) { case 1: function *f() {} default: function *f() {} }", redeclaredError);
    expectParseError( "switch (0) { case 1: function f() {} default: let f }", redeclaredError);
    expectParseError( "switch (0) { case 1: function f() {} default: var f }", redeclaredError);
    expectParseError( "switch (0) { case 1: function *f() {} default: var f }", redeclaredError);
    expectParseError( "switch (0) { case 1: let f; default: function f() {} }", redeclaredError);
    expectParseError( "switch (0) { case 1: var f; default: function f() {} }", redeclaredError);
    expectParseError( "switch (0) { case 1: var f; default: function *f() {} }", redeclaredError);
    // Inject parentheses around IIFEs as they are an optimization hint for VMs
    expectPrinted( "var x = function() { y() }()", "var x = (function() {\n  y();\n})();\n");
    expectPrinted( "var x = (true && function() { y() })()", "var x = (function() {\n  y();\n})();\n");
}

TEST(JsParser, TestClass) {
    expectPrinted( "class Foo { foo() {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectPrinted( "class Foo { *foo() {} }", "class Foo {\n  *foo() {\n  }\n}\n");
    expectPrinted( "class Foo { get foo() {} }", "class Foo {\n  get foo() {\n  }\n}\n");
    expectPrinted( "class Foo { set foo(x) {} }", "class Foo {\n  set foo(x) {\n  }\n}\n");
    expectPrinted( "class Foo { async foo() {} }", "class Foo {\n  async foo() {\n  }\n}\n");
    expectPrinted( "class Foo { async *foo() {} }", "class Foo {\n  async *foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static foo() {} }", "class Foo {\n  static foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static *foo() {} }", "class Foo {\n  static *foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static get foo() {} }", "class Foo {\n  static get foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static set foo(x) {} }", "class Foo {\n  static set foo(x) {\n  }\n}\n");
    expectPrinted( "class Foo { static async foo() {} }", "class Foo {\n  static async foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static async *foo() {} }", "class Foo {\n  static async *foo() {\n  }\n}\n");
    expectParseError( "class Foo { async static foo() {} }", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "class Foo { * static foo() {} }", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "class Foo { * *foo() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { async * *foo() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { * async foo() {} }", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "class Foo { * async * foo() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectParseError( "class Foo { static * *foo() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { static async * *foo() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { static * async foo() {} }", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "class Foo { static * async * foo() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectPrinted( "class Foo { if() {} }", "class Foo {\n  if() {\n  }\n}\n");
    expectPrinted( "class Foo { *if() {} }", "class Foo {\n  *if() {\n  }\n}\n");
    expectPrinted( "class Foo { get if() {} }", "class Foo {\n  get if() {\n  }\n}\n");
    expectPrinted( "class Foo { set if(x) {} }", "class Foo {\n  set if(x) {\n  }\n}\n");
    expectPrinted( "class Foo { async if() {} }", "class Foo {\n  async if() {\n  }\n}\n");
    expectPrinted( "class Foo { async *if() {} }", "class Foo {\n  async *if() {\n  }\n}\n");
    expectPrinted( "class Foo { static if() {} }", "class Foo {\n  static if() {\n  }\n}\n");
    expectPrinted( "class Foo { static *if() {} }", "class Foo {\n  static *if() {\n  }\n}\n");
    expectPrinted( "class Foo { static get if() {} }", "class Foo {\n  static get if() {\n  }\n}\n");
    expectPrinted( "class Foo { static set if(x) {} }", "class Foo {\n  static set if(x) {\n  }\n}\n");
    expectPrinted( "class Foo { static async if() {} }", "class Foo {\n  static async if() {\n  }\n}\n");
    expectPrinted( "class Foo { static async *if() {} }", "class Foo {\n  static async *if() {\n  }\n}\n");
    expectParseError( "class Foo { async static if() {} }", "<stdin>: ERROR: Expected \"(\" but found \"if\"\n");
    expectParseError( "class Foo { * static if() {} }", "<stdin>: ERROR: Expected \"(\" but found \"if\"\n");
    expectParseError( "class Foo { * *if() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { async * *if() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { * async if() {} }", "<stdin>: ERROR: Expected \"(\" but found \"if\"\n");
    expectParseError( "class Foo { * async * if() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectParseError( "class Foo { static * *if() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { static async * *if() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { static * async if() {} }", "<stdin>: ERROR: Expected \"(\" but found \"if\"\n");
    expectParseError( "class Foo { static * async * if() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectPrinted( "class Foo { a() {} b() {} }", "class Foo {\n  a() {\n  }\n  b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} get b() {} }", "class Foo {\n  a() {\n  }\n  get b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} set b(x) {} }", "class Foo {\n  a() {\n  }\n  set b(x) {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} async b() {} }", "class Foo {\n  a() {\n  }\n  async b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} async *b() {} }", "class Foo {\n  a() {\n  }\n  async *b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} static b() {} }", "class Foo {\n  a() {\n  }\n  static b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} static *b() {} }", "class Foo {\n  a() {\n  }\n  static *b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} static get b() {} }", "class Foo {\n  a() {\n  }\n  static get b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} static set b(x) {} }", "class Foo {\n  a() {\n  }\n  static set b(x) {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} static async b() {} }", "class Foo {\n  a() {\n  }\n  static async b() {\n  }\n}\n");
    expectPrinted( "class Foo { a() {} static async *b() {} }", "class Foo {\n  a() {\n  }\n  static async *b() {\n  }\n}\n");
    expectParseError( "class Foo { a() {} async static b() {} }", "<stdin>: ERROR: Expected \"(\" but found \"b\"\n");
    expectParseError( "class Foo { a() {} * static b() {} }", "<stdin>: ERROR: Expected \"(\" but found \"b\"\n");
    expectParseError( "class Foo { a() {} * *b() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { a() {} async * *b() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { a() {} * async b() {} }", "<stdin>: ERROR: Expected \"(\" but found \"b\"\n");
    expectParseError( "class Foo { a() {} * async * b() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectParseError( "class Foo { a() {} static * *b() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { a() {} static async * *b() {} }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "class Foo { a() {} static * async b() {} }", "<stdin>: ERROR: Expected \"(\" but found \"b\"\n");
    expectParseError( "class Foo { a() {} static * async * b() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectParseError( "class Foo { `a`() {} }", "<stdin>: ERROR: Expected identifier but found \"`a`\"\n");
    // Strict mode reserved words cannot be used as class names
    expectParseError( "class static {}",
        "<stdin>: ERROR: \"static\" is a reserved word and cannot be used in strict mode\n" "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n");
    expectParseError( "(class static {})",
        "<stdin>: ERROR: \"static\" is a reserved word and cannot be used in strict mode\n" "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n");
    expectParseError( "class implements {}",
        "<stdin>: ERROR: \"implements\" is a reserved word and cannot be used in strict mode\n" "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n");
    expectParseError( "(class implements {})",
        "<stdin>: ERROR: \"implements\" is a reserved word and cannot be used in strict mode\n" "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n");
    // The name "arguments" is forbidden in class bodies outside of computed properties
    expectPrinted( "class Foo { [arguments] }", "class Foo {\n  [arguments];\n}\n");
    expectPrinted( "class Foo { [arguments] = 1 }", "class Foo {\n  [arguments] = 1;\n}\n");
    expectPrinted( "class Foo { arguments = 1 }", "class Foo {\n  arguments = 1;\n}\n");
    expectPrinted( "class Foo { x = class { arguments = 1 } }", "class Foo {\n  x = class {\n    arguments = 1;\n  };\n}\n");
    expectPrinted( "class Foo { x = function() { arguments } }", "class Foo {\n  x = function() {\n    arguments;\n  };\n}\n");
    expectParseError( "class Foo { x = arguments }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { x = () => arguments }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { x = typeof arguments }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { x = 1 ? 2 : arguments }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { x = class { [arguments] } }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { x = class { [arguments] = 1 } }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { static { arguments } }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    expectParseError( "class Foo { static { class Bar { [arguments] } } }", "<stdin>: ERROR: Cannot access \"arguments\" here:\n");
    // The name "constructor" is sometimes forbidden
    expectPrinted( "class Foo { get ['constructor']() {} }", "class Foo {\n  get [\"constructor\"]() {\n  }\n}\n");
    expectPrinted( "class Foo { set ['constructor'](x) {} }", "class Foo {\n  set [\"constructor\"](x) {\n  }\n}\n");
    expectPrinted( "class Foo { *['constructor']() {} }", "class Foo {\n  *[\"constructor\"]() {\n  }\n}\n");
    expectPrinted( "class Foo { async ['constructor']() {} }", "class Foo {\n  async [\"constructor\"]() {\n  }\n}\n");
    expectPrinted( "class Foo { async *['constructor']() {} }", "class Foo {\n  async *[\"constructor\"]() {\n  }\n}\n");
    expectParseError( "class Foo { get constructor() {} }", "<stdin>: ERROR: Class constructor cannot be a getter\n");
    expectParseError( "class Foo { get 'constructor'() {} }", "<stdin>: ERROR: Class constructor cannot be a getter\n");
    expectParseError( "class Foo { set constructor(x) {} }", "<stdin>: ERROR: Class constructor cannot be a setter\n");
    expectParseError( "class Foo { set 'constructor'(x) {} }", "<stdin>: ERROR: Class constructor cannot be a setter\n");
    expectParseError( "class Foo { *constructor() {} }", "<stdin>: ERROR: Class constructor cannot be a generator\n");
    expectParseError( "class Foo { *'constructor'() {} }", "<stdin>: ERROR: Class constructor cannot be a generator\n");
    expectParseError( "class Foo { async constructor() {} }", "<stdin>: ERROR: Class constructor cannot be an async function\n");
    expectParseError( "class Foo { async 'constructor'() {} }", "<stdin>: ERROR: Class constructor cannot be an async function\n");
    expectParseError( "class Foo { async *constructor() {} }", "<stdin>: ERROR: Class constructor cannot be an async function\n");
    expectParseError( "class Foo { async *'constructor'() {} }", "<stdin>: ERROR: Class constructor cannot be an async function\n");
    expectPrinted( "class Foo { static get constructor() {} }", "class Foo {\n  static get constructor() {\n  }\n}\n");
    expectPrinted( "class Foo { static get 'constructor'() {} }", "class Foo {\n  static get \"constructor\"() {\n  }\n}\n");
    expectPrinted( "class Foo { static set constructor(x) {} }", "class Foo {\n  static set constructor(x) {\n  }\n}\n");
    expectPrinted( "class Foo { static set 'constructor'(x) {} }", "class Foo {\n  static set \"constructor\"(x) {\n  }\n}\n");
    expectPrinted( "class Foo { static *constructor() {} }", "class Foo {\n  static *constructor() {\n  }\n}\n");
    expectPrinted( "class Foo { static *'constructor'() {} }", "class Foo {\n  static *\"constructor\"() {\n  }\n}\n");
    expectPrinted( "class Foo { static async constructor() {} }", "class Foo {\n  static async constructor() {\n  }\n}\n");
    expectPrinted( "class Foo { static async 'constructor'() {} }", "class Foo {\n  static async \"constructor\"() {\n  }\n}\n");
    expectPrinted( "class Foo { static async *constructor() {} }", "class Foo {\n  static async *constructor() {\n  }\n}\n");
    expectPrinted( "class Foo { static async *'constructor'() {} }", "class Foo {\n  static async *\"constructor\"() {\n  }\n}\n");
    expectPrinted( "({ constructor: 1 })", "({ constructor: 1 });\n");
    expectPrinted( "({ get constructor() {} })", "({ get constructor() {\n} });\n");
    expectPrinted( "({ set constructor(x) {} })", "({ set constructor(x) {\n} });\n");
    expectPrinted( "({ *constructor() {} })", "({ *constructor() {\n} });\n");
    expectPrinted( "({ async constructor() {} })", "({ async constructor() {\n} });\n");
    expectPrinted( "({ async* constructor() {} })", "({ async *constructor() {\n} });\n");
    // The name "prototype" is sometimes forbidden
    expectPrinted( "class Foo { get prototype() {} }", "class Foo {\n  get prototype() {\n  }\n}\n");
    expectPrinted( "class Foo { get 'prototype'() {} }", "class Foo {\n  get \"prototype\"() {\n  }\n}\n");
    expectPrinted( "class Foo { set prototype(x) {} }", "class Foo {\n  set prototype(x) {\n  }\n}\n");
    expectPrinted( "class Foo { set 'prototype'(x) {} }", "class Foo {\n  set \"prototype\"(x) {\n  }\n}\n");
    expectPrinted( "class Foo { *prototype() {} }", "class Foo {\n  *prototype() {\n  }\n}\n");
    expectPrinted( "class Foo { *'prototype'() {} }", "class Foo {\n  *\"prototype\"() {\n  }\n}\n");
    expectPrinted( "class Foo { async prototype() {} }", "class Foo {\n  async prototype() {\n  }\n}\n");
    expectPrinted( "class Foo { async 'prototype'() {} }", "class Foo {\n  async \"prototype\"() {\n  }\n}\n");
    expectPrinted( "class Foo { async *prototype() {} }", "class Foo {\n  async *prototype() {\n  }\n}\n");
    expectPrinted( "class Foo { async *'prototype'() {} }", "class Foo {\n  async *\"prototype\"() {\n  }\n}\n");
    expectParseError( "class Foo { static get prototype() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static get 'prototype'() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static set prototype(x) {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static set 'prototype'(x) {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static *prototype() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static *'prototype'() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static async prototype() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static async 'prototype'() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static async *prototype() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectParseError( "class Foo { static async *'prototype'() {} }", "<stdin>: ERROR: Invalid static method name \"prototype\"\n");
    expectPrinted( "class Foo { static get ['prototype']() {} }", "class Foo {\n  static get [\"prototype\"]() {\n  }\n}\n");
    expectPrinted( "class Foo { static set ['prototype'](x) {} }", "class Foo {\n  static set [\"prototype\"](x) {\n  }\n}\n");
    expectPrinted( "class Foo { static *['prototype']() {} }", "class Foo {\n  static *[\"prototype\"]() {\n  }\n}\n");
    expectPrinted( "class Foo { static async ['prototype']() {} }", "class Foo {\n  static async [\"prototype\"]() {\n  }\n}\n");
    expectPrinted( "class Foo { static async *['prototype']() {} }", "class Foo {\n  static async *[\"prototype\"]() {\n  }\n}\n");
    expectPrinted( "({ prototype: 1 })", "({ prototype: 1 });\n");
    expectPrinted( "({ get prototype() {} })", "({ get prototype() {\n} });\n");
    expectPrinted( "({ set prototype(x) {} })", "({ set prototype(x) {\n} });\n");
    expectPrinted( "({ *prototype() {} })", "({ *prototype() {\n} });\n");
    expectPrinted( "({ async prototype() {} })", "({ async prototype() {\n} });\n");
    expectPrinted( "({ async* prototype() {} })", "({ async *prototype() {\n} });\n");
    expectPrintedMangle( "class Foo { ['constructor'] = 0 }", "class Foo {\n  [\"constructor\"] = 0;\n}\n");
    expectPrintedMangle( "class Foo { ['constructor']() {} }", "class Foo {\n  [\"constructor\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { *['constructor']() {} }", "class Foo {\n  *[\"constructor\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { get ['constructor']() {} }", "class Foo {\n  get [\"constructor\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { set ['constructor'](x) {} }", "class Foo {\n  set [\"constructor\"](x) {\n  }\n}\n");
    expectPrintedMangle( "class Foo { async ['constructor']() {} }", "class Foo {\n  async [\"constructor\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static ['constructor'] = 0 }", "class Foo {\n  static [\"constructor\"] = 0;\n}\n");
    expectPrintedMangle( "class Foo { static ['constructor']() {} }", "class Foo {\n  static constructor() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static *['constructor']() {} }", "class Foo {\n  static *constructor() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static get ['constructor']() {} }", "class Foo {\n  static get constructor() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static set ['constructor'](x) {} }", "class Foo {\n  static set constructor(x) {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static async ['constructor']() {} }", "class Foo {\n  static async constructor() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { ['prototype'] = 0 }", "class Foo {\n  prototype = 0;\n}\n");
    expectPrintedMangle( "class Foo { ['prototype']() {} }", "class Foo {\n  prototype() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { *['prototype']() {} }", "class Foo {\n  *prototype() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { get ['prototype']() {} }", "class Foo {\n  get prototype() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { set ['prototype'](x) {} }", "class Foo {\n  set prototype(x) {\n  }\n}\n");
    expectPrintedMangle( "class Foo { async ['prototype']() {} }", "class Foo {\n  async prototype() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static ['prototype'] = 0 }", "class Foo {\n  static [\"prototype\"] = 0;\n}\n");
    expectPrintedMangle( "class Foo { static ['prototype']() {} }", "class Foo {\n  static [\"prototype\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static *['prototype']() {} }", "class Foo {\n  static *[\"prototype\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static get ['prototype']() {} }", "class Foo {\n  static get [\"prototype\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static set ['prototype'](x) {} }", "class Foo {\n  static set [\"prototype\"](x) {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static async ['prototype']() {} }", "class Foo {\n  static async [\"prototype\"]() {\n  }\n}\n");
    const std::string dupCtor = "<stdin>: ERROR: Classes cannot contain more than one constructor\n";
    expectParseError( "class Foo { constructor() {} constructor() {} }", dupCtor);
    expectParseError( "class Foo { constructor() {} 'constructor'() {} }", dupCtor);
    expectParseError( "class Foo { constructor() {} ['constructor']() {} }", "");
    expectParseError( "class Foo { 'constructor'() {} constructor() {} }", dupCtor);
    expectParseError( "class Foo { ['constructor']() {} constructor() {} }", "");
    expectParseError( "class Foo { constructor() {} static constructor() {} }", "");
    expectParseError( "class Foo { static constructor() {} constructor() {} }", "");
    expectParseError( "class Foo { static constructor() {} static constructor() {} }", "");
    expectParseError( "class Foo { constructor = () => {}; constructor = () => {} }",
        "<stdin>: ERROR: Invalid field name \"constructor\"\n<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "({ constructor() {}, constructor() {} })",
        "<stdin>: WARNING: Duplicate key \"constructor\" in object literal\n<stdin>: NOTE: The original key \"constructor\" is here:\n");
    expectParseError( "(class { constructor() {} constructor() {} })", dupCtor);
    expectPrintedMangle( "class Foo { constructor() {} ['constructor']() {} }",
        "class Foo {\n  constructor() {\n  }\n  [\"constructor\"]() {\n  }\n}\n");
    expectPrintedMangle( "class Foo { static constructor() {} static ['constructor']() {} }",
        "class Foo {\n  static constructor() {\n  }\n  static constructor() {\n  }\n}\n");
    // Check the string-to-int optimization
    expectPrintedMangle( "class x { '0' = y }", "class x {\n  0 = y;\n}\n");
    expectPrintedMangle( "class x { '123' = y }", "class x {\n  123 = y;\n}\n");
    expectPrintedMangle( "class x { ['-123'] = y }", "class x {\n  \"-123\" = y;\n}\n");
    expectPrintedMangle( "class x { '-0' = y }", "class x {\n  \"-0\" = y;\n}\n");
    expectPrintedMangle( "class x { '01' = y }", "class x {\n  \"01\" = y;\n}\n");
    expectPrintedMangle( "class x { '-01' = y }", "class x {\n  \"-01\" = y;\n}\n");
    expectPrintedMangle( "class x { '0x1' = y }", "class x {\n  \"0x1\" = y;\n}\n");
    expectPrintedMangle( "class x { '-0x1' = y }", "class x {\n  \"-0x1\" = y;\n}\n");
    expectPrintedMangle( "class x { '2147483647' = y }", "class x {\n  2147483647 = y;\n}\n");
    expectPrintedMangle( "class x { '2147483648' = y }", "class x {\n  \"2147483648\" = y;\n}\n");
    expectPrintedMangle( "class x { ['-2147483648'] = y }", "class x {\n  \"-2147483648\" = y;\n}\n");
    expectPrintedMangle( "class x { ['-2147483649'] = y }", "class x {\n  \"-2147483649\" = y;\n}\n");
    // Make sure direct "eval" doesn't cause the class name to change
    expectPrinted( "class Foo { foo = [Foo, eval(bar)] }", "class Foo {\n  foo = [Foo, eval(bar)];\n}\n");
    // See: https://github.com/microsoft/TypeScript/pull/60225
    expectPrinted( "class A { get \n x() {} }", "class A {\n  get x() {\n  }\n}\n");
    expectPrinted( "class A { set \n x(_) {} }", "class A {\n  set x(_) {\n  }\n}\n");
    expectPrinted( "class A { get \n *x() {} }", "class A {\n  get;\n  *x() {\n  }\n}\n");
    expectPrinted( "class A { set \n *x(_) {} }", "class A {\n  set;\n  *x(_) {\n  }\n}\n");
    expectParseError( "class A { get \n async x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectParseError( "class A { set \n async x(_) {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectParseError( "class A { async get \n *x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectParseError( "class A { async set \n *x(_) {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
}

TEST(JsParser, TestSuperCall) {
    expectParseError( "super", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "super()", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { foo = super() }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { foo() { super() } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo extends Bar { foo = super() }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo extends Bar { foo() { super() } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo extends Bar { static constructor() { super() } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo extends Bar { constructor(x = function() { super() }) {} }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo extends Bar { constructor() { function foo() { super() } } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo extends Bar { constructor() { super } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectPrinted( "class Foo extends Bar { constructor() { super() } }",
        "class Foo extends Bar {\n  constructor() {\n    super();\n  }\n}\n");
    expectPrinted( "class Foo extends Bar { constructor() { () => super() } }",
        "class Foo extends Bar {\n  constructor() {\n    () => super();\n  }\n}\n");
    expectPrinted( "class Foo extends Bar { constructor() { () => { super() } } }",
        "class Foo extends Bar {\n  constructor() {\n    () => {\n      super();\n    };\n  }\n}\n");
    expectPrinted( "class Foo extends Bar { constructor(x = super()) {} }",
        "class Foo extends Bar {\n  constructor(x = super()) {\n  }\n}\n");
    expectPrinted( "class Foo extends Bar { constructor(x = () => super()) {} }",
        "class Foo extends Bar {\n  constructor(x = () => super()) {\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x; constructor() { super() } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\");\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super() } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super(); c() } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n    c();\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { c(); super() } }",
        "class A extends B {\n  constructor() {\n    c();\n    super();\n    __publicField(this, \"x\", 1);\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super(); if (c) throw c } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n    if (c) throw c;\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super(); switch (c) { case 0: throw c } } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n    if (c === 0)\n      throw c;\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super(); while (!c) throw c } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n    for (; !c; ) throw c;\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super(); return c } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n    return c;\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { super(); throw c } }",
        "class A extends B {\n  constructor() {\n    super();\n    __publicField(this, \"x\", 1);\n    throw c;\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { if (true) super(1); else super(2); } }",
        "class A extends B {\n  constructor() {\n    super(1);\n    __publicField(this, \"x\", 1);\n  }\n}\n");
    expectPrintedMangleTarget( 2015, "class A extends B { x = 1; constructor() { if (foo) super(1); else super(2); } }",
        "class A extends B {\n  constructor() {\n    var __super = (...args) => (super(...args), __publicField(this, \"x\", 1), this);\n    foo ? __super(1) : __super(2);\n  }\n}\n");
}

TEST(JsParser, TestSuperProp) {
    expectParseError( "super.x", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "super[x]", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectPrinted( "class Foo { foo() { super.x } }", "class Foo {\n  foo() {\n    super.x;\n  }\n}\n");
    expectPrinted( "class Foo { foo() { super[x] } }", "class Foo {\n  foo() {\n    super[x];\n  }\n}\n");
    expectPrinted( "class Foo { foo(x = super.x) {} }", "class Foo {\n  foo(x = super.x) {\n  }\n}\n");
    expectPrinted( "class Foo { foo(x = super[x]) {} }", "class Foo {\n  foo(x = super[x]) {\n  }\n}\n");
    expectPrinted( "class Foo { static foo() { super.x } }", "class Foo {\n  static foo() {\n    super.x;\n  }\n}\n");
    expectPrinted( "class Foo { static foo() { super[x] } }", "class Foo {\n  static foo() {\n    super[x];\n  }\n}\n");
    expectPrinted( "class Foo { static foo(x = super.x) {} }", "class Foo {\n  static foo(x = super.x) {\n  }\n}\n");
    expectPrinted( "class Foo { static foo(x = super[x]) {} }", "class Foo {\n  static foo(x = super[x]) {\n  }\n}\n");
    expectPrinted( "(class { foo() { super.x } })", "(class {\n  foo() {\n    super.x;\n  }\n});\n");
    expectPrinted( "(class { foo() { super[x] } })", "(class {\n  foo() {\n    super[x];\n  }\n});\n");
    expectPrinted( "(class { foo(x = super.x) {} })", "(class {\n  foo(x = super.x) {\n  }\n});\n");
    expectPrinted( "(class { foo(x = super[x]) {} })", "(class {\n  foo(x = super[x]) {\n  }\n});\n");
    expectPrinted( "(class { static foo() { super.x } })", "(class {\n  static foo() {\n    super.x;\n  }\n});\n");
    expectPrinted( "(class { static foo() { super[x] } })", "(class {\n  static foo() {\n    super[x];\n  }\n});\n");
    expectPrinted( "(class { static foo(x = super.x) {} })", "(class {\n  static foo(x = super.x) {\n  }\n});\n");
    expectPrinted( "(class { static foo(x = super[x]) {} })", "(class {\n  static foo(x = super[x]) {\n  }\n});\n");
    expectPrinted( "class Foo { foo = super.x }", "class Foo {\n  foo = super.x;\n}\n");
    expectPrinted( "class Foo { foo = super[x] }", "class Foo {\n  foo = super[x];\n}\n");
    expectPrinted( "class Foo { foo = () => super.x }", "class Foo {\n  foo = () => super.x;\n}\n");
    expectPrinted( "class Foo { foo = () => super[x] }", "class Foo {\n  foo = () => super[x];\n}\n");
    expectParseError( "class Foo { foo = function () { super.x } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { foo = function () { super[x] } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectPrinted( "class Foo { static foo = super.x }", "class Foo {\n  static foo = super.x;\n}\n");
    expectPrinted( "class Foo { static foo = super[x] }", "class Foo {\n  static foo = super[x];\n}\n");
    expectPrinted( "class Foo { static foo = () => super.x }", "class Foo {\n  static foo = () => super.x;\n}\n");
    expectPrinted( "class Foo { static foo = () => super[x] }", "class Foo {\n  static foo = () => super[x];\n}\n");
    expectParseError( "class Foo { static foo = function () { super.x } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { static foo = function () { super[x] } }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectPrinted( "(class { foo = super.x })", "(class {\n  foo = super.x;\n});\n");
    expectPrinted( "(class { foo = super[x] })", "(class {\n  foo = super[x];\n});\n");
    expectPrinted( "(class { foo = () => super.x })", "(class {\n  foo = () => super.x;\n});\n");
    expectPrinted( "(class { foo = () => super[x] })", "(class {\n  foo = () => super[x];\n});\n");
    expectParseError( "(class { foo = function () { super.x } })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "(class { foo = function () { super[x] } })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectPrinted( "(class { static foo = super.x })", "(class {\n  static foo = super.x;\n});\n");
    expectPrinted( "(class { static foo = super[x] })", "(class {\n  static foo = super[x];\n});\n");
    expectPrinted( "(class { static foo = () => super.x })", "(class {\n  static foo = () => super.x;\n});\n");
    expectPrinted( "(class { static foo = () => super[x] })", "(class {\n  static foo = () => super[x];\n});\n");
    expectParseError( "(class { static foo = function () { super.x } })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "(class { static foo = function () { super[x] } })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "({ foo: super.x })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "({ foo: super[x] })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "({ foo: () => super.x })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "({ foo: () => super[x] })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "({ foo: function () { super.x } })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "({ foo: function () { super[x] } })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectPrinted( "({ foo() { super.x } })", "({ foo() {\n  super.x;\n} });\n");
    expectPrinted( "({ foo() { super[x] } })", "({ foo() {\n  super[x];\n} });\n");
    expectPrinted( "({ foo(x = super.x) {} })", "({ foo(x = super.x) {\n} });\n");
    expectParseError( "class Foo { [super.x] }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { [super[x]] }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { static [super.x] }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "class Foo { static [super[x]] }", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "(class { [super.x] })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "(class { [super[x]] })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "(class { static [super.x] })", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseError( "(class { static [super[x]] })", "<stdin>: ERROR: Unexpected \"super\"\n");
}

TEST(JsParser, TestClassFields) {
    expectPrinted( "class Foo { a }", "class Foo {\n  a;\n}\n");
    expectPrinted( "class Foo { a = 1 }", "class Foo {\n  a = 1;\n}\n");
    expectPrinted( "class Foo { a = 1; b }", "class Foo {\n  a = 1;\n  b;\n}\n");
    expectParseError( "class Foo { a = 1 b }", "<stdin>: ERROR: Expected \";\" but found \"b\"\n");
    expectPrinted( "class Foo { [a] }", "class Foo {\n  [a];\n}\n");
    expectPrinted( "class Foo { [a] = 1 }", "class Foo {\n  [a] = 1;\n}\n");
    expectPrinted( "class Foo { [a] = 1; [b] }", "class Foo {\n  [a] = 1;\n  [b];\n}\n");
    expectParseError( "class Foo { [a] = 1 b }", "<stdin>: ERROR: Expected \";\" but found \"b\"\n");
    expectPrinted( "class Foo { static a }", "class Foo {\n  static a;\n}\n");
    expectPrinted( "class Foo { static a = 1 }", "class Foo {\n  static a = 1;\n}\n");
    expectPrinted( "class Foo { static a = 1; b }", "class Foo {\n  static a = 1;\n  b;\n}\n");
    expectParseError( "class Foo { static a = 1 b }", "<stdin>: ERROR: Expected \";\" but found \"b\"\n");
    expectPrinted( "class Foo { static [a] }", "class Foo {\n  static [a];\n}\n");
    expectPrinted( "class Foo { static [a] = 1 }", "class Foo {\n  static [a] = 1;\n}\n");
    expectPrinted( "class Foo { static [a] = 1; [b] }", "class Foo {\n  static [a] = 1;\n  [b];\n}\n");
    expectParseError( "class Foo { static [a] = 1 b }", "<stdin>: ERROR: Expected \";\" but found \"b\"\n");
    expectParseError( "class Foo { get a }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseError( "class Foo { set a }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseError( "class Foo { async a }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseError( "class Foo { get a = 1 }", "<stdin>: ERROR: Expected \"(\" but found \"=\"\n");
    expectParseError( "class Foo { set a = 1 }", "<stdin>: ERROR: Expected \"(\" but found \"=\"\n");
    expectParseError( "class Foo { async a = 1 }", "<stdin>: ERROR: Expected \"(\" but found \"=\"\n");
    expectParseError( "class Foo { `a` = 0 }", "<stdin>: ERROR: Expected identifier but found \"`a`\"\n");
    // The name "constructor" is forbidden
    expectParseError( "class Foo { constructor }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { 'constructor' }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { constructor = 1 }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { 'constructor' = 1 }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { static constructor }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { static 'constructor' }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { static constructor = 1 }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectParseError( "class Foo { static 'constructor' = 1 }", "<stdin>: ERROR: Invalid field name \"constructor\"\n");
    expectPrinted( "class Foo { ['constructor'] }", "class Foo {\n  [\"constructor\"];\n}\n");
    expectPrinted( "class Foo { ['constructor'] = 1 }", "class Foo {\n  [\"constructor\"] = 1;\n}\n");
    expectPrinted( "class Foo { static ['constructor'] }", "class Foo {\n  static [\"constructor\"];\n}\n");
    expectPrinted( "class Foo { static ['constructor'] = 1 }", "class Foo {\n  static [\"constructor\"] = 1;\n}\n");
    // The name "prototype" is sometimes forbidden
    expectPrinted( "class Foo { prototype }", "class Foo {\n  prototype;\n}\n");
    expectPrinted( "class Foo { 'prototype' }", "class Foo {\n  \"prototype\";\n}\n");
    expectPrinted( "class Foo { prototype = 1 }", "class Foo {\n  prototype = 1;\n}\n");
    expectPrinted( "class Foo { 'prototype' = 1 }", "class Foo {\n  \"prototype\" = 1;\n}\n");
    expectParseError( "class Foo { static prototype }", "<stdin>: ERROR: Invalid field name \"prototype\"\n");
    expectParseError( "class Foo { static 'prototype' }", "<stdin>: ERROR: Invalid field name \"prototype\"\n");
    expectParseError( "class Foo { static prototype = 1 }", "<stdin>: ERROR: Invalid field name \"prototype\"\n");
    expectParseError( "class Foo { static 'prototype' = 1 }", "<stdin>: ERROR: Invalid field name \"prototype\"\n");
    expectPrinted( "class Foo { static ['prototype'] }", "class Foo {\n  static [\"prototype\"];\n}\n");
    expectPrinted( "class Foo { static ['prototype'] = 1 }", "class Foo {\n  static [\"prototype\"] = 1;\n}\n");
}

TEST(JsParser, TestClassStaticBlocks) {
    expectPrinted( "class Foo { static {} }", "class Foo {\n  static {\n  }\n}\n");
    expectPrinted( "class Foo { static {} x = 1 }", "class Foo {\n  static {\n  }\n  x = 1;\n}\n");
    expectPrinted( "class Foo { static { this.foo() } }", "class Foo {\n  static {\n    this.foo();\n  }\n}\n");
    expectParseError( "class Foo { static { yield } }",
        "<stdin>: ERROR: \"yield\" is a reserved word and cannot be used in strict mode\n" "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n");
    expectParseError( "class Foo { static { await } }", "<stdin>: ERROR: The keyword \"await\" cannot be used here:\n");
    expectParseError( "class Foo { static { return } }", "<stdin>: ERROR: A return statement cannot be used here:\n");
    expectParseError( "class Foo { static { break } }", "<stdin>: ERROR: Cannot use \"break\" here:\n");
    expectParseError( "class Foo { static { continue } }", "<stdin>: ERROR: Cannot use \"continue\" here:\n");
    expectParseError( "x: { class Foo { static { break x } } }", "<stdin>: ERROR: There is no containing label named \"x\"\n");
    expectParseError( "x: { class Foo { static { continue x } } }", "<stdin>: ERROR: There is no containing label named \"x\"\n");
    expectPrintedMangle( "class Foo { static {} }", "class Foo {\n}\n");
    expectPrintedMangle( "class Foo { static { 123 } }", "class Foo {\n}\n");
    expectPrintedMangle( "class Foo { static { /* @__PURE__ */ foo() } }", "class Foo {\n}\n");
    expectPrintedMangle( "class Foo { static { foo() } }", "class Foo {\n  static {\n    foo();\n  }\n}\n");
}

TEST(JsParser, TestAutoAccessors) {
    expectPrinted( "class Foo { accessor }", "class Foo {\n  accessor;\n}\n");
    expectPrinted( "class Foo { accessor \n x }", "class Foo {\n  accessor;\n  x;\n}\n");
    expectPrinted( "class Foo { static accessor }", "class Foo {\n  static accessor;\n}\n");
    expectPrinted( "class Foo { static accessor \n x }", "class Foo {\n  static accessor;\n  x;\n}\n");
    expectPrinted( "class Foo { accessor x }", "class Foo {\n  accessor x;\n}\n");
    expectPrinted( "class Foo { accessor x = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrinted( "class Foo { accessor [x] }", "class Foo {\n  accessor [x];\n}\n");
    expectPrinted( "class Foo { accessor [x] = y }", "class Foo {\n  accessor [x] = y;\n}\n");
    expectPrinted( "class Foo { static accessor x }", "class Foo {\n  static accessor x;\n}\n");
    expectPrinted( "class Foo { static accessor [x] }", "class Foo {\n  static accessor [x];\n}\n");
    expectPrinted( "class Foo { static accessor x = y }", "class Foo {\n  static accessor x = y;\n}\n");
    expectPrinted( "class Foo { static accessor [x] = y }", "class Foo {\n  static accessor [x] = y;\n}\n");
    expectPrinted( "Foo = class { accessor x }", "Foo = class {\n  accessor x;\n};\n");
    expectPrinted( "Foo = class { accessor [x] }", "Foo = class {\n  accessor [x];\n};\n");
    expectPrinted( "Foo = class { accessor x = y }", "Foo = class {\n  accessor x = y;\n};\n");
    expectPrinted( "Foo = class { accessor [x] = y }", "Foo = class {\n  accessor [x] = y;\n};\n");
    expectPrinted( "Foo = class { static accessor x }", "Foo = class {\n  static accessor x;\n};\n");
    expectPrinted( "Foo = class { static accessor [x] }", "Foo = class {\n  static accessor [x];\n};\n");
    expectPrinted( "Foo = class { static accessor x = y }", "Foo = class {\n  static accessor x = y;\n};\n");
    expectPrinted( "class Foo { accessor get }", "class Foo {\n  accessor get;\n}\n");
    expectPrinted( "class Foo { get accessor() {} }", "class Foo {\n  get accessor() {\n  }\n}\n");
    expectParseError( "class Foo { accessor x() {} }", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "class Foo { accessor get x() {} }", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "class Foo { get accessor x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectPrinted( "Foo = { get accessor() {} }", "Foo = { get accessor() {\n} };\n");
    expectParseError( "Foo = { accessor x }", "<stdin>: ERROR: Expected \"}\" but found \"x\"\n");
    expectParseError( "Foo = { accessor x() {} }", "<stdin>: ERROR: Expected \"}\" but found \"x\"\n");
    expectParseError( "Foo = { get accessor x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectParseError( "class Foo { accessor x, y }", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectParseError( "class Foo { static accessor x, y }", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectParseError( "Foo = class { accessor x, y }", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectParseError( "Foo = class { static accessor x, y }", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
}

TEST(JsParser, TestDecorators) {
    expectPrinted( "@x @y class Foo {}", "@x @y class Foo {\n}\n");
    expectPrinted( "@x @y export class Foo {}", "@x @y export class Foo {\n}\n");
    expectPrinted( "@x @y export default class Foo {}", "@x @y export default class Foo {\n}\n");
    expectPrinted( "_ = @x @y class {}", "_ = @x @y class {\n};\n");
    expectPrinted( "class Foo { @x y }", "class Foo {\n  @x y;\n}\n");
    expectPrinted( "class Foo { @x y() {} }", "class Foo {\n  @x y() {\n  }\n}\n");
    expectPrinted( "class Foo { @x static y }", "class Foo {\n  @x static y;\n}\n");
    expectPrinted( "class Foo { @x static y() {} }", "class Foo {\n  @x static y() {\n  }\n}\n");
    expectPrinted( "class Foo { @x accessor y }", "class Foo {\n  @x accessor y;\n}\n");
    expectPrinted( "class Foo { @x #y }", "class Foo {\n  @x #y;\n}\n");
    expectPrinted( "class Foo { @x #y() {} }", "class Foo {\n  @x #y() {\n  }\n}\n");
    expectPrinted( "class Foo { @x static #y }", "class Foo {\n  @x static #y;\n}\n");
    expectPrinted( "class Foo { @x static #y() {} }", "class Foo {\n  @x static #y() {\n  }\n}\n");
    expectPrinted( "class Foo { @x accessor #y }", "class Foo {\n  @x accessor #y;\n}\n");
    expectParseError( "class Foo { x(@y z) {} }", "<stdin>: ERROR: Parameter decorators are not allowed in JavaScript\n");
    expectParseError( "class Foo { @x static {} }", "<stdin>: ERROR: Expected \";\" but found \"{\"\n");
    expectPrinted( "@\na\n(\n)\n@\n(\nb\n)\nclass\nFoo\n{\n}\n", "@a()\n@b\nclass Foo {\n}\n");
    expectPrinted( "@(a, b) class Foo {}", "@(a, b) class Foo {\n}\n");
    expectPrinted( "@x() class Foo {}", "@x() class Foo {\n}\n");
    expectPrinted( "@x.y() class Foo {}", "@x.y() class Foo {\n}\n");
    expectPrinted( "@(() => {}) class Foo {}", "@(() => {\n}) class Foo {\n}\n");
    expectPrinted( "class Foo { #x = @y.#x.y.#x class {} }", "class Foo {\n  #x = @y.#x.y.#x class {\n  };\n}\n");
    expectParseError( "@123 class Foo {}", "<stdin>: ERROR: Expected identifier but found \"123\"\n");
    expectParseError( "@x[y] class Foo {}", "<stdin>: ERROR: Expected \";\" but found \"class\"\n");
    expectParseError( "@x?.() class Foo {}", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "@x?.y() class Foo {}",
        "<stdin>: ERROR: JavaScript decorator syntax does not allow \"?.\" here\n" "<stdin>: NOTE: Wrap this decorator in parentheses to allow arbitrary expressions:\n");
    expectParseError( "@x?.[y]() class Foo {}", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseError( "@new Function() class Foo {}", "<stdin>: ERROR: Expected identifier but found \"new\"\n");
    expectParseError( "@() => {} class Foo {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "x = @y function() {}", "<stdin>: ERROR: Expected \"class\" but found \"function\"\n");
    // See: https://github.com/microsoft/TypeScript/issues/55336
    expectParseError( "@x().y() class Foo {}",
        "<stdin>: ERROR: JavaScript decorator syntax does not allow \".\" after a call expression\n" "<stdin>: NOTE: Wrap this decorator in parentheses to allow arbitrary expressions:\n");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "@dec class Foo {}",
        "var _Foo_decorators, _init;\n"
        "_Foo_decorators = [dec];\n"
        "class Foo {\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "Foo = __decorateElement(_init, 0, \"Foo\", _Foo_decorators, Foo);\n"
        "__runInitializers(_init, 1, Foo);\n"
        "");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "class Foo { @dec x }",
        "var _x_dec, _init;\n"
        "_x_dec = [dec];\n"
        "class Foo {\n"
        "  constructor() {\n"
        "    __publicField(this, \"x\", __runInitializers(_init, 8, this)), __runInitializers(_init, 11, this);\n"
        "  }\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "__decorateElement(_init, 5, \"x\", _x_dec, Foo);\n"
        "__decoratorMetadata(_init, Foo);\n"
        "");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "class Foo { @dec x() {} }",
        "var _x_dec, _init;\n"
        "_x_dec = [dec];\n"
        "class Foo {\n"
        "  constructor() {\n"
        "    __runInitializers(_init, 5, this);\n"
        "  }\n"
        "  x() {\n"
        "  }\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "__decorateElement(_init, 1, \"x\", _x_dec, Foo);\n"
        "__decoratorMetadata(_init, Foo);\n"
        "");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "class Foo { @dec accessor x }",
        "var _x_dec, _init, _x;\n"
        "_x_dec = [dec];\n"
        "class Foo {\n"
        "  constructor() {\n"
        "    __privateAdd(this, _x, __runInitializers(_init, 8, this)), __runInitializers(_init, 11, this);\n"
        "  }\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "_x = new WeakMap();\n"
        "__decorateElement(_init, 4, \"x\", _x_dec, Foo, _x);\n"
        "__decoratorMetadata(_init, Foo);\n"
        "");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "class Foo { @dec static x }",
        "var _x_dec, _init;\n"
        "_x_dec = [dec];\n"
        "class Foo {\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "__decorateElement(_init, 13, \"x\", _x_dec, Foo);\n"
        "__decoratorMetadata(_init, Foo);\n"
        "__publicField(Foo, \"x\", __runInitializers(_init, 8, Foo)), __runInitializers(_init, 11, Foo);\n"
        "");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "class Foo { @dec static x() {} }",
        "var _x_dec, _init;\n"
        "_x_dec = [dec];\n"
        "class Foo {\n"
        "  static x() {\n"
        "  }\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "__decorateElement(_init, 9, \"x\", _x_dec, Foo);\n"
        "__decoratorMetadata(_init, Foo);\n"
        "__runInitializers(_init, 3, Foo);\n"
        "");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kDecorators, "class Foo { @dec static accessor x }",
        "var _x_dec, _init, _x;\n"
        "_x_dec = [dec];\n"
        "class Foo {\n"
        "}\n"
        "_init = __decoratorStart(null);\n"
        "_x = new WeakMap();\n"
        "__decorateElement(_init, 12, \"x\", _x_dec, Foo, _x);\n"
        "__decoratorMetadata(_init, Foo);\n"
        "__privateAdd(Foo, _x, __runInitializers(_init, 8, Foo)), __runInitializers(_init, 11, Foo);\n"
        "");
    // Check ASI for "abstract"
    expectParseError( "@x abstract class Foo {}", "<stdin>: ERROR: Expected \";\" but found \"class\"\n");
    expectParseError( "@x abstract\nclass Foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    // Check decorator locations in relation to the "export" keyword
    expectPrinted( "@x export class Foo {}", "@x export class Foo {\n}\n");
    expectPrinted( "export @x class Foo {}", "@x export class Foo {\n}\n");
    expectPrinted( "@x export default class {}", "@x export default class {\n}\n");
    expectPrinted( "export default @x class {}", "@x export default class {\n}\n");
    expectPrinted( "@x export default class Foo {}", "@x export default class Foo {\n}\n");
    expectPrinted( "export default @x class Foo {}", "@x export default class Foo {\n}\n");
    expectPrinted( "export default (@x class {})", "export default (@x class {\n});\n");
    expectPrinted( "export default (@x class Foo {})", "export default (@x class Foo {\n});\n");
    expectParseError( "export @x default class {}", "<stdin>: ERROR: Unexpected \"default\"\n");
    expectParseError( "@x export @y class Foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseError( "@x export default abstract", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseError( "@x export @y default class {}", "<stdin>: ERROR: Decorators are not valid here\n<stdin>: ERROR: Unexpected \"default\"\n");
    // Disallow TypeScript syntax in JavaScript
    expectParseError( "@x!.y!.z class Foo {}", "<stdin>: ERROR: Unexpected \"!\"\n");
}

TEST(JsParser, TestGenerator) {
    expectParseError( "(class { * foo })", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseError( "(class { * *foo() {} })", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "(class { get*foo() {} })", "<stdin>: ERROR: Expected \";\" but found \"*\"\n");
    expectParseError( "(class { set*foo() {} })", "<stdin>: ERROR: Expected \";\" but found \"*\"\n");
    expectParseError( "(class { *get foo() {} })", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "(class { *set foo() {} })", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "(class { *static foo() {} })", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "(class { *async foo() {} })", "<stdin>: ERROR: Expected \"(\" but found \"foo\"\n");
    expectParseError( "(class { async * *foo() {} })", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "function* foo() { -yield 100 }", "<stdin>: ERROR: Cannot use a \"yield\" expression here without parentheses:\n");
    expectPrinted( "function* foo() { -(yield 100) }", "function* foo() {\n  -(yield 100);\n}\n");
}

TEST(JsParser, TestYield) {
    expectParseError( "yield 100", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseError( "-yield 100", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectPrinted( "yield\n100", "yield;\n100;\n");
    const std::string noYield = "<stdin>: ERROR: The keyword \"yield\" cannot be used here:\n";
    expectParseError( "function* bar(x = yield y) {}", noYield+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "(function*(x = yield y) {})", noYield+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "({ *foo(x = yield y) {} })", noYield+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "class Foo { *foo(x = yield y) {} }", noYield+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "(class { *foo(x = yield y) {} })", noYield+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "function *foo() { function bar(x = yield y) {} }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseError( "function *foo() { (function(x = yield y) {}) }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseError( "function *foo() { ({ foo(x = yield y) {} }) }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseError( "function *foo() { class Foo { foo(x = yield y) {} } }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseError( "function *foo() { (class { foo(x = yield y) {} }) }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseError( "function *foo() { (x = yield y) => {} }", "<stdin>: ERROR: Cannot use a \"yield\" expression here:\n");
    expectPrinted( "function *foo() { x = yield }", "function* foo() {\n  x = yield;\n}\n");
    expectPrinted( "function *foo() { x = yield; }", "function* foo() {\n  x = yield;\n}\n");
    expectPrinted( "function *foo() { (x = yield) }", "function* foo() {\n  x = yield;\n}\n");
    expectPrinted( "function *foo() { [x = yield] }", "function* foo() {\n  [x = yield];\n}\n");
    expectPrinted( "function *foo() { x = (yield, yield) }", "function* foo() {\n  x = (yield, yield);\n}\n");
    expectPrinted( "function *foo() { x = y ? yield : yield }", "function* foo() {\n  x = y ? yield : yield;\n}\n");
    expectParseError( "function *foo() { x = yield ? y : z }", "<stdin>: ERROR: Unexpected \"?\"\n");
    expectParseError( "function *foo() { x = yield * }", "<stdin>: ERROR: Unexpected \"}\"\n");
    expectParseError( "function *foo() { (x = yield *) }", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "function *foo() { [x = yield *] }", "<stdin>: ERROR: Unexpected \"]\"\n");
    expectPrinted( "function *foo() { x = yield y }", "function* foo() {\n  x = yield y;\n}\n");
    expectPrinted( "function *foo() { (x = yield y) }", "function* foo() {\n  x = yield y;\n}\n");
    expectPrinted( "function *foo() { x = yield \n y }", "function* foo() {\n  x = yield;\n  y;\n}\n");
    expectPrinted( "function *foo() { x = yield * y }", "function* foo() {\n  x = yield* y;\n}\n");
    expectPrinted( "function *foo() { (x = yield * y) }", "function* foo() {\n  x = yield* y;\n}\n");
    expectPrinted( "function *foo() { x = yield * \n y }", "function* foo() {\n  x = yield* y;\n}\n");
    expectParseError( "function *foo() { x = yield \n * y }", "<stdin>: ERROR: Unexpected \"*\"\n");
    expectParseError( "function foo() { (x = yield y) }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectPrinted( "function foo() { x = yield * y }", "function foo() {\n  x = yield * y;\n}\n");
    expectPrinted( "function foo() { (x = yield * y) }", "function foo() {\n  x = yield * y;\n}\n");
    expectParseError( "function *foo() { (x = \\u0079ield) }", "<stdin>: ERROR: The keyword \"yield\" cannot be escaped\n");
    expectParseError( "function *foo() { (x = \\u0079ield* y) }", "<stdin>: ERROR: The keyword \"yield\" cannot be escaped\n");
    // Yield as an identifier
    expectPrinted( "({yield} = x)", "({ yield } = x);\n");
    expectPrinted( "let x = {yield}", "let x = { yield };\n");
    expectPrinted( "function* yield() {}", "function* yield() {\n}\n");
    expectPrinted( "function foo() { ({yield} = x) }", "function foo() {\n  ({ yield } = x);\n}\n");
    expectPrinted( "function foo() { let x = {yield} }", "function foo() {\n  let x = { yield };\n}\n");
    expectParseError( "function *foo() { ({yield} = x) }", "<stdin>: ERROR: Cannot use \"yield\" as an identifier here:\n");
    expectParseError( "function *foo() { let x = {yield} }", "<stdin>: ERROR: Cannot use \"yield\" as an identifier here:\n");
    // Yield as a declaration
    expectPrinted( "({ *yield() {} })", "({ *yield() {\n} });\n");
    expectPrinted( "(class { *yield() {} })", "(class {\n  *yield() {\n  }\n});\n");
    expectPrinted( "class Foo { *yield() {} }", "class Foo {\n  *yield() {\n  }\n}\n");
    expectPrinted( "function* yield() {}", "function* yield() {\n}\n");
    expectParseError( "(function* yield() {})", "<stdin>: ERROR: A generator function expression cannot be named \"yield\"\n");
    // Yield as an async declaration
    expectPrinted( "({ async *yield() {} })", "({ async *yield() {\n} });\n");
    expectPrinted( "(class { async *yield() {} })", "(class {\n  async *yield() {\n  }\n});\n");
    expectPrinted( "class Foo { async *yield() {} }", "class Foo {\n  async *yield() {\n  }\n}\n");
    expectPrinted( "async function* yield() {}", "async function* yield() {\n}\n");
    expectParseError( "(async function* yield() {})", "<stdin>: ERROR: A generator function expression cannot be named \"yield\"\n");
}

TEST(JsParser, TestAsync) {
    expectPrinted( "function foo() { await }", "function foo() {\n  await;\n}\n");
    expectPrinted( "async function foo() { await 0 }", "async function foo() {\n  await 0;\n}\n");
    expectParseError( "async function() {}", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectPrinted( "-async function foo() { await 0 }", "-async function foo() {\n  await 0;\n};\n");
    expectPrinted( "-async function() { await 0 }", "-async function() {\n  await 0;\n};\n");
    expectPrinted( "1 - async function foo() { await 0 }", "1 - async function foo() {\n  await 0;\n};\n");
    expectPrinted( "1 - async function() { await 0 }", "1 - async function() {\n  await 0;\n};\n");
    expectPrinted( "(async function foo() { await 0 })", "(async function foo() {\n  await 0;\n});\n");
    expectPrinted( "(async function() { await 0 })", "(async function() {\n  await 0;\n});\n");
    expectPrinted( "(x, async function foo() { await 0 })", "x, async function foo() {\n  await 0;\n};\n");
    expectPrinted( "(x, async function() { await 0 })", "x, async function() {\n  await 0;\n};\n");
    expectPrinted( "new async function() { await 0 }", "new async function() {\n  await 0;\n}();\n");
    expectPrinted( "new async function() { await 0 }.x", "new async function() {\n  await 0;\n}.x();\n");
    const std::string friendlyAwaitError = "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n";
    const std::string friendlyAwaitErrorWithNote = friendlyAwaitError + "<stdin>: NOTE: Consider adding the \"async\" keyword here:\n";
    expectPrinted( "async", "async;\n");
    expectPrinted( "async + 1", "async + 1;\n");
    expectPrinted( "async => {}", "(async) => {\n};\n");
    expectPrinted( "(async, 1)", "async, 1;\n");
    expectPrinted( "(async, x) => {}", "(async, x) => {\n};\n");
    expectPrinted( "async ()", "async();\n");
    expectPrinted( "async (x)", "async(x);\n");
    expectPrinted( "async (...x)", "async(...x);\n");
    expectPrinted( "async (...x, ...y)", "async(...x, ...y);\n");
    expectPrinted( "async () => {}", "async () => {\n};\n");
    expectPrinted( "async x => {}", "async (x) => {\n};\n");
    expectPrinted( "async (x) => {}", "async (x) => {\n};\n");
    expectPrinted( "async (...x) => {}", "async (...x) => {\n};\n");
    expectPrinted( "async x => await 0", "async (x) => await 0;\n");
    expectPrinted( "async () => await 0", "async () => await 0;\n");
    expectPrinted( "new async()", "new async();\n");
    expectPrinted( "new async().x", "new async().x;\n");
    expectPrinted( "new (async())", "new (async())();\n");
    expectPrinted( "new (async().x)", "new (async()).x();\n");
    expectParseError( "async x;", "<stdin>: ERROR: Expected \"=>\" but found \";\"\n");
    expectParseError( "async (...x,) => {}", "<stdin>: ERROR: Unexpected \",\" after rest pattern\n");
    expectParseError( "async => await 0", friendlyAwaitErrorWithNote);
    expectParseError( "new async => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseError( "new async () => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectPrinted( "(async x => y), z", "(async (x) => y), z;\n");
    expectPrinted( "(async x => y, z)", "async (x) => y, z;\n");
    expectPrinted( "(async x => (y, z))", "(async (x) => (y, z));\n");
    expectPrinted( "(async (x) => y), z", "(async (x) => y), z;\n");
    expectPrinted( "(async (x) => y, z)", "async (x) => y, z;\n");
    expectPrinted( "(async (x) => (y, z))", "(async (x) => (y, z));\n");
    expectPrinted( "async x => y, z", "async (x) => y, z;\n");
    expectPrinted( "async x => (y, z)", "async (x) => (y, z);\n");
    expectPrinted( "async (x) => y, z", "async (x) => y, z;\n");
    expectPrinted( "async (x) => (y, z)", "async (x) => (y, z);\n");
    expectPrinted( "export default async x => (y, z)", "export default async (x) => (y, z);\n");
    expectPrinted( "export default async (x) => (y, z)", "export default async (x) => (y, z);\n");
    expectParseError( "export default async x => y, z", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectParseError( "export default async (x) => y, z", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectPrinted( "class Foo { async async() {} }", "class Foo {\n  async async() {\n  }\n}\n");
    expectPrinted( "(class { async async() {} })", "(class {\n  async async() {\n  }\n});\n");
    expectPrinted( "({ async async() {} })", "({ async async() {\n} });\n");
    expectParseError( "class Foo { async async }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseError( "(class { async async })", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseError( "({ async async })", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    const std::string noAwait = "<stdin>: ERROR: The keyword \"await\" cannot be used here:\n";
    expectParseError( "async function bar(x = await y) {}", noAwait+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "async (function(x = await y) {})", friendlyAwaitError);
    expectParseError( "async ({ foo(x = await y) {} })", friendlyAwaitError);
    expectParseError( "class Foo { async foo(x = await y) {} }", noAwait+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "(class { async foo(x = await y) {} })", noAwait+"<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "async function foo() { function bar(x = await y) {} }", friendlyAwaitError);
    expectParseError( "async function foo() { (function(x = await y) {}) }", friendlyAwaitError);
    expectParseError( "async function foo() { ({ foo(x = await y) {} }) }", friendlyAwaitError);
    expectParseError( "async function foo() { class Foo { foo(x = await y) {} } }", friendlyAwaitError);
    expectParseError( "async function foo() { (class { foo(x = await y) {} }) }", friendlyAwaitError);
    expectParseError( "async function foo() { (x = await y) => {} }", "<stdin>: ERROR: Cannot use an \"await\" expression here:\n");
    expectParseError( "async function foo(x = await y) {}", "<stdin>: ERROR: The keyword \"await\" cannot be used here:\n<stdin>: ERROR: Expected \")\" but found \"y\"\n");
    expectParseError( "async function foo({ [await y]: x }) {}", "<stdin>: ERROR: The keyword \"await\" cannot be used here:\n<stdin>: ERROR: Expected \"]\" but found \"y\"\n");
    expectPrinted( "async function foo() { (x = await y) }", "async function foo() {\n  x = await y;\n}\n");
    expectParseError( "function foo() { (x = await y) }", friendlyAwaitErrorWithNote);
    // Newlines
    expectPrinted( "(class { async \n foo() {} })", "(class {\n  async;\n  foo() {\n  }\n});\n");
    expectPrinted( "(class { async \n *foo() {} })", "(class {\n  async;\n  *foo() {\n  }\n});\n");
    expectParseError( "({ async \n foo() {} })", "<stdin>: ERROR: Expected \"}\" but found \"foo\"\n");
    expectParseError( "({ async \n *foo() {} })", "<stdin>: ERROR: Expected \"}\" but found \"*\"\n");
    // Top-level await
    expectPrinted( "await foo;", "await foo;\n");
    expectPrinted( "for await(foo of bar);", "for await (foo of bar) ;\n");
    expectParseError( "function foo() { await foo }", friendlyAwaitErrorWithNote);
    expectParseError( "function foo() { for await(foo of bar); }", "<stdin>: ERROR: Cannot use \"await\" outside an async function\n");
    expectPrinted( "function foo(x = await) {}", "function foo(x = await) {\n}\n");
    expectParseError( "function foo(x = await y) {}", friendlyAwaitError);
    expectPrinted( "(function(x = await) {})", "(function(x = await) {\n});\n");
    expectParseError( "(function(x = await y) {})", friendlyAwaitError);
    expectPrinted( "({ foo(x = await) {} })", "({ foo(x = await) {\n} });\n");
    expectParseError( "({ foo(x = await y) {} })", friendlyAwaitError);
    expectPrinted( "class Foo { foo(x = await) {} }", "class Foo {\n  foo(x = await) {\n  }\n}\n");
    expectParseError( "class Foo { foo(x = await y) {} }", friendlyAwaitError);
    expectPrinted( "(class { foo(x = await) {} })", "(class {\n  foo(x = await) {\n  }\n});\n");
    expectParseError( "(class { foo(x = await y) {} })", friendlyAwaitError);
    expectParseError( "(x = await) => {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "(x = await y) => {}", "<stdin>: ERROR: Cannot use an \"await\" expression here:\n");
    expectParseError( "(x = await)", "<stdin>: ERROR: Unexpected \")\"\n");
    expectPrinted( "(x = await y)", "x = await y;\n");
    expectParseError( "async (x = await) => {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "async (x = await y) => {}", "<stdin>: ERROR: Cannot use an \"await\" expression here:\n");
    expectPrinted( "async(x = await y)", "async(x = await y);\n");
    // Keywords with escapes
    expectPrinted( "\\u0061sync", "async;\n");
    expectPrinted( "(\\u0061sync)", "async;\n");
    expectPrinted( "function foo() { \\u0061wait }", "function foo() {\n  await;\n}\n");
    expectPrinted( "function foo() { var \\u0061wait }", "function foo() {\n  var await;\n}\n");
    expectParseError( "\\u0061wait", "<stdin>: ERROR: The keyword \"await\" cannot be escaped\n");
    expectParseError( "var \\u0061wait", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "async function foo() { \\u0061wait }", "<stdin>: ERROR: The keyword \"await\" cannot be escaped\n");
    expectParseError( "async function foo() { var \\u0061wait }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "\\u0061sync x => {}", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "\\u0061sync () => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseError( "\\u0061sync function foo() {}", "<stdin>: ERROR: Expected \";\" but found \"function\"\n");
    expectParseError( "({ \\u0061sync foo() {} })", "<stdin>: ERROR: Expected \"}\" but found \"foo\"\n");
    expectParseError( "({ \\u0061sync *foo() {} })", "<stdin>: ERROR: Expected \"}\" but found \"*\"\n");
    // For-await
    expectParseError( "for await(;;);", "<stdin>: ERROR: Unexpected \";\"\n");
    expectParseError( "for await(x in y);", "<stdin>: ERROR: Expected \"of\" but found \"in\"\n");
    expectParseError( "async function foo(){for await(;;);}", "<stdin>: ERROR: Unexpected \";\"\n");
    expectParseError( "async function foo(){for await(let x;;);}", "<stdin>: ERROR: Expected \"of\" but found \";\"\n");
    expectPrinted( "async function foo(){for await(x of y);}", "async function foo() {\n  for await (x of y) ;\n}\n");
    expectPrinted( "async function foo(){for await(let x of y);}", "async function foo() {\n  for await (let x of y) ;\n}\n");
    // Await as an identifier
    expectPrinted( "(function await() {})", "(function await() {\n});\n");
    expectPrinted( "function foo() { ({await} = x) }", "function foo() {\n  ({ await } = x);\n}\n");
    expectPrinted( "function foo() { let x = {await} }", "function foo() {\n  let x = { await };\n}\n");
    expectParseError( "({await} = x)", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "let x = {await}", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "class await {}", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "(class await {})", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "function await() {}", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "async function foo() { ({await} = x) }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    expectParseError( "async function foo() { let x = {await} }", "<stdin>: ERROR: Cannot use \"await\" as an identifier here:\n");
    // Await as a declaration
    expectPrinted( "({ async await() {} })", "({ async await() {\n} });\n");
    expectPrinted( "(class { async await() {} })", "(class {\n  async await() {\n  }\n});\n");
    expectPrinted( "class Foo { async await() {} }", "class Foo {\n  async await() {\n  }\n}\n");
    expectParseError( "async function await() {}", "<stdin>: ERROR: An async function cannot be named \"await\"\n");
    expectParseError( "(async function await() {})", "<stdin>: ERROR: An async function cannot be named \"await\"\n");
    // Await as a generator declaration
    expectPrinted( "({ async *await() {} })", "({ async *await() {\n} });\n");
    expectPrinted( "(class { async *await() {} })", "(class {\n  async *await() {\n  }\n});\n");
    expectPrinted( "class Foo { async *await() {} }", "class Foo {\n  async *await() {\n  }\n}\n");
    expectParseError( "async function* await() {}", "<stdin>: ERROR: An async function cannot be named \"await\"\n");
    expectParseError( "(async function* await() {})", "<stdin>: ERROR: An async function cannot be named \"await\"\n");
}

TEST(JsParser, TestLabels) {
    expectPrinted( "{a:b}", "{\n  a: b;\n}\n");
    expectPrinted( "({a:b})", "({ a: b });\n");
    expectParseError( "while (1) break x", "<stdin>: ERROR: There is no containing label named \"x\"\n");
    expectParseError( "while (1) continue x", "<stdin>: ERROR: There is no containing label named \"x\"\n");
    expectPrinted( "x: y: z: 1", "x: y: z: 1;\n");
    expectPrinted( "x: 1; y: 2; x: 3", "x: 1;\ny: 2;\nx: 3;\n");
    expectPrinted( "x: (() => { x: 1; })()", "x: (() => {\n  x: 1;\n})();\n");
    expectPrinted( "x: ({ f() { x: 1; } }).f()", "x: ({ f() {\n  x: 1;\n} }).f();\n");
    expectPrinted( "x: (function() { x: 1; })()", "x: (function() {\n  x: 1;\n})();\n");
    expectParseError( "x: y: x: 1", "<stdin>: ERROR: Duplicate label \"x\"\n<stdin>: NOTE: The original label \"x\" is here:\n");
    expectPrinted( "x: break x", "x: break x;\n");
    expectPrinted( "x: { break x; foo() }", "x: {\n  break x;\n  foo();\n}\n");
    expectPrinted( "x: { y: { z: { foo(); break x; } } }", "x: {\n  y: {\n    z: {\n      foo();\n      break x;\n    }\n  }\n}\n");
    expectPrinted( "x: { class X { static { new X } } }", "x: {\n  class X {\n    static {\n      new X();\n    }\n  }\n}\n");
    expectPrintedMangle( "x: break x", "");
    expectPrintedMangle( "x: { break x; foo() }", "");
    expectPrintedMangle( "y: while (foo()) x: { break x; foo() }", "for (; foo(); ) ;\n");
    expectPrintedMangle( "y: while (foo()) x: { break y; foo() }", "y: for (; foo(); ) break y;\n");
    expectPrintedMangle( "x: { y: { z: { foo(); break x; } } }", "x: {\n  foo();\n  break x;\n}\n");
    expectPrintedMangle( "x: { class X { static { new X } } }", "{\n  class X {\n    static {\n      new X();\n    }\n  }\n}\n");
}

TEST(JsParser, TestArrow) {
    expectParseError( "({a: b, c() {}}) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "({a: b, get c() {}}) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "({a: b, set c(x) {}}) => {}", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "x = ([ (y) ]) => 0", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "x = ([ ...(y) ]) => 0", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "x = ({ (y) }) => 0", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseError( "x = ({ y: (z) }) => 0", "<stdin>: ERROR: Invalid binding pattern\n");
    expectParseError( "x = ({ ...(y) }) => 0", "<stdin>: ERROR: Invalid binding pattern\n");
    expectPrinted( "x = ([ y = [ (z) ] ]) => 0", "x = ([y = [z]]) => 0;\n");
    expectPrinted( "x = ([ y = [ ...(z) ] ]) => 0", "x = ([y = [...z]]) => 0;\n");
    expectPrinted( "x = ({ y = { y: (z) } }) => 0", "x = ({ y = { y: z } }) => 0;\n");
    expectPrinted( "x = ({ y = { ...(y) } }) => 0", "x = ({ y = { ...y } }) => 0;\n");
    expectPrinted( "x => function() {}", "(x) => function() {\n};\n");
    expectPrinted( "(x) => function() {}", "(x) => function() {\n};\n");
    expectPrinted( "(x => function() {})", "((x) => function() {\n});\n");
    expectPrinted( "(x = () => {}) => {}", "(x = () => {\n}) => {\n};\n");
    expectPrinted( "async (x = () => {}) => {}", "async (x = () => {\n}) => {\n};\n");
    expectParseError( "()\n=> {}", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "x\n=> {}", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "async x\n=> {}", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "async ()\n=> {}", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "(()\n=> {})", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "(x\n=> {})", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "(async x\n=> {})", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectParseError( "(async ()\n=> {})", "<stdin>: ERROR: Unexpected newline before \"=>\"\n");
    expectPrinted( "(() => {}) ? a : b", "(() => {\n}) ? a : b;\n");
    expectPrintedMangle( "(() => {}) ? a : b", "a;\n");
    expectParseError( "() => {} ? a : b", "<stdin>: ERROR: Expected \";\" but found \"?\"\n");
    expectPrinted( "1 < (() => {})", "1 < (() => {\n});\n");
    expectParseError( "1 < () => {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "(...x = y) => {}", "<stdin>: ERROR: A rest argument cannot have a default initializer\n");
    expectParseError( "([...x = y]) => {}", "<stdin>: ERROR: A rest argument cannot have a default initializer\n");
    // Can assign an arrow function
    expectPrinted( "y = x => {}", "y = (x) => {\n};\n");
    expectPrinted( "y = () => {}", "y = () => {\n};\n");
    expectPrinted( "y = (x) => {}", "y = (x) => {\n};\n");
    expectPrinted( "y = async x => {}", "y = async (x) => {\n};\n");
    expectPrinted( "y = async () => {}", "y = async () => {\n};\n");
    expectPrinted( "y = async (x) => {}", "y = async (x) => {\n};\n");
    // Cannot add an arrow function
    expectPrinted( "1 + function () {}", "1 + function() {\n};\n");
    expectPrinted( "1 + async function () {}", "1 + async function() {\n};\n");
    expectParseError( "1 + x => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseError( "1 + () => {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "1 + (x) => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseError( "1 + async x => {}", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "1 + async () => {}", "<stdin>: ERROR: Unexpected \"=>\"\n");
    expectParseError( "1 + async (x) => {}", "<stdin>: ERROR: Unexpected \"=>\"\n");
    // Cannot extend an arrow function
    expectPrinted( "class Foo extends function () {} {}", "class Foo extends function() {\n} {\n}\n");
    expectPrinted( "class Foo extends async function () {} {}", "class Foo extends async function() {\n} {\n}\n");
    expectParseError( "class Foo extends x => {} {}", "<stdin>: ERROR: Expected \"{\" but found \"=>\"\n");
    expectParseError( "class Foo extends () => {} {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "class Foo extends (x) => {} {}", "<stdin>: ERROR: Expected \"{\" but found \"=>\"\n");
    expectParseError( "class Foo extends async x => {} {}", "<stdin>: ERROR: Expected \"{\" but found \"x\"\n");
    expectParseError( "class Foo extends async () => {} {}", "<stdin>: ERROR: Unexpected \"=>\"\n");
    expectParseError( "class Foo extends async (x) => {} {}", "<stdin>: ERROR: Unexpected \"=>\"\n");
    expectParseError( "(class extends x => {} {})", "<stdin>: ERROR: Expected \"{\" but found \"=>\"\n");
    expectParseError( "(class extends () => {} {})", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "(class extends (x) => {} {})", "<stdin>: ERROR: Expected \"{\" but found \"=>\"\n");
    expectParseError( "(class extends async x => {} {})", "<stdin>: ERROR: Expected \"{\" but found \"x\"\n");
    expectParseError( "(class extends async () => {} {})", "<stdin>: ERROR: Unexpected \"=>\"\n");
    expectParseError( "(class extends async (x) => {} {})", "<stdin>: ERROR: Unexpected \"=>\"\n");
    expectParseError( "() => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "x => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "async () => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "async x => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "async (x) => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "0, async () => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "0, async x => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "0, async (x) => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectPrinted( "() => {}\n(0)", "() => {\n};\n0;\n");
    expectPrinted( "x => {}\n(0)", "(x) => {\n};\n0;\n");
    expectPrinted( "async () => {}\n(0)", "async () => {\n};\n0;\n");
    expectPrinted( "async x => {}\n(0)", "async (x) => {\n};\n0;\n");
    expectPrinted( "async (x) => {}\n(0)", "async (x) => {\n};\n0;\n");
    expectPrinted( "() => {}\n,0", "() => {\n}, 0;\n");
    expectPrinted( "x => {}\n,0", "(x) => {\n}, 0;\n");
    expectPrinted( "async () => {}\n,0", "async () => {\n}, 0;\n");
    expectPrinted( "async x => {}\n,0", "async (x) => {\n}, 0;\n");
    expectPrinted( "async (x) => {}\n,0", "async (x) => {\n}, 0;\n");
    expectPrinted( "(() => {})\n(0)", "/* @__PURE__ */ (() => {\n})(0);\n");
    expectPrinted( "(x => {})\n(0)", "/* @__PURE__ */ ((x) => {\n})(0);\n");
    expectPrinted( "(async () => {})\n(0)", "(async () => {\n})(0);\n");
    expectPrinted( "(async x => {})\n(0)", "(async (x) => {\n})(0);\n");
    expectPrinted( "(async (x) => {})\n(0)", "(async (x) => {\n})(0);\n");
    expectParseError( "y = () => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "y = x => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "y = async () => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "y = async x => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseError( "y = async (x) => {}(0)", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectPrinted( "y = () => {}\n(0)", "y = () => {\n};\n0;\n");
    expectPrinted( "y = x => {}\n(0)", "y = (x) => {\n};\n0;\n");
    expectPrinted( "y = async () => {}\n(0)", "y = async () => {\n};\n0;\n");
    expectPrinted( "y = async x => {}\n(0)", "y = async (x) => {\n};\n0;\n");
    expectPrinted( "y = async (x) => {}\n(0)", "y = async (x) => {\n};\n0;\n");
    expectPrinted( "y = () => {}\n,0", "y = () => {\n}, 0;\n");
    expectPrinted( "y = x => {}\n,0", "y = (x) => {\n}, 0;\n");
    expectPrinted( "y = async () => {}\n,0", "y = async () => {\n}, 0;\n");
    expectPrinted( "y = async x => {}\n,0", "y = async (x) => {\n}, 0;\n");
    expectPrinted( "y = async (x) => {}\n,0", "y = async (x) => {\n}, 0;\n");
    expectPrinted( "y = (() => {})\n(0)", "y = /* @__PURE__ */ (() => {\n})(0);\n");
    expectPrinted( "y = (x => {})\n(0)", "y = /* @__PURE__ */ ((x) => {\n})(0);\n");
    expectPrinted( "y = (async () => {})\n(0)", "y = (async () => {\n})(0);\n");
    expectPrinted( "y = (async x => {})\n(0)", "y = (async (x) => {\n})(0);\n");
    expectPrinted( "y = (async (x) => {})\n(0)", "y = (async (x) => {\n})(0);\n");
    expectParseError( "(() => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(x => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(async () => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(async x => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(async (x) => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(() => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(x => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(async () => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(async x => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "(async (x) => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectPrinted( "(() => {}\n,0)", "() => {\n}, 0;\n");
    expectPrinted( "(x => {}\n,0)", "(x) => {\n}, 0;\n");
    expectPrinted( "(async () => {}\n,0)", "async () => {\n}, 0;\n");
    expectPrinted( "(async x => {}\n,0)", "async (x) => {\n}, 0;\n");
    expectPrinted( "(async (x) => {}\n,0)", "async (x) => {\n}, 0;\n");
    expectPrinted( "((() => {})\n(0))", "/* @__PURE__ */ (() => {\n})(0);\n");
    expectPrinted( "((x => {})\n(0))", "/* @__PURE__ */ ((x) => {\n})(0);\n");
    expectPrinted( "((async () => {})\n(0))", "(async () => {\n})(0);\n");
    expectPrinted( "((async x => {})\n(0))", "(async (x) => {\n})(0);\n");
    expectPrinted( "((async (x) => {})\n(0))", "(async (x) => {\n})(0);\n");
    expectParseError( "y = (() => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (x => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (async () => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (async x => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (async (x) => {}(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (() => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (x => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (async () => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (async x => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectParseError( "y = (async (x) => {}\n(0))", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
    expectPrinted( "y = (() => {}\n,0)", "y = (() => {\n}, 0);\n");
    expectPrinted( "y = (x => {}\n,0)", "y = ((x) => {\n}, 0);\n");
    expectPrinted( "y = (async () => {}\n,0)", "y = (async () => {\n}, 0);\n");
    expectPrinted( "y = (async x => {}\n,0)", "y = (async (x) => {\n}, 0);\n");
    expectPrinted( "y = (async (x) => {}\n,0)", "y = (async (x) => {\n}, 0);\n");
    expectPrinted( "y = ((() => {})\n(0))", "y = /* @__PURE__ */ (() => {\n})(0);\n");
    expectPrinted( "y = ((x => {})\n(0))", "y = /* @__PURE__ */ ((x) => {\n})(0);\n");
    expectPrinted( "y = ((async () => {})\n(0))", "y = (async () => {\n})(0);\n");
    expectPrinted( "y = ((async x => {})\n(0))", "y = (async (x) => {\n})(0);\n");
    expectPrinted( "y = ((async (x) => {})\n(0))", "y = (async (x) => {\n})(0);\n");
}

TEST(JsParser, TestTemplate) {
    expectPrinted( "`\\0`", "`\\0`;\n");
    expectPrinted( "`${'\\00'}`", "`${\"\\0\"}`;\n");
    expectParseError( "`\\7`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`\\8`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`\\9`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`\\00`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`\\00${x}`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`${x}\\00`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`${x}\\00${y}`", "<stdin>: ERROR: Legacy octal escape sequences cannot be used in template literals\n");
    expectParseError( "`\\unicode`", "<stdin>: ERROR: Syntax error \"n\"\n");
    expectParseError( "`\\unicode${x}`", "<stdin>: ERROR: Syntax error \"n\"\n");
    expectParseError( "`${x}\\unicode`", "<stdin>: ERROR: Syntax error \"n\"\n");
    expectParseError( "`\\u{10FFFFF}`", "<stdin>: ERROR: Unicode escape sequence is out of range\n");
    expectPrinted( "tag`\\7`", "tag`\\7`;\n");
    expectPrinted( "tag`\\8`", "tag`\\8`;\n");
    expectPrinted( "tag`\\9`", "tag`\\9`;\n");
    expectPrinted( "tag`\\00`", "tag`\\00`;\n");
    expectPrinted( "tag`\\00${x}`", "tag`\\00${x}`;\n");
    expectPrinted( "tag`${x}\\00`", "tag`${x}\\00`;\n");
    expectPrinted( "tag`${x}\\00${y}`", "tag`${x}\\00${y}`;\n");
    expectPrinted( "tag`\\unicode`", "tag`\\unicode`;\n");
    expectPrinted( "tag`\\unicode${x}`", "tag`\\unicode${x}`;\n");
    expectPrinted( "tag`${x}\\unicode`", "tag`${x}\\unicode`;\n");
    expectPrinted( "tag`\\u{10FFFFF}`", "tag`\\u{10FFFFF}`;\n");
    expectPrinted( "new foo`bar`()", "new foo`bar`();\n");
    expectPrinted( "new (foo`bar`)()", "new foo`bar`();\n");
    expectPrinted( "(new foo)`bar`()", "new foo()`bar`();\n");
    expectPrinted( "new foo()`bar`()", "new foo()`bar`();\n");
    expectPrinted( "(new foo())`bar`()", "new foo()`bar`();\n");
    expectPrinted( "new (foo()`bar`)()", "new (foo())`bar`();\n");
    expectPrinted( "tag``", "tag``;\n");
    expectPrinted( "(a?.b)``", "(a?.b)``;\n");
    expectPrinted( "(a?.(b))``", "(a?.(b))``;\n");
    expectPrinted( "(a?.[b])``", "(a?.[b])``;\n");
    expectPrinted( "(a?.b.c)``", "(a?.b.c)``;\n");
    expectPrinted( "(a?.(b).c)``", "(a?.(b).c)``;\n");
    expectPrinted( "(a?.[b].c)``", "(a?.[b].c)``;\n");
    expectParseError( "a?.b``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b)``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b]``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b.c``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b).c``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b].c``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b)`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b]`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b.c`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b).c`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b].c`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b\n``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b)\n``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b]\n``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b.c\n``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b).c\n``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b].c\n``", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b\n`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b)\n`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b]\n`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.b.c\n`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.(b).c\n`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectParseError( "a?.[b].c\n`${d}`", "<stdin>: ERROR: Template literals cannot have an optional chain as a tag\n");
    expectPrinted( "`a${1 + `b${2}c` + 3}d`", "`a${`1b${2}c3`}d`;\n");
    expectPrintedMangle( "x = `a${1 + `b${2}c` + 3}d`", "x = `a1b2c3d`;\n");
    expectPrinted( "`a\nb`", "`a\nb`;\n");
    expectPrinted( "`a\rb`", "`a\nb`;\n");
    expectPrinted( "`a\r\nb`", "`a\nb`;\n");
    expectPrinted( "`a\\nb`", "`a\nb`;\n");
    expectPrinted( "`a\\rb`", "`a\\rb`;\n");
    expectPrinted( "`a\\r\\nb`", "`a\\r\nb`;\n");
    expectPrinted( "`a\u2028b`", "`a\\u2028b`;\n");
    expectPrinted( "`a\u2029b`", "`a\\u2029b`;\n");
    expectPrinted( "`a\n${b}`", "`a\n${b}`;\n");
    expectPrinted( "`a\r${b}`", "`a\n${b}`;\n");
    expectPrinted( "`a\r\n${b}`", "`a\n${b}`;\n");
    expectPrinted( "`a\\n${b}`", "`a\n${b}`;\n");
    expectPrinted( "`a\\r${b}`", "`a\\r${b}`;\n");
    expectPrinted( "`a\\r\\n${b}`", "`a\\r\n${b}`;\n");
    expectPrinted( "`a\u2028${b}`", "`a\\u2028${b}`;\n");
    expectPrinted( "`a\u2029${b}`", "`a\\u2029${b}`;\n");
    expectPrinted( "`${a}\nb`", "`${a}\nb`;\n");
    expectPrinted( "`${a}\rb`", "`${a}\nb`;\n");
    expectPrinted( "`${a}\r\nb`", "`${a}\nb`;\n");
    expectPrinted( "`${a}\\nb`", "`${a}\nb`;\n");
    expectPrinted( "`${a}\\rb`", "`${a}\\rb`;\n");
    expectPrinted( "`${a}\\r\\nb`", "`${a}\\r\nb`;\n");
    expectPrinted( "`${a}\u2028b`", "`${a}\\u2028b`;\n");
    expectPrinted( "`${a}\u2029b`", "`${a}\\u2029b`;\n");
    expectPrinted( "tag`a\nb`", "tag`a\nb`;\n");
    expectPrinted( "tag`a\rb`", "tag`a\nb`;\n");
    expectPrinted( "tag`a\r\nb`", "tag`a\nb`;\n");
    expectPrinted( "tag`a\\nb`", "tag`a\\nb`;\n");
    expectPrinted( "tag`a\\rb`", "tag`a\\rb`;\n");
    expectPrinted( "tag`a\\r\\nb`", "tag`a\\r\\nb`;\n");
    expectPrinted( "tag`a\u2028b`", "tag`a\u2028b`;\n");
    expectPrinted( "tag`a\u2029b`", "tag`a\u2029b`;\n");
    expectPrinted( "tag`a\n${b}`", "tag`a\n${b}`;\n");
    expectPrinted( "tag`a\r${b}`", "tag`a\n${b}`;\n");
    expectPrinted( "tag`a\r\n${b}`", "tag`a\n${b}`;\n");
    expectPrinted( "tag`a\\n${b}`", "tag`a\\n${b}`;\n");
    expectPrinted( "tag`a\\r${b}`", "tag`a\\r${b}`;\n");
    expectPrinted( "tag`a\\r\\n${b}`", "tag`a\\r\\n${b}`;\n");
    expectPrinted( "tag`a\u2028${b}`", "tag`a\u2028${b}`;\n");
    expectPrinted( "tag`a\u2029${b}`", "tag`a\u2029${b}`;\n");
    expectPrinted( "tag`${a}\nb`", "tag`${a}\nb`;\n");
    expectPrinted( "tag`${a}\rb`", "tag`${a}\nb`;\n");
    expectPrinted( "tag`${a}\r\nb`", "tag`${a}\nb`;\n");
    expectPrinted( "tag`${a}\\nb`", "tag`${a}\\nb`;\n");
    expectPrinted( "tag`${a}\\rb`", "tag`${a}\\rb`;\n");
    expectPrinted( "tag`${a}\\r\\nb`", "tag`${a}\\r\\nb`;\n");
    expectPrinted( "tag`${a}\u2028b`", "tag`${a}\u2028b`;\n");
    expectPrinted( "tag`${a}\u2029b`", "tag`${a}\u2029b`;\n");
}

TEST(JsParser, TestSwitch) {
    expectPrinted( "switch (x) { default: }", "switch (x) {\n  default:\n}\n");
    expectPrinted( "switch ((x => x + 1)(0)) { case 1: var y } y = 2", "switch (((x) => x + 1)(0)) {\n  case 1:\n    var y;\n}\ny = 2;\n");
    expectParseError( "switch (x) { default: default: }", "<stdin>: ERROR: Multiple default clauses are not allowed\n");
    expectPrintedMangle( "switch (x) {}", "x;\n");
    expectPrintedMangle( "switch (x) { case x: a(); break; case y: b(); break }", "switch (x) {\n  case x:\n    a();\n    break;\n  case y:\n    b();\n    break;\n}\n");
    expectPrintedMangle( "switch (0) { default: a() }", "a();\n");
    expectPrintedMangle( "switch (x) { default: a() }", "switch (x) {\n  default:\n    a();\n}\n");
    expectPrintedMangle( "switch (0) { case 0: a(); break; case 1: b(); break }", "a();\n");
    expectPrintedMangle( "switch (1) { case 0: a(); break; case 1: b(); break }", "b();\n");
    expectPrintedMangle( "switch (2) { case 0: a(); break; case 1: b(); break }", "");
    expectPrintedMangle( "switch (0) { case 0: a(); case 1: b(); break }", "a(), b();\n");
    expectPrintedMangle( "switch (1) { case 0: a(); case 1: b(); break }", "b();\n");
    expectPrintedMangle( "switch (2) { case 0: a(); case 1: b(); break }", "");
    expectPrintedMangle( "switch (0) { case 0: a(); break; default: b(); break }", "a();\n");
    expectPrintedMangle( "switch (1) { case 0: a(); break; default: b(); break }", "b();\n");
    expectPrintedMangle( "switch (0) { case 0: { a(); break; } case 1: b(); break }", "a();\n");
    expectPrintedMangle( "switch (0) { case 0: { var x = a(); break; } case 1: b(); break }", "var x = a();\n");
    expectPrintedMangle( "switch (0) { case 0: { let x = a(); break; } case 1: b(); break }", "{\n  let x = a();\n}\n");
    expectPrintedMangle( "switch (0) { case 0: { const x = a(); break; } case 1: b(); break }", "{\n  const x = a();\n}\n");
    expectPrintedMangle( "for (x of y) switch (0) { case 0: a(); continue; default: b(); continue }", "for (x of y) a();\n");
    expectPrintedMangle( "for (x of y) switch (1) { case 0: a(); continue; default: b(); continue }", "for (x of y) b();\n");
    expectPrintedMangle( "for (x of y) switch (0) { case 0: throw a(); default: throw b() }", "for (x of y) throw a();\n");
    expectPrintedMangle( "for (x of y) switch (1) { case 0: throw a(); default: throw b() }", "for (x of y) throw b();\n");
    expectPrintedMangle( "for (x of y) switch (0) { case 0: return a(); default: return b() }", "for (x of y) return a();\n");
    expectPrintedMangle( "for (x of y) switch (1) { case 0: return a(); default: return b() }", "for (x of y) return b();\n");
    expectPrintedMangle( "z: for (x of y) switch (0) { case 0: a(); break z; default: b(); break z }", "z: for (x of y) switch (0) {\n  case 0:\n    a();\n    break z;\n}\n");
    expectPrintedMangle( "z: for (x of y) switch (1) { case 0: a(); break z; default: b(); break z }", "z: for (x of y) switch (1) {\n  default:\n    b();\n    break z;\n}\n");
    expectPrintedMangle( "for (x of y) z: switch (0) { case 0: a(); break z; default: b(); break z }", "for (x of y) z: switch (0) {\n  case 0:\n    a();\n    break z;\n}\n");
    expectPrintedMangle( "for (x of y) z: switch (1) { case 0: a(); break z; default: b(); break z }", "for (x of y) z: switch (1) {\n  default:\n    b();\n    break z;\n}\n");
    expectPrinted( "switch (0) { case x(() => 1): y = () => 2; case x(() => 3): y = () => 4 }",
        "switch (0) {\n  case x(() => 1):\n    y = () => 2;\n  case x(() => 3):\n    y = () => 4;\n}\n");
    // The specification was changed after implementations shipped the feature.
    // Specifically "using" inside "case" and "default" inside "switch" is no
    // longer allowed: https://github.com/rbuckton/ecma262/pull/14
    const std::string usingError = "<stdin>: ERROR: Cannot use a \"using\" declaration directly inside a switch case\n" "NOTE: Wrap this declaration in a block statement to use it here.\n";
    expectParseError( "switch (x) { case 0: using y = z }\n", usingError);
    expectParseError( "switch (x) { case 0: await using y = z }\n", usingError);
    expectPrinted( "switch (x) { case 0: { using y = z } }\n", "switch (x) {\n  case 0: {\n    using y = z;\n  }\n}\n");
    expectPrinted( "switch (x) { case 0: { await using y = z } }\n", "switch (x) {\n  case 0: {\n    await using y = z;\n  }\n}\n");
}

TEST(JsParser, TestConstantFolding) {
    expectPrinted( "x = !false", "x = true;\n");
    expectPrinted( "x = !true", "x = false;\n");
    expectPrinted( "x = !!0", "x = false;\n");
    expectPrinted( "x = !!-0", "x = false;\n");
    expectPrinted( "x = !!1", "x = true;\n");
    expectPrinted( "x = !!NaN", "x = false;\n");
    expectPrinted( "x = !!Infinity", "x = true;\n");
    expectPrinted( "x = !!-Infinity", "x = true;\n");
    expectPrinted( "x = !!\"\"", "x = false;\n");
    expectPrinted( "x = !!\"x\"", "x = true;\n");
    expectPrinted( "x = !!function() {}", "x = true;\n");
    expectPrinted( "x = !!(() => {})", "x = true;\n");
    expectPrinted( "x = !!0n", "x = false;\n");
    expectPrinted( "x = !!1n", "x = true;\n");
    expectPrinted( "x = !!0b0n", "x = !!0b0n;\n");
    expectPrinted( "x = !!0b1n", "x = !!0b1n;\n");
    expectPrinted( "x = !!0o0n", "x = !!0o0n;\n");
    expectPrinted( "x = !!0o1n", "x = !!0o1n;\n");
    expectPrinted( "x = !!0x0n", "x = !!0x0n;\n");
    expectPrinted( "x = !!0x1n", "x = !!0x1n;\n");
    expectPrinted( "x = 1 ? a : b", "x = 1 ? a : b;\n");
    expectPrinted( "x = 0 ? a : b", "x = 0 ? a : b;\n");
    expectPrintedMangle( "x = 1 ? a : b", "x = a;\n");
    expectPrintedMangle( "x = 0 ? a : b", "x = b;\n");
    expectPrinted( "x = 1 && 2", "x = 2;\n");
    expectPrinted( "x = 1 || 2", "x = 1;\n");
    expectPrinted( "x = 0 && 1", "x = 0;\n");
    expectPrinted( "x = 0 || 1", "x = 1;\n");
    expectPrinted( "x = null ?? 1", "x = 1;\n");
    expectPrinted( "x = undefined ?? 1", "x = 1;\n");
    expectPrinted( "x = 0 ?? 1", "x = 0;\n");
    expectPrinted( "x = false ?? 1", "x = false;\n");
    expectPrinted( "x = \"\" ?? 1", "x = \"\";\n");
    expectPrinted( "x = typeof undefined", "x = \"undefined\";\n");
    expectPrinted( "x = typeof null", "x = \"object\";\n");
    expectPrinted( "x = typeof false", "x = \"boolean\";\n");
    expectPrinted( "x = typeof true", "x = \"boolean\";\n");
    expectPrinted( "x = typeof 123", "x = \"number\";\n");
    expectPrinted( "x = typeof 123n", "x = \"bigint\";\n");
    expectPrinted( "x = typeof 'abc'", "x = \"string\";\n");
    expectPrinted( "x = typeof function() {}", "x = \"function\";\n");
    expectPrinted( "x = typeof (() => {})", "x = \"function\";\n");
    expectPrinted( "x = typeof {}", "x = typeof {};\n");
    expectPrinted( "x = typeof []", "x = typeof [];\n");
    expectPrinted( "x = undefined === undefined", "x = true;\n");
    expectPrinted( "x = undefined !== undefined", "x = false;\n");
    expectPrinted( "x = undefined == undefined", "x = true;\n");
    expectPrinted( "x = undefined != undefined", "x = false;\n");
    expectPrinted( "x = null === null", "x = true;\n");
    expectPrinted( "x = null !== null", "x = false;\n");
    expectPrinted( "x = null == null", "x = true;\n");
    expectPrinted( "x = null != null", "x = false;\n");
    expectPrinted( "x = null === undefined", "x = false;\n");
    expectPrinted( "x = null !== undefined", "x = true;\n");
    expectPrinted( "x = null == undefined", "x = true;\n");
    expectPrinted( "x = null != undefined", "x = false;\n");
    expectPrinted( "x = undefined === null", "x = false;\n");
    expectPrinted( "x = undefined !== null", "x = true;\n");
    expectPrinted( "x = undefined == null", "x = true;\n");
    expectPrinted( "x = undefined != null", "x = false;\n");
    expectPrinted( "x = true === true", "x = true;\n");
    expectPrinted( "x = true === false", "x = false;\n");
    expectPrinted( "x = true !== true", "x = false;\n");
    expectPrinted( "x = true !== false", "x = true;\n");
    expectPrinted( "x = true == true", "x = true;\n");
    expectPrinted( "x = true == false", "x = false;\n");
    expectPrinted( "x = true != true", "x = false;\n");
    expectPrinted( "x = true != false", "x = true;\n");
    expectPrinted( "x = 1 === 1", "x = true;\n");
    expectPrinted( "x = 1 === 2", "x = false;\n");
    expectPrinted( "x = 1 === '1'", "x = false;\n");
    expectPrinted( "x = 1 == 1", "x = true;\n");
    expectPrinted( "x = 1 == 2", "x = false;\n");
    expectPrinted( "x = 1 == '1'", "x = 1 == \"1\";\n");
    expectPrinted( "x = 1 !== 1", "x = false;\n");
    expectPrinted( "x = 1 !== 2", "x = true;\n");
    expectPrinted( "x = 1 !== '1'", "x = true;\n");
    expectPrinted( "x = 1 != 1", "x = false;\n");
    expectPrinted( "x = 1 != 2", "x = true;\n");
    expectPrinted( "x = 1 != '1'", "x = 1 != \"1\";\n");
    expectPrinted( "x = 'a' === '\\x61'", "x = true;\n");
    expectPrinted( "x = 'a' === '\\x62'", "x = false;\n");
    expectPrinted( "x = 'a' === 'abc'", "x = false;\n");
    expectPrinted( "x = 'a' !== '\\x61'", "x = false;\n");
    expectPrinted( "x = 'a' !== '\\x62'", "x = true;\n");
    expectPrinted( "x = 'a' !== 'abc'", "x = true;\n");
    expectPrinted( "x = 'a' == '\\x61'", "x = true;\n");
    expectPrinted( "x = 'a' == '\\x62'", "x = false;\n");
    expectPrinted( "x = 'a' == 'abc'", "x = false;\n");
    expectPrinted( "x = 'a' != '\\x61'", "x = false;\n");
    expectPrinted( "x = 'a' != '\\x62'", "x = true;\n");
    expectPrinted( "x = 'a' != 'abc'", "x = true;\n");
    expectPrinted( "x = 'a' + 'b'", "x = \"ab\";\n");
    expectPrinted( "x = 'a' + 'bc'", "x = \"abc\";\n");
    expectPrinted( "x = 'ab' + 'c'", "x = \"abc\";\n");
    expectPrinted( "x = x + 'a' + 'b'", "x = x + \"ab\";\n");
    expectPrinted( "x = x + 'a' + 'bc'", "x = x + \"abc\";\n");
    expectPrinted( "x = x + 'ab' + 'c'", "x = x + \"abc\";\n");
    expectPrinted( "x = 'a' + 1", "x = \"a1\";\n");
    expectPrinted( "x = x * 'a' + 'b'", "x = x * \"a\" + \"b\";\n");
    expectPrinted( "x = 'string' + `template`", "x = `stringtemplate`;\n");
    expectPrinted( "x = 'string' + `a${foo}b`", "x = `stringa${foo}b`;\n");
    expectPrinted( "x = 'string' + tag`template`", "x = \"string\" + tag`template`;\n");
    expectPrinted( "x = `template` + 'string'", "x = `templatestring`;\n");
    expectPrinted( "x = `a${foo}b` + 'string'", "x = `a${foo}bstring`;\n");
    expectPrinted( "x = tag`template` + 'string'", "x = tag`template` + \"string\";\n");
    expectPrinted( "x = `template` + `a${foo}b`", "x = `templatea${foo}b`;\n");
    expectPrinted( "x = `a${foo}b` + `template`", "x = `a${foo}btemplate`;\n");
    expectPrinted( "x = `a${foo}b` + `x${bar}y`", "x = `a${foo}bx${bar}y`;\n");
    expectPrinted( "x = `a${i}${j}bb` + `xxx${bar}yyyy`", "x = `a${i}${j}bbxxx${bar}yyyy`;\n");
    expectPrinted( "x = `a${foo}bb` + `xxx${i}${j}yyyy`", "x = `a${foo}bbxxx${i}${j}yyyy`;\n");
    expectPrinted( "x = `template` + tag`template2`", "x = `template` + tag`template2`;\n");
    expectPrinted( "x = tag`template` + `template2`", "x = tag`template` + `template2`;\n");
    expectPrinted( "x = 123", "x = 123;\n");
    expectPrinted( "x = 123 .toString()", "x = 123 .toString();\n");
    expectPrinted( "x = -123", "x = -123;\n");
    expectPrinted( "x = (-123).toString()", "x = (-123).toString();\n");
    expectPrinted( "x = -0", "x = -0;\n");
    expectPrinted( "x = (-0).toString()", "x = (-0).toString();\n");
    expectPrinted( "x = -0 === 0", "x = true;\n");
    expectPrinted( "x = NaN", "x = NaN;\n");
    expectPrinted( "x = NaN.toString()", "x = NaN.toString();\n");
    expectPrinted( "x = NaN === NaN", "x = false;\n");
    expectPrinted( "x = Infinity", "x = Infinity;\n");
    expectPrinted( "x = Infinity.toString()", "x = Infinity.toString();\n");
    expectPrinted( "x = (-Infinity).toString()", "x = (-Infinity).toString();\n");
    expectPrinted( "x = Infinity === Infinity", "x = true;\n");
    expectPrinted( "x = Infinity === -Infinity", "x = false;\n");
    expectPrinted( "x = 0n === 0n", "x = true;\n");
    expectPrinted( "x = 1n === 1n", "x = true;\n");
    expectPrinted( "x = 0n === 1n", "x = false;\n");
    expectPrinted( "x = 0n !== 1n", "x = true;\n");
    expectPrinted( "x = 0n !== 0n", "x = false;\n");
    expectPrinted( "x = 123n === 1_2_3n", "x = true;\n");
    expectPrinted( "x = 0n === '1n'", "x = false;\n");
    expectPrinted( "x = 0n !== '1n'", "x = true;\n");
    expectPrinted( "x = 0n === 0b0n", "x = 0n === 0b0n;\n");
    expectPrinted( "x = 0n === 0o0n", "x = 0n === 0o0n;\n");
    expectPrinted( "x = 0n === 0x0n", "x = 0n === 0x0n;\n");
    expectPrinted( "x = 0b0n === 0b0n", "x = true;\n");
    expectPrinted( "x = 0o0n === 0o0n", "x = true;\n");
    expectPrinted( "x = 0x0n === 0x0n", "x = true;\n");
    // We support folding strings from sibling AST nodes since that ends up being
    // equivalent with string addition. For example, "(x + 'a') + 'b'" is the
    // same as "x + 'ab'". However, this is not true for numbers. We can't turn
    // "(x + 1) + '2'" into "x + '12'". These tests check for this edge case.
    expectPrinted( "x = 'a' + 'b' + y", "x = \"ab\" + y;\n");
    expectPrinted( "x = y + 'a' + 'b'", "x = y + \"ab\";\n");
    expectPrinted( "x = '3' + 4 + y", "x = \"34\" + y;\n");
    expectPrinted( "x = y + 4 + '5'", "x = y + 4 + \"5\";\n");
    expectPrinted( "x = '3' + 4 + 5", "x = \"345\";\n");
    expectPrinted( "x = 3 + 4 + '5'", "x = 3 + 4 + \"5\";\n");
    expectPrinted( "x = null == 0", "x = false;\n");
    expectPrinted( "x = 0 == null", "x = false;\n");
    expectPrinted( "x = undefined == 0", "x = false;\n");
    expectPrinted( "x = 0 == undefined", "x = false;\n");
    expectPrinted( "x = null == NaN", "x = false;\n");
    expectPrinted( "x = NaN == null", "x = false;\n");
    expectPrinted( "x = undefined == NaN", "x = false;\n");
    expectPrinted( "x = NaN == undefined", "x = false;\n");
    expectPrinted( "x = null == ''", "x = false;\n");
    expectPrinted( "x = '' == null", "x = false;\n");
    expectPrinted( "x = undefined == ''", "x = false;\n");
    expectPrinted( "x = '' == undefined", "x = false;\n");
    expectPrinted( "x = null == 'null'", "x = false;\n");
    expectPrinted( "x = 'null' == null", "x = false;\n");
    expectPrinted( "x = undefined == 'undefined'", "x = false;\n");
    expectPrinted( "x = 'undefined' == undefined", "x = false;\n");
    expectPrinted( "x = false === 0", "x = false;\n");
    expectPrinted( "x = true === 1", "x = false;\n");
    expectPrinted( "x = false == 0", "x = true;\n");
    expectPrinted( "x = false == -0", "x = true;\n");
    expectPrinted( "x = true == 1", "x = true;\n");
    expectPrinted( "x = true == 2", "x = false;\n");
    expectPrinted( "x = 0 === false", "x = false;\n");
    expectPrinted( "x = 1 === true", "x = false;\n");
    expectPrinted( "x = 0 == false", "x = true;\n");
    expectPrinted( "x = -0 == false", "x = true;\n");
    expectPrinted( "x = 1 == true", "x = true;\n");
    expectPrinted( "x = 2 == true", "x = false;\n");
}

TEST(JsParser, TestConstantFoldingScopes) {
    // Parsing will crash if somehow the scope traversal is misaligned between
    // the parsing and binding passes. This checks for those cases.
    expectPrintedMangle( "x; 1 ? 0 : ()=>{}; (()=>{})()", "x;\n");
    expectPrintedMangle( "x; 0 ? ()=>{} : 1; (()=>{})()", "x;\n");
    expectPrinted( "x; 0 && (()=>{}); (()=>{})()", "x;\n/* @__PURE__ */ (() => {\n})();\n");
    expectPrinted( "x; 1 || (()=>{}); (()=>{})()", "x;\n/* @__PURE__ */ (() => {\n})();\n");
    expectPrintedMangle( "if (1) 0; else ()=>{}; (()=>{})()", "");
    expectPrintedMangle( "if (0) ()=>{}; else 1; (()=>{})()", "");
}

TEST(JsParser, TestImport) {
    expectPrinted( "import \"foo\"", "import \"foo\";\n");
    expectPrinted( "import {} from \"foo\"", "import {} from \"foo\";\n");
    expectPrinted( "import {x} from \"foo\";x", "import { x } from \"foo\";\nx;\n");
    expectPrinted( "import {x as y} from \"foo\";y", "import { x as y } from \"foo\";\ny;\n");
    expectPrinted( "import {x as y, z} from \"foo\";y;z", "import { x as y, z } from \"foo\";\ny;\nz;\n");
    expectPrinted( "import {x as y, z,} from \"foo\";y;z", "import { x as y, z } from \"foo\";\ny;\nz;\n");
    expectPrinted( "import z, {x as y} from \"foo\";y;z", "import z, { x as y } from \"foo\";\ny;\nz;\n");
    expectPrinted( "import z from \"foo\";z", "import z from \"foo\";\nz;\n");
    expectPrinted( "import * as ns from \"foo\";ns;ns.x", "import * as ns from \"foo\";\nns;\nns.x;\n");
    expectPrinted( "import z, * as ns from \"foo\";z;ns;ns.x", "import z, * as ns from \"foo\";\nz;\nns;\nns.x;\n");
    expectParseError( "import * from \"foo\"", "<stdin>: ERROR: Expected \"as\" but found \"from\"\n");
    expectPrinted( "import('foo')", "import(\"foo\");\n");
    expectPrinted( "(import('foo'))", "import(\"foo\");\n");
    expectPrinted( "{import('foo')}", "{\n  import(\"foo\");\n}\n");
    expectPrinted( "import('foo').then(() => {})", "import(\"foo\").then(() => {\n});\n");
    expectPrinted( "new import.meta", "new import.meta();\n");
    expectPrinted( "new (import('foo'))", "new (import(\"foo\"))();\n");
    expectParseError( "import()", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseError( "import(...a)", "<stdin>: ERROR: Unexpected \"...\"\n");
    expectParseError( "new import('foo')", "<stdin>: ERROR: Cannot use an \"import\" expression here without parentheses:\n");
    expectPrinted( "import.meta", "import.meta;\n");
    expectPrinted( "(import.meta)", "import.meta;\n");
    expectPrinted( "{import.meta}", "{\n  import.meta;\n}\n");
    expectPrinted( "import x from \"foo\"; x = 1", "import x from \"foo\";\nx = 1;\n");
    expectPrinted( "import x from \"foo\"; x++", "import x from \"foo\";\nx++;\n");
    expectPrinted( "import x from \"foo\"; ([x] = 1)", "import x from \"foo\";\n[x] = 1;\n");
    expectPrinted( "import x from \"foo\"; ({x} = 1)", "import x from \"foo\";\n({ x } = 1);\n");
    expectPrinted( "import x from \"foo\"; ({y: x} = 1)", "import x from \"foo\";\n({ y: x } = 1);\n");
    expectPrinted( "import {x} from \"foo\"; x++", "import { x } from \"foo\";\nx++;\n");
    expectPrinted( "import * as x from \"foo\"; x++", "import * as x from \"foo\";\nx++;\n");
    expectPrinted( "import * as x from \"foo\"; x.y = 1", "import * as x from \"foo\";\nx.y = 1;\n");
    expectPrinted( "import * as x from \"foo\"; x[y] = 1", "import * as x from \"foo\";\nx[y] = 1;\n");
    expectPrinted( "import * as x from \"foo\"; x['y'] = 1", "import * as x from \"foo\";\nx[\"y\"] = 1;\n");
    expectPrinted( "import * as x from \"foo\"; x['y z'] = 1", "import * as x from \"foo\";\nx[\"y z\"] = 1;\n");
    expectPrinted( "import x from \"foo\"; ({y = x} = 1)", "import x from \"foo\";\n({ y = x } = 1);\n");
    expectPrinted( "import x from \"foo\"; ({[x]: y} = 1)", "import x from \"foo\";\n({ [x]: y } = 1);\n");
    expectPrinted( "import x from \"foo\"; x.y = 1", "import x from \"foo\";\nx.y = 1;\n");
    expectPrinted( "import x from \"foo\"; x[y] = 1", "import x from \"foo\";\nx[y] = 1;\n");
    expectPrinted( "import x from \"foo\"; x['y'] = 1", "import x from \"foo\";\nx[\"y\"] = 1;\n");
    // "eval" and "arguments" are forbidden import names
    expectParseError( "import {eval} from 'foo'", "<stdin>: ERROR: Cannot use \"eval\" as an identifier here:\n");
    expectParseError( "import {ev\\u0061l} from 'foo'", "<stdin>: ERROR: Cannot use \"eval\" as an identifier here:\n");
    expectParseError( "import {x as eval} from 'foo'", "<stdin>: ERROR: Cannot use \"eval\" as an identifier here:\n");
    expectParseError( "import {x as ev\\u0061l} from 'foo'", "<stdin>: ERROR: Cannot use \"eval\" as an identifier here:\n");
    expectPrinted( "import {eval as x} from 'foo'", "import { eval as x } from \"foo\";\n");
    expectPrinted( "import {ev\\u0061l as x} from 'foo'", "import { eval as x } from \"foo\";\n");
    expectParseError( "import {arguments} from 'foo'", "<stdin>: ERROR: Cannot use \"arguments\" as an identifier here:\n");
    expectParseError( "import {\\u0061rguments} from 'foo'", "<stdin>: ERROR: Cannot use \"arguments\" as an identifier here:\n");
    expectParseError( "import {x as arguments} from 'foo'", "<stdin>: ERROR: Cannot use \"arguments\" as an identifier here:\n");
    expectParseError( "import {x as \\u0061rguments} from 'foo'", "<stdin>: ERROR: Cannot use \"arguments\" as an identifier here:\n");
    expectPrinted( "import {arguments as x} from 'foo'", "import { arguments as x } from \"foo\";\n");
    expectPrinted( "import {\\u0061rguments as x} from 'foo'", "import { arguments as x } from \"foo\";\n");
    // String import alias with "import {} from"
    expectPrinted( "import {'' as x} from 'foo'", "import { \"\" as x } from \"foo\";\n");
    expectPrinted( "import {'🍕' as x} from 'foo'", "import { \"🍕\" as x } from \"foo\";\n");
    expectPrinted( "import {'a b' as x} from 'foo'", "import { \"a b\" as x } from \"foo\";\n");
    expectPrinted( "import {'\\uD800\\uDC00' as x} from 'foo'", "import { 𐀀 as x } from \"foo\";\n");
    expectParseError( "import {'x'} from 'foo'", "<stdin>: ERROR: Expected \"as\" but found \"}\"\n");
    expectParseError( "import {'\\uD800' as x} from 'foo'",
        "<stdin>: ERROR: This import alias is invalid because it contains the unpaired Unicode surrogate U+D800\n");
    expectParseError( "import {'\\uDC00' as x} from 'foo'",
        "<stdin>: ERROR: This import alias is invalid because it contains the unpaired Unicode surrogate U+DC00\n");
    // String import alias with "import * as"
    expectParseError( "import * as '' from 'foo'", "<stdin>: ERROR: Expected identifier but found \"''\"\n");
    // See: https://github.com/tc39/proposal-defer-import-eval
    expectPrinted( "import defer from 'bar'", "import defer from \"bar\";\n");
    expectPrinted( "import defer, { foo } from 'bar'", "import defer, { foo } from \"bar\";\n");
    expectPrinted( "import defer * as foo from 'bar'", "import defer * as foo from \"bar\";\n");
    expectPrinted( "import.defer('foo')", "import.defer(\"foo\");\n");
    expectParseError( "import defer 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"'bar'\"\n");
    expectParseError( "import defer foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"foo\"\n");
    expectParseError( "import defer { foo } from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"{\"\n");
    expectParseErrorTarget( 6, "import defer * as foo from 'bar'", "<stdin>: ERROR: Deferred imports are not available in the configured target environment\n");
    expectParseErrorTarget( 6, "import.defer('foo')", "<stdin>: ERROR: Deferred imports are not available in the configured target environment\n");
    // See: https://github.com/tc39/proposal-source-phase-imports
    expectPrinted( "import source from 'bar'", "import source from \"bar\";\n");
    expectPrinted( "import source, { foo } from 'bar'", "import source, { foo } from \"bar\";\n");
    expectPrinted( "import source foo from 'bar'", "import source foo from \"bar\";\n");
    expectPrinted( "import source from from 'bar'", "import source from from \"bar\";\n");
    expectPrinted( "import source source from 'bar'", "import source source from \"bar\";\n");
    expectPrinted( "import.source('foo')", "import.source(\"foo\");\n");
    expectParseError( "import source 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"'bar'\"\n");
    expectParseError( "import source * as foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"*\"\n");
    expectParseError( "import source { foo } from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"{\"\n");
    expectParseErrorTarget( 6, "import source foo from 'bar'", "<stdin>: ERROR: Source phase imports are not available in the configured target environment\n");
    expectParseErrorTarget( 6, "import.source('foo')", "<stdin>: ERROR: Source phase imports are not available in the configured target environment\n");
}

TEST(JsParser, TestExport) {
    expectPrinted( "export default x", "export default x;\n");
    expectPrinted( "export class x {}", "export class x {\n}\n");
    expectPrinted( "export function x() {}", "export function x() {\n}\n");
    expectPrinted( "export async function x() {}", "export async function x() {\n}\n");
    expectPrinted( "export var x, y", "export var x, y;\n");
    expectPrinted( "export let x, y", "export let x, y;\n");
    expectPrinted( "export const x = 0, y = 1", "export const x = 0, y = 1;\n");
    expectPrinted( "export * from \"foo\"", "export * from \"foo\";\n");
    expectPrinted( "export * as ns from \"foo\"", "export * as ns from \"foo\";\n");
    expectPrinted( "export * as if from \"foo\"", "export * as if from \"foo\";\n");
    expectPrinted( "let x; export {x}", "let x;\nexport { x };\n");
    expectPrinted( "let x; export {x as y}", "let x;\nexport { x as y };\n");
    expectPrinted( "let x, z; export {x as y, z}", "let x, z;\nexport { x as y, z };\n");
    expectPrinted( "let x, z; export {x as y, z,}", "let x, z;\nexport { x as y, z };\n");
    expectPrinted( "let x; export {x} from \"foo\"", "let x;\nexport { x } from \"foo\";\n");
    expectPrinted( "let x; export {x as y} from \"foo\"", "let x;\nexport { x as y } from \"foo\";\n");
    expectPrinted( "let x, z; export {x as y, z} from \"foo\"", "let x, z;\nexport { x as y, z } from \"foo\";\n");
    expectPrinted( "let x, z; export {x as y, z,} from \"foo\"", "let x, z;\nexport { x as y, z } from \"foo\";\n");
    expectParseError( "export x from \"foo\"", "<stdin>: ERROR: Unexpected \"x\"\n");
    expectParseError( "export async", "<stdin>: ERROR: Expected \"function\" but found end of file\n");
    expectParseError( "export async function", "<stdin>: ERROR: Expected identifier but found end of file\n");
    expectParseError( "export async () => {}", "<stdin>: ERROR: Expected \"function\" but found \"(\"\n");
    expectParseError( "export var", "<stdin>: ERROR: Expected identifier but found end of file\n");
    expectParseError( "export let", "<stdin>: ERROR: Expected identifier but found end of file\n");
    expectParseError( "export const", "<stdin>: ERROR: Expected identifier but found end of file\n");
    // Do not parse TypeScript export syntax in JavaScript
    expectParseError( "export enum Foo {}", "<stdin>: ERROR: Unexpected \"enum\"\n");
    expectParseError( "export interface Foo {}", "<stdin>: ERROR: Unexpected \"interface\"\n");
    expectParseError( "export namespace Foo {}", "<stdin>: ERROR: Unexpected \"namespace\"\n");
    expectParseError( "export abstract class Foo {}", "<stdin>: ERROR: Unexpected \"abstract\"\n");
    expectParseError( "export declare class Foo {}", "<stdin>: ERROR: Unexpected \"declare\"\n");
    expectParseError( "export declare function foo() {}", "<stdin>: ERROR: Unexpected \"declare\"\n");
    // String export alias with "export {}"
    expectPrinted( "let x; export {x as ''}", "let x;\nexport { x as \"\" };\n");
    expectPrinted( "let x; export {x as '🍕'}", "let x;\nexport { x as \"🍕\" };\n");
    expectPrinted( "let x; export {x as 'a b'}", "let x;\nexport { x as \"a b\" };\n");
    expectPrinted( "let x; export {x as '\\uD800\\uDC00'}", "let x;\nexport { x as 𐀀 };\n");
    expectParseError( "let x; export {'x'}", "<stdin>: ERROR: Expected identifier but found \"'x'\"\n");
    expectParseError( "let x; export {'x' as 'y'}", "<stdin>: ERROR: Expected identifier but found \"'x'\"\n");
    expectParseError( "let x; export {x as '\\uD800'}",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+D800\n");
    expectParseError( "let x; export {x as '\\uDC00'}",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+DC00\n");
    // String import alias with "export {} from"
    expectPrinted( "export {'' as x} from 'foo'", "export { \"\" as x } from \"foo\";\n");
    expectPrinted( "export {'🍕' as x} from 'foo'", "export { \"🍕\" as x } from \"foo\";\n");
    expectPrinted( "export {'a b' as x} from 'foo'", "export { \"a b\" as x } from \"foo\";\n");
    expectPrinted( "export {'\\uD800\\uDC00' as x} from 'foo'", "export { 𐀀 as x } from \"foo\";\n");
    expectParseError( "export {'\\uD800' as x} from 'foo'",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+D800\n");
    expectParseError( "export {'\\uDC00' as x} from 'foo'",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+DC00\n");
    // String export alias with "export {} from"
    expectPrinted( "export {x as ''} from 'foo'", "export { x as \"\" } from \"foo\";\n");
    expectPrinted( "export {x as '🍕'} from 'foo'", "export { x as \"🍕\" } from \"foo\";\n");
    expectPrinted( "export {x as 'a b'} from 'foo'", "export { x as \"a b\" } from \"foo\";\n");
    expectPrinted( "export {x as '\\uD800\\uDC00'} from 'foo'", "export { x as 𐀀 } from \"foo\";\n");
    expectParseError( "export {x as '\\uD800'} from 'foo'",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+D800\n");
    expectParseError( "export {x as '\\uDC00'} from 'foo'",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+DC00\n");
    // String import and export alias with "export {} from"
    expectPrinted( "export {'x'} from 'foo'", "export { x } from \"foo\";\n");
    expectPrinted( "export {'a b'} from 'foo'", "export { \"a b\" } from \"foo\";\n");
    expectPrinted( "export {'x' as 'y'} from 'foo'", "export { x as y } from \"foo\";\n");
    expectPrinted( "export {'a b' as 'c d'} from 'foo'", "export { \"a b\" as \"c d\" } from \"foo\";\n");
    // String export alias with "export * as"
    expectPrinted( "export * as '' from 'foo'", "export * as \"\" from \"foo\";\n");
    expectPrinted( "export * as '🍕' from 'foo'", "export * as \"🍕\" from \"foo\";\n");
    expectPrinted( "export * as 'a b' from 'foo'", "export * as \"a b\" from \"foo\";\n");
    expectPrinted( "export * as '\\uD800\\uDC00' from 'foo'", "export * as 𐀀 from \"foo\";\n");
    expectParseError( "export * as '\\uD800' from 'foo'",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+D800\n");
    expectParseError( "export * as '\\uDC00' from 'foo'",
        "<stdin>: ERROR: This export alias is invalid because it contains the unpaired Unicode surrogate U+DC00\n");
}

TEST(JsParser, TestExportDuplicates) {
    expectPrinted( "export {x};let x", "export { x };\nlet x;\n");
    expectPrinted( "export {x, x as y};let x", "export { x, x as y };\nlet x;\n");
    expectPrinted( "export {x};export {x as y} from 'foo';let x", "export { x };\nexport { x as y } from \"foo\";\nlet x;\n");
    expectPrinted( "export {x};export default function x() {}", "export { x };\nexport default function x() {\n}\n");
    expectPrinted( "export {x};export default class x {}", "export { x };\nexport default class x {\n}\n");
    const std::string errorTextX = "<stdin>: ERROR: Multiple exports with the same name \"x\"\n"
        "<stdin>: NOTE: The name \"x\" was originally exported here:\n"
        "";
    expectParseError( "export {x, x};let x", errorTextX);
    expectParseError( "export {x, y as x};let x, y", errorTextX);
    expectParseError( "export {x};export function x() {}", errorTextX);
    expectParseError( "export {x};export class x {}", errorTextX);
    expectParseError( "export {x};export const x = 0", errorTextX);
    expectParseError( "export {x};export let x", errorTextX);
    expectParseError( "export {x};export var x", errorTextX);
    expectParseError( "export {x};let x;export {x} from 'foo'", errorTextX);
    expectParseError( "export {x};let x;export {y as x} from 'foo'", errorTextX);
    expectParseError( "export {x};let x;export * as x from 'foo'", errorTextX);
    const std::string errorTextDefault = "<stdin>: ERROR: Multiple exports with the same name \"default\"\n"
        "<stdin>: NOTE: The name \"default\" was originally exported here:\n"
        "";
    expectParseError( "export {x as default};let x;export default 0", errorTextDefault);
    expectParseError( "export {x as default};let x;export default function() {}", errorTextDefault);
    expectParseError( "export {x as default};let x;export default class {}", errorTextDefault);
    expectParseError( "export {x as default};export default function x() {}", errorTextDefault);
    expectParseError( "export {x as default};export default class x {}", errorTextDefault);
}

TEST(JsParser, TestExportDefault) {
    expectParseError( "export default 1, 2", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectPrinted( "export default (1, 2)", "export default (1, 2);\n");
    expectParseError( "export default async, 0", "<stdin>: ERROR: Expected \";\" but found \",\"\n");
    expectPrinted( "export default async", "export default async;\n");
    expectPrinted( "export default async()", "export default async();\n");
    expectPrinted( "export default async + 1", "export default async + 1;\n");
    expectPrinted( "export default async => {}", "export default (async) => {\n};\n");
    expectPrinted( "export default async x => {}", "export default async (x) => {\n};\n");
    expectPrinted( "export default async () => {}", "export default async () => {\n};\n");
    // This is a corner case in the ES6 grammar. The "export default" statement
    // normally takes an expression except for the function and class keywords
    // which behave sort of like their respective declarations instead.
    expectPrinted( "export default function() {} - after", "export default function() {\n}\n-after;\n");
    expectPrinted( "export default function*() {} - after", "export default function* () {\n}\n-after;\n");
    expectPrinted( "export default function foo() {} - after", "export default function foo() {\n}\n-after;\n");
    expectPrinted( "export default function* foo() {} - after", "export default function* foo() {\n}\n-after;\n");
    expectPrinted( "export default async function() {} - after", "export default async function() {\n}\n-after;\n");
    expectPrinted( "export default async function*() {} - after", "export default async function* () {\n}\n-after;\n");
    expectPrinted( "export default async function foo() {} - after", "export default async function foo() {\n}\n-after;\n");
    expectPrinted( "export default async function* foo() {} - after", "export default async function* foo() {\n}\n-after;\n");
    expectPrinted( "export default class {} - after", "export default class {\n}\n-after;\n");
    expectPrinted( "export default class Foo {} - after", "export default class Foo {\n}\n-after;\n");
    // Check ASI for "abstract"
    expectPrinted( "export default abstract\nclass Foo {}", "export default abstract;\nclass Foo {\n}\n");
    expectParseError( "export default abstract class {}", "<stdin>: ERROR: Expected \";\" but found \"class\"\n");
}

TEST(JsParser, TestExportClause) {
    expectPrinted( "export {x, y};let x, y", "export { x, y };\nlet x, y;\n");
    expectPrinted( "export {x, y as z,};let x, y", "export { x, y as z };\nlet x, y;\n");
    expectPrinted( "export {x, y} from 'path'", "export { x, y } from \"path\";\n");
    expectPrinted( "export {default, if} from 'path'", "export { default, if } from \"path\";\n");
    expectPrinted( "export {default as foo, if as bar} from 'path'", "export { default as foo, if as bar } from \"path\";\n");
    expectParseError( "export {default}", "<stdin>: ERROR: Expected identifier but found \"default\"\n");
    expectParseError( "export {default as foo}", "<stdin>: ERROR: Expected identifier but found \"default\"\n");
    expectParseError( "export {if}", "<stdin>: ERROR: Expected identifier but found \"if\"\n");
    expectParseError( "export {if as foo}", "<stdin>: ERROR: Expected identifier but found \"if\"\n");
}

TEST(JsParser, TestCatch) {
    expectPrinted( "try {} catch (e) {}", "try {\n} catch (e) {\n}\n");
    expectPrinted( "try {} catch (e) { var e }", "try {\n} catch (e) {\n  var e;\n}\n");
    expectPrinted( "var e; try {} catch (e) {}", "var e;\ntry {\n} catch (e) {\n}\n");
    expectPrinted( "let e; try {} catch (e) {}", "let e;\ntry {\n} catch (e) {\n}\n");
    expectPrinted( "try { var e } catch (e) {}", "try {\n  var e;\n} catch (e) {\n}\n");
    expectPrinted( "try { function e() {} } catch (e) {}", "try {\n  let e = function() {\n  };\n  var e = e;\n} catch (e) {\n}\n");
    expectPrinted( "try {} catch (e) { { function e() {} } }", "try {\n} catch (e) {\n  {\n    let e = function() {\n    };\n    var e = e;\n  }\n}\n");
    expectPrinted( "try {} catch (e) { if (1) function e() {} }", "try {\n} catch (e) {\n  if (1) {\n    let e = function() {\n    };\n    var e = e;\n  }\n}\n");
    expectPrinted( "try {} catch (e) { if (0) ; else function e() {} }", "try {\n} catch (e) {\n  if (0) ;\n  else {\n    let e = function() {\n    };\n    var e = e;\n  }\n}\n");
    expectPrinted( "try {} catch ({ e }) { { function e() {} } }", "try {\n} catch ({ e }) {\n  {\n    let e = function() {\n    };\n    var e = e;\n  }\n}\n");
    const std::string errorText = "<stdin>: ERROR: The symbol \"e\" has already been declared\n"
        "<stdin>: NOTE: The symbol \"e\" was originally declared here:\n"
        "";
    expectParseError( "try {} catch (e) { function e() {} }", errorText);
    expectParseError( "try {} catch ({ e }) { var e }", errorText);
    expectParseError( "try {} catch ({ e }) { { var e } }", errorText);
    expectParseError( "try {} catch ({ e }) { function e() {} }", errorText);
    expectParseError( "try {} catch (e) { let e }", errorText);
    expectParseError( "try {} catch (e) { const e = 0 }", errorText);
}

TEST(JsParser, TestWarningEqualsNegativeZero) {
    const std::string note = "NOTE: Floating-point equality is defined such that 0 and -0 are equal, so \"x === -0\" returns true for both 0 and -0. " "You need to use \"Object.is(x, -0)\" instead to test for -0.\n";
    expectParseError( "x === -0", "<stdin>: WARNING: Comparison with -0 using the \"===\" operator will also match 0\n"+note);
    expectParseError( "x == -0", "<stdin>: WARNING: Comparison with -0 using the \"==\" operator will also match 0\n"+note);
    expectParseError( "x !== -0", "<stdin>: WARNING: Comparison with -0 using the \"!==\" operator will also match 0\n"+note);
    expectParseError( "x != -0", "<stdin>: WARNING: Comparison with -0 using the \"!=\" operator will also match 0\n"+note);
    expectParseError( "switch (x) { case -0: }", "<stdin>: WARNING: Comparison with -0 using a case clause will also match 0\n"+note);
    expectParseError( "-0 === x", "<stdin>: WARNING: Comparison with -0 using the \"===\" operator will also match 0\n"+note);
    expectParseError( "-0 == x", "<stdin>: WARNING: Comparison with -0 using the \"==\" operator will also match 0\n"+note);
    expectParseError( "-0 !== x", "<stdin>: WARNING: Comparison with -0 using the \"!==\" operator will also match 0\n"+note);
    expectParseError( "-0 != x", "<stdin>: WARNING: Comparison with -0 using the \"!=\" operator will also match 0\n"+note);
    expectParseError( "switch (-0) { case x: }", "");// Don't bother to handle this case
}

TEST(JsParser, TestWarningEqualsNewObject) {
    const std::string note = "NOTE: Equality with a new object is always false in JavaScript because the equality operator tests object identity. " "You need to write code to compare the contents of the object instead. " "For example, use \"Array.isArray(x) && x.length === 0\" instead of \"x === []\" to test for an empty array.\n";
    expectParseError( "x === []", "<stdin>: WARNING: Comparison using the \"===\" operator here is always false\n"+note);
    expectParseError( "x !== []", "<stdin>: WARNING: Comparison using the \"!==\" operator here is always true\n"+note);
    expectParseError( "x == []", "");
    expectParseError( "x != []", "");
    expectParseError( "switch (x) { case []: }", "<stdin>: WARNING: This case clause will never be evaluated because the comparison is always false\n"+note);
    expectParseError( "[] === x", "<stdin>: WARNING: Comparison using the \"===\" operator here is always false\n"+note);
    expectParseError( "[] !== x", "<stdin>: WARNING: Comparison using the \"!==\" operator here is always true\n"+note);
    expectParseError( "[] == x", "");
    expectParseError( "[] != x", "");
    expectParseError( "switch ([]) { case x: }", "");// Don't bother to handle this case
}

TEST(JsParser, TestWarningEqualsNaN) {
    const std::string note = "NOTE: Floating-point equality is defined such that NaN is never equal to anything, so \"x === NaN\" always returns false. " "You need to use \"Number.isNaN(x)\" instead to test for NaN.\n";
    expectParseError( "x === NaN", "<stdin>: WARNING: Comparison with NaN using the \"===\" operator here is always false\n"+note);
    expectParseError( "x !== NaN", "<stdin>: WARNING: Comparison with NaN using the \"!==\" operator here is always true\n"+note);
    expectParseError( "x == NaN", "<stdin>: WARNING: Comparison with NaN using the \"==\" operator here is always false\n"+note);
    expectParseError( "x != NaN", "<stdin>: WARNING: Comparison with NaN using the \"!=\" operator here is always true\n"+note);
    expectParseError( "switch (x) { case NaN: }", "<stdin>: WARNING: This case clause will never be evaluated because equality with NaN is always false\n"+note);
    expectParseError( "NaN === x", "<stdin>: WARNING: Comparison with NaN using the \"===\" operator here is always false\n"+note);
    expectParseError( "NaN !== x", "<stdin>: WARNING: Comparison with NaN using the \"!==\" operator here is always true\n"+note);
    expectParseError( "NaN == x", "<stdin>: WARNING: Comparison with NaN using the \"==\" operator here is always false\n"+note);
    expectParseError( "NaN != x", "<stdin>: WARNING: Comparison with NaN using the \"!=\" operator here is always true\n"+note);
    expectParseError( "switch (NaN) { case x: }", "");// Don't bother to handle this case
}

TEST(JsParser, TestWarningTypeofEquals) {
    const std::string note = "NOTE: The expression \"typeof x\" actually evaluates to \"object\" in JavaScript, not \"null\". " "You need to use \"x === null\" to test for null.\n";
    expectParseError( "typeof x === 'null'", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "typeof x !== 'null'", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "typeof x == 'null'", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "typeof x != 'null'", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "switch (typeof x) { case 'null': }", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "'null' === typeof x", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "'null' !== typeof x", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "'null' == typeof x", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "'null' != typeof x", "<stdin>: WARNING: The \"typeof\" operator will never evaluate to \"null\"\n"+note);
    expectParseError( "switch ('null') { case typeof x: }", "");// Don't bother to handle this case
}

TEST(JsParser, TestWarningDeleteSuperProperty) {
    const std::string text = "<stdin>: WARNING: Attempting to delete a property of \"super\" will throw a ReferenceError\n";
    expectParseError( "class Foo extends Bar { constructor() { delete super.foo } }", text);
    expectParseError( "class Foo extends Bar { constructor() { delete super['foo'] } }", text);
    expectParseError( "class Foo extends Bar { constructor() { delete (super.foo) } }", text);
    expectParseError( "class Foo extends Bar { constructor() { delete (super['foo']) } }", text);
    expectParseError( "class Foo extends Bar { constructor() { delete super.foo.bar } }", "");
    expectParseError( "class Foo extends Bar { constructor() { delete super['foo']['bar'] } }", "");
}

TEST(JsParser, TestWarningDuplicateCase) {
    expectParseError( "switch (x) { case null: case undefined: }", "");
    expectParseError( "switch (x) { case false: case true: }", "");
    expectParseError( "switch (x) { case 0: case 1: }", "");
    expectParseError( "switch (x) { case 1: case 1n: }", "");
    expectParseError( "switch (x) { case 'a': case 'b': }", "");
    expectParseError( "switch (x) { case y: case z: }", "");
    expectParseError( "switch (x) { case y.a: case y.b: }", "");
    expectParseError( "switch (x) { case y.a: case z.a: }", "");
    expectParseError( "switch (x) { case y.a: case y?.a: }", "");
    expectParseError( "switch (x) { case y[a]: case y[b]: }", "");
    expectParseError( "switch (x) { case y[a]: case z[a]: }", "");
    expectParseError( "switch (x) { case y[a]: case y?.[a]: }", "");
    const std::string alwaysWarning = "<stdin>: WARNING: This case clause will never be evaluated because it duplicates an earlier case clause\n" "<stdin>: NOTE: The earlier case clause is here:\n";
    const std::string likelyWarning = "<stdin>: WARNING: This case clause may never be evaluated because it likely duplicates an earlier case clause\n" "<stdin>: NOTE: The earlier case clause is here:\n";
    expectParseError( "switch (x) { case null: case null: }", alwaysWarning);
    expectParseError( "switch (x) { case undefined: case undefined: }", alwaysWarning);
    expectParseError( "switch (x) { case true: case true: }", alwaysWarning);
    expectParseError( "switch (x) { case false: case false: }", alwaysWarning);
    expectParseError( "switch (x) { case 0xF: case 15: }", alwaysWarning);
    expectParseError( "switch (x) { case 'a': case `a`: }", alwaysWarning);
    expectParseError( "switch (x) { case 123n: case 1_2_3n: }", alwaysWarning);
    expectParseError( "switch (x) { case y: case y: }", alwaysWarning);
    expectParseError( "switch (x) { case y.a: case y.a: }", likelyWarning);
    expectParseError( "switch (x) { case y?.a: case y?.a: }", likelyWarning);
    expectParseError( "switch (x) { case y[a]: case y[a]: }", likelyWarning);
    expectParseError( "switch (x) { case y?.[a]: case y?.[a]: }", likelyWarning);
}

TEST(JsParser, TestWarningDuplicateClassMember) {
    const std::string duplicateWarning = "<stdin>: WARNING: Duplicate member \"x\" in class body\n" "<stdin>: NOTE: The original member \"x\" is here:\n";
    expectParseError( "class Foo { x; x }", duplicateWarning);
    expectParseError( "class Foo { x() {}; x() {} }", duplicateWarning);
    expectParseError( "class Foo { get x() {}; get x() {} }", duplicateWarning);
    expectParseError( "class Foo { get x() {}; set x(y) {}; get x() {} }", duplicateWarning);
    expectParseError( "class Foo { get x() {}; set x(y) {}; set x(y) {} }", duplicateWarning);
    expectParseError( "class Foo { get x() {}; set x(y) {} }", "");
    expectParseError( "class Foo { set x(y) {}; get x() {} }", "");
    expectParseError( "class Foo { static x; static x }", duplicateWarning);
    expectParseError( "class Foo { static x() {}; static x() {} }", duplicateWarning);
    expectParseError( "class Foo { static get x() {}; static get x() {} }", duplicateWarning);
    expectParseError( "class Foo { static get x() {}; static set x(y) {}; static get x() {} }", duplicateWarning);
    expectParseError( "class Foo { static get x() {}; static set x(y) {}; static set x(y) {} }", duplicateWarning);
    expectParseError( "class Foo { static get x() {}; static set x(y) {} }", "");
    expectParseError( "class Foo { static set x(y) {}; static get x() {} }", "");
    expectParseError( "class Foo { x; static x }", "");
    expectParseError( "class Foo { x; static x() {} }", "");
    expectParseError( "class Foo { x() {}; static x }", "");
    expectParseError( "class Foo { x() {}; static x() {} }", "");
    expectParseError( "class Foo { static x; x }", "");
    expectParseError( "class Foo { static x; x() {} }", "");
    expectParseError( "class Foo { static x() {}; x }", "");
    expectParseError( "class Foo { static x() {}; x() {} }", "");
    expectParseError( "class Foo { get x() {}; static get x() {} }", "");
    expectParseError( "class Foo { set x(y) {}; static set x(y) {} }", "");
}

TEST(JsParser, TestWarningNullishCoalescing) {
    expectParseError( "x = null ?? y", "");
    expectParseError( "x = undefined ?? y", "");
    expectParseError( "x = false ?? y", "");
    expectParseError( "x = true ?? y", "");
    expectParseError( "x = 0 ?? y", "");
    expectParseError( "x = 1 ?? y", "");
    const std::string alwaysLeft = "<stdin>: WARNING: The \"??\" operator here will always return the left operand\n" "<stdin>: NOTE: The left operand of the \"??\" operator here will never be null or undefined, so it will always be returned. This usually indicates a bug in your code:\n";
    const std::string alwaysRight = "<stdin>: WARNING: The \"??\" operator here will always return the right operand\n" "<stdin>: NOTE: The left operand of the \"??\" operator here will always be null or undefined, so it will never be returned. This usually indicates a bug in your code:\n";
    expectParseError( "x = a === b ?? y", alwaysLeft);
    expectParseError( "x = { ...a } ?? y", alwaysLeft);
    expectParseError( "x = (a => b) ?? y", alwaysLeft);
    expectParseError( "x = void a ?? y", alwaysRight);
}

TEST(JsParser, TestWarningLogicalOperator) {
    expectParseError( "x(a => b && a <= c)", "");
    expectParseError( "x(a => b || a <= c)", "");
    expectParseError( "x(a => (0 && a <= 1))", "");
    expectParseError( "x(a => (-1 && a <= 0))", "");
    expectParseError( "x(a => (0 || a <= -1))", "");
    expectParseError( "x(a => (1 || a <= 0))", "");
    expectParseError( "x(a => 0 && a <= 1)", "<stdin>: WARNING: The \"&&\" operator here will always return the left operand\n" "<stdin>: NOTE: The \"=>\" symbol creates an arrow function expression in JavaScript. Did you mean to use the greater-than-or-equal-to operator \">=\" here instead?\n");
    expectParseError( "x(a => -1 && a <= 0)", "<stdin>: WARNING: The \"&&\" operator here will always return the right operand\n" "<stdin>: NOTE: The \"=>\" symbol creates an arrow function expression in JavaScript. Did you mean to use the greater-than-or-equal-to operator \">=\" here instead?\n");
    expectParseError( "x(a => 0 || a <= -1)", "<stdin>: WARNING: The \"||\" operator here will always return the right operand\n" "<stdin>: NOTE: The \"=>\" symbol creates an arrow function expression in JavaScript. Did you mean to use the greater-than-or-equal-to operator \">=\" here instead?\n");
    expectParseError( "x(a => 1 || a <= 0)", "<stdin>: WARNING: The \"||\" operator here will always return the left operand\n" "<stdin>: NOTE: The \"=>\" symbol creates an arrow function expression in JavaScript. Did you mean to use the greater-than-or-equal-to operator \">=\" here instead?\n");
}

TEST(JsParser, TestMangleFor) {
    expectPrintedMangle( "var a; while (1) ;", "for (var a; ; ) ;\n");
    expectPrintedMangle( "let a; while (1) ;", "let a;\nfor (; ; ) ;\n");
    expectPrintedMangle( "const a=0; while (1) ;", "const a = 0;\nfor (; ; ) ;\n");
    expectPrintedMangle( "var a; for (var b;;) ;", "for (var a, b; ; ) ;\n");
    expectPrintedMangle( "let a; for (let b;;) ;", "let a;\nfor (let b; ; ) ;\n");
    expectPrintedMangle( "const a=0; for (const b = 1;;) ;", "const a = 0;\nfor (const b = 1; ; ) ;\n");
    expectPrintedMangle( "export var a; while (1) ;", "export var a;\nfor (; ; ) ;\n");
    expectPrintedMangle( "export let a; while (1) ;", "export let a;\nfor (; ; ) ;\n");
    expectPrintedMangle( "export const a=0; while (1) ;", "export const a = 0;\nfor (; ; ) ;\n");
    expectPrintedMangle( "export var a; for (var b;;) ;", "export var a;\nfor (var b; ; ) ;\n");
    expectPrintedMangle( "export let a; for (let b;;) ;", "export let a;\nfor (let b; ; ) ;\n");
    expectPrintedMangle( "export const a=0; for (const b = 1;;) ;", "export const a = 0;\nfor (const b = 1; ; ) ;\n");
    expectPrintedMangle( "var a; for (let b;;) ;", "var a;\nfor (let b; ; ) ;\n");
    expectPrintedMangle( "let a; for (const b=0;;) ;", "let a;\nfor (const b = 0; ; ) ;\n");
    expectPrintedMangle( "const a=0; for (var b;;) ;", "const a = 0;\nfor (var b; ; ) ;\n");
    expectPrintedMangle( "a(); while (1) ;", "for (a(); ; ) ;\n");
    expectPrintedMangle( "a(); for (b();;) ;", "for (a(), b(); ; ) ;\n");
    expectPrintedMangle( "for (; ;) if (x) break;", "for (; !x; ) ;\n");
    expectPrintedMangle( "for (; ;) if (!x) break;", "for (; x; ) ;\n");
    expectPrintedMangle( "for (; a;) if (x) break;", "for (; a && !x; ) ;\n");
    expectPrintedMangle( "for (; a;) if (!x) break;", "for (; a && x; ) ;\n");
    expectPrintedMangle( "for (; ;) { if (x) break; y(); }", "for (; !x; )\n  y();\n");
    expectPrintedMangle( "for (; a;) { if (x) break; y(); }", "for (; a && !x; )\n  y();\n");
    expectPrintedMangle( "for (; ;) if (x) break; else y();", "for (; !x; ) y();\n");
    expectPrintedMangle( "for (; a;) if (x) break; else y();", "for (; a && !x; ) y();\n");
    expectPrintedMangle( "for (; ;) { if (x) break; else y(); z(); }", "for (; !x; )\n  y(), z();\n");
    expectPrintedMangle( "for (; a;) { if (x) break; else y(); z(); }", "for (; a && !x; )\n  y(), z();\n");
    expectPrintedMangle( "for (; ;) if (x) y(); else break;", "for (; x; ) y();\n");
    expectPrintedMangle( "for (; ;) if (!x) y(); else break;", "for (; !x; ) y();\n");
    expectPrintedMangle( "for (; a;) if (x) y(); else break;", "for (; a && x; ) y();\n");
    expectPrintedMangle( "for (; a;) if (!x) y(); else break;", "for (; a && !x; ) y();\n");
    expectPrintedMangle( "for (; ;) { if (x) y(); else break; z(); }", "for (; x; ) {\n  y();\n  z();\n}\n");
    expectPrintedMangle( "for (; a;) { if (x) y(); else break; z(); }", "for (; a && x; ) {\n  y();\n  z();\n}\n");
}

TEST(JsParser, TestMangleLoopJump) {
    // Trim after jump
    expectPrintedMangle( "while (x) { if (1) break; z(); }", "for (; x; )\n  break;\n");
    expectPrintedMangle( "while (x) { if (1) continue; z(); }", "for (; x; )\n  ;\n");
    expectPrintedMangle( "foo: while (a) while (x) { if (1) continue foo; z(); }", "foo: for (; a; ) for (; x; )\n  continue foo;\n");
    expectPrintedMangle( "while (x) { y(); if (1) break; z(); }", "for (; x; ) {\n  y();\n  break;\n}\n");
    expectPrintedMangle( "while (x) { y(); if (1) continue; z(); }", "for (; x; )\n  y();\n");
    expectPrintedMangle( "while (x) { y(); debugger; if (1) continue; z(); }", "for (; x; ) {\n  y();\n  debugger;\n}\n");
    expectPrintedMangle( "while (x) { let y = z(); if (1) continue; z(); }", "for (; x; ) {\n  let y = z();\n}\n");
    expectPrintedMangle( "while (x) { debugger; if (y) { if (1) break; z() } }", "for (; x; ) {\n  debugger;\n  if (y)\n    break;\n}\n");
    expectPrintedMangle( "while (x) { debugger; if (y) { if (1) continue; z() } }", "for (; x; ) {\n  debugger;\n  y;\n}\n");
    expectPrintedMangle( "while (x) { debugger; if (1) { if (1) break; z() } }", "for (; x; ) {\n  debugger;\n  break;\n}\n");
    expectPrintedMangle( "while (x) { debugger; if (1) { if (1) continue; z() } }", "for (; x; )\n  debugger;\n");
    // Trim trailing continue
    expectPrintedMangle( "while (x()) continue", "for (; x(); ) ;\n");
    expectPrintedMangle( "while (x) { y(); continue }", "for (; x; )\n  y();\n");
    expectPrintedMangle( "while (x) { if (y) { z(); continue } }",
        "for (; x; )\n  if (y) {\n    z();\n    continue;\n  }\n");
    expectPrintedMangle( "label: while (x) while (y) { z(); continue label }",
        "label: for (; x; ) for (; y; ) {\n  z();\n  continue label;\n}\n");
    // Optimize implicit continue
    expectPrintedMangle( "while (x) { if (y) continue; z(); }", "for (; x; )\n  y || z();\n");
    expectPrintedMangle( "while (x) { if (y) continue; else z(); w(); }", "for (; x; )\n  y || (z(), w());\n");
    expectPrintedMangle( "while (x) { t(); if (y) continue; z(); }", "for (; x; )\n  t(), !y && z();\n");
    expectPrintedMangle( "while (x) { t(); if (y) continue; else z(); w(); }", "for (; x; )\n  t(), !y && (z(), w());\n");
    expectPrintedMangle( "while (x) { debugger; if (y) continue; z(); }", "for (; x; ) {\n  debugger;\n  y || z();\n}\n");
    expectPrintedMangle( "while (x) { debugger; if (y) continue; else z(); w(); }", "for (; x; ) {\n  debugger;\n  y || (z(), w());\n}\n");
    // Do not optimize implicit continue for statements that care about scope
    expectPrintedMangle( "while (x) { if (y) continue; function y() {} }", "for (; x; ) {\n  let y = function() {\n  };\n  var y = y;\n}\n");
    expectPrintedMangle( "while (x) { if (y) continue; let y }", "for (; x; ) {\n  if (y) continue;\n  let y;\n}\n");
    expectPrintedMangle( "while (x) { if (y) continue; var y }", "for (; x; )\n  if (!y)\n    var y;\n");
}

TEST(JsParser, TestMangleUndefined) {
    // These should be transformed
    expectPrintedNormalAndMangle( "console.log(undefined)", "console.log(void 0);\n", "console.log(void 0);\n");
    expectPrintedNormalAndMangle( "console.log(+undefined)", "console.log(NaN);\n", "console.log(NaN);\n");
    expectPrintedNormalAndMangle( "console.log(undefined + undefined)", "console.log(void 0 + void 0);\n", "console.log(void 0 + void 0);\n");
    expectPrintedNormalAndMangle( "const x = undefined", "const x = void 0;\n", "const x = void 0;\n");
    expectPrintedNormalAndMangle( "let x = undefined", "let x = void 0;\n", "let x;\n");
    expectPrintedNormalAndMangle( "var x = undefined", "var x = void 0;\n", "var x = void 0;\n");
    expectPrintedNormalAndMangle( "function foo(a) { if (!a) return undefined; a() }", "function foo(a) {\n  if (!a) return void 0;\n  a();\n}\n", "function foo(a) {\n  a && a();\n}\n");
    // These should not be transformed
    expectPrintedNormalAndMangle( "delete undefined", "delete undefined;\n", "delete undefined;\n");
    expectPrintedNormalAndMangle( "undefined--", "undefined--;\n", "undefined--;\n");
    expectPrintedNormalAndMangle( "undefined++", "undefined++;\n", "undefined++;\n");
    expectPrintedNormalAndMangle( "--undefined", "--undefined;\n", "--undefined;\n");
    expectPrintedNormalAndMangle( "++undefined", "++undefined;\n", "++undefined;\n");
    expectPrintedNormalAndMangle( "undefined = 1", "undefined = 1;\n", "undefined = 1;\n");
    expectPrintedNormalAndMangle( "[undefined] = 1", "[undefined] = 1;\n", "[undefined] = 1;\n");
    expectPrintedNormalAndMangle( "({x: undefined} = 1)", "({ x: undefined } = 1);\n", "({ x: undefined } = 1);\n");
    expectPrintedNormalAndMangle( "with (x) y(undefined); z(undefined)", "with (x) y(undefined);\nz(void 0);\n", "with (x) y(undefined);\nz(void 0);\n");
    expectPrintedNormalAndMangle( "with (x) while (i) y(undefined); z(undefined)", "with (x) while (i) y(undefined);\nz(void 0);\n", "with (x) for (; i; ) y(undefined);\nz(void 0);\n");
}

TEST(JsParser, TestMangleIndex) {
    expectPrintedNormalAndMangle( "x['y']", "x[\"y\"];\n", "x.y;\n");
    expectPrintedNormalAndMangle( "x['y z']", "x[\"y z\"];\n", "x[\"y z\"];\n");
    expectPrintedNormalAndMangle( "x?.['y']", "x?.[\"y\"];\n", "x?.y;\n");
    expectPrintedNormalAndMangle( "x?.['y z']", "x?.[\"y z\"];\n", "x?.[\"y z\"];\n");
    expectPrintedNormalAndMangle( "x?.['y']()", "x?.[\"y\"]();\n", "x?.y();\n");
    expectPrintedNormalAndMangle( "x?.['y z']()", "x?.[\"y z\"]();\n", "x?.[\"y z\"]();\n");
    expectPrintedNormalAndMangle( "x['y' + 'z']", "x[\"yz\"];\n", "x.yz;\n");
    expectPrintedNormalAndMangle( "x?.['y' + 'z']", "x?.[\"yz\"];\n", "x?.[\"yz\"];\n");
    // Check the string-to-int optimization
    expectPrintedNormalAndMangle( "x['0']", "x[\"0\"];\n", "x[0];\n");
    expectPrintedNormalAndMangle( "x['123']", "x[\"123\"];\n", "x[123];\n");
    expectPrintedNormalAndMangle( "x['-123']", "x[\"-123\"];\n", "x[-123];\n");
    expectPrintedNormalAndMangle( "x['-0']", "x[\"-0\"];\n", "x[\"-0\"];\n");
    expectPrintedNormalAndMangle( "x['01']", "x[\"01\"];\n", "x[\"01\"];\n");
    expectPrintedNormalAndMangle( "x['-01']", "x[\"-01\"];\n", "x[\"-01\"];\n");
    expectPrintedNormalAndMangle( "x['0x1']", "x[\"0x1\"];\n", "x[\"0x1\"];\n");
    expectPrintedNormalAndMangle( "x['-0x1']", "x[\"-0x1\"];\n", "x[\"-0x1\"];\n");
    expectPrintedNormalAndMangle( "x['2147483647']", "x[\"2147483647\"];\n", "x[2147483647];\n");
    expectPrintedNormalAndMangle( "x['2147483648']", "x[\"2147483648\"];\n", "x[\"2147483648\"];\n");
    expectPrintedNormalAndMangle( "x['-2147483648']", "x[\"-2147483648\"];\n", "x[-2147483648];\n");
    expectPrintedNormalAndMangle( "x['-2147483649']", "x[\"-2147483649\"];\n", "x[\"-2147483649\"];\n");
}

TEST(JsParser, TestMangleBlock) {
    expectPrintedMangle( "while(1) { while (1) {} }", "for (; ; )\n  for (; ; )\n    ;\n");
    expectPrintedMangle( "while(1) { const x = y; }", "for (; ; ) {\n  const x = y;\n}\n");
    expectPrintedMangle( "while(1) { let x; }", "for (; ; ) {\n  let x;\n}\n");
    expectPrintedMangle( "while(1) { var x; }", "for (; ; )\n  var x;\n");
    expectPrintedMangle( "while(1) { class X {} }", "for (; ; ) {\n  class X {\n  }\n}\n");
    expectPrintedMangle( "while(1) { function x() {} }", "for (; ; )\n  var x = function() {\n  };\n");
    expectPrintedMangle( "while(1) { function* x() {} }", "for (; ; ) {\n  function* x() {\n  }\n}\n");
    expectPrintedMangle( "while(1) { async function x() {} }", "for (; ; ) {\n  async function x() {\n  }\n}\n");
    expectPrintedMangle( "while(1) { async function* x() {} }", "for (; ; ) {\n  async function* x() {\n  }\n}\n");
}

TEST(JsParser, TestMangleSwitch) {
    expectPrintedMangle( "x(); switch (y) { case z: return w; }", "if (x(), y === z)\n  return w;\n");
    expectPrintedMangle( "if (t) { x(); switch (y) { case z: return w; } }", "if (t && (x(), y === z))\n  return w;\n");
    // We potentially need to keep let/const declarations in dead cases
    expectPrintedMangle( "switch (1) { case 0: x; case 1: return x }", "return x;\n");
    expectPrintedMangle( "switch (1) { case 0: var x; case 1: return x }", "switch (1) {\n  case 0:\n    var x;\n  case 1:\n    return x;\n}\n");
    expectPrintedMangle( "switch (1) { case 0: let x; case 1: return x }", "switch (1) {\n  case 0:\n    let x;\n  case 1:\n    return x;\n}\n");
    expectPrintedMangle( "switch (1) { case 0: const x = 0; case 1: return x }", "switch (1) {\n  case 0:\n    const x = 0;\n  case 1:\n    return x;\n}\n");
    expectPrintedMangle( "switch (2) { case 0: var x; case 1: return x }", "if (0)\n  var x;\n");
    expectPrintedMangle( "switch (2) { case 0: let x; case 1: return x }", "");
    expectPrintedMangle( "switch (2) { case 0: const x = 0; case 1: return x }", "");
    expectPrintedMangle( "switch (x) { case 0: a(); break; default: b() }", "x === 0 ? a() : b();\n");
    expectPrintedMangle( "switch (x) { default: a(); break; case 0: b() }", "x === 0 ? b() : a();\n");
    expectPrintedMangle( "switch (x) { case p: a(); break; case q: default: b() }", "switch (x) {\n  case p:\n    a();\n    break;\n  case q:\n  default:\n    b();\n}\n");
    expectPrintedMangle( "switch (x) { case 0: a(); break; case 1: case 2: default: b() }", "x === 0 ? a() : b();\n");
    expectPrintedMangle( "switch (x) { case 0: default: a(); break; case 0: b() }", "switch (x) {\n  case 0:\n  default:\n    a();\n    break;\n  case 0:\n    b();\n}\n");
    expectPrintedMangle( "switch (x) { case 0: if (y) break; a(); break; default: b() }",
        "switch (x) {\n  case 0:\n    if (y) break;\n    a();\n    break;\n  default:\n    b();\n}\n");
    expectPrintedMangle( "switch (x) { case 0: a(); break; default: if (y) break; b() }",
        "switch (x) {\n  case 0:\n    a();\n    break;\n  default:\n    if (y) break;\n    b();\n}\n");
    expectPrintedMangle( "switch (1) { case 0: case 1: case 2: x() }", "x();\n");
    expectPrintedMangle( "switch (1) { case 0: x(); case 1: case 2: y() }", "y();\n");
    expectPrintedMangle( "switch (1) { case 0: x(); case 1: y(); case 2: z() }", "y(), z();\n");
    expectPrintedMangle( "switch (1) { case 0: x(); default: y(); case 2: z() }", "y(), z();\n");
    expectPrintedMangle( "switch (1) { case 0: x(); case 1: y(); break; case 2: z() }", "y();\n");
    expectPrintedMangle( "switch (1) { case 0: x(); default: y(); break; case 2: z() }", "y();\n");
    expectPrintedMangle( "switch (0) { case 0: case y: x() }", "switch (0) {\n  case 0:\n  case y:\n    x();\n}\n");
}

TEST(JsParser, TestMangleAddEmptyString) {
    expectPrintedNormalAndMangle( "a = '' + 0", "a = \"0\";\n", "a = \"0\";\n");
    expectPrintedNormalAndMangle( "a = 0 + ''", "a = \"0\";\n", "a = \"0\";\n");
    expectPrintedNormalAndMangle( "a = '' + b", "a = \"\" + b;\n", "a = \"\" + b;\n");
    expectPrintedNormalAndMangle( "a = b + ''", "a = b + \"\";\n", "a = b + \"\";\n");
    expectPrintedNormalAndMangle( "a = [] + 0", "a = \"0\";\n", "a = \"0\";\n");
    expectPrintedNormalAndMangle( "a = 0 + []", "a = \"0\";\n", "a = \"0\";\n");
    expectPrintedNormalAndMangle( "a = [] + b", "a = [] + b;\n", "a = [] + b;\n");
    expectPrintedNormalAndMangle( "a = b + []", "a = b + [];\n", "a = b + [];\n");
    expectPrintedNormalAndMangle( "a = [b] + 0", "a = [b] + 0;\n", "a = [b] + 0;\n");
    expectPrintedNormalAndMangle( "a = 0 + [b]", "a = 0 + [b];\n", "a = 0 + [b];\n");
    expectPrintedNormalAndMangle( "a = [1, 2] + ''", "a = \"1,2\";\n", "a = \"1,2\";\n");
    expectPrintedNormalAndMangle( "a = [1, 0, 2] + ''", "a = \"1,0,2\";\n", "a = \"1,0,2\";\n");
    expectPrintedNormalAndMangle( "a = [1, null, 2] + ''", "a = \"1,,2\";\n", "a = \"1,,2\";\n");
    expectPrintedNormalAndMangle( "a = [1, undefined, 2] + ''", "a = \"1,,2\";\n", "a = \"1,,2\";\n");
    expectPrintedNormalAndMangle( "a = [1, true, 2] + ''", "a = \"1,true,2\";\n", "a = \"1,true,2\";\n");
    expectPrintedNormalAndMangle( "a = [1, false, 2] + ''", "a = \"1,false,2\";\n", "a = \"1,false,2\";\n");
    expectPrintedNormalAndMangle( "a = [1, , 2] + ''", "a = [1, , 2] + \"\";\n", "a = [1, , 2] + \"\";\n");// Note: Prototype hazards
    expectPrintedNormalAndMangle( "a = [1, , ,] + ''", "a = [1, , ,] + \"\";\n", "a = [1, , ,] + \"\";\n");// Note: Prototype hazards
    expectPrintedNormalAndMangle( "a = {} + 0", "a = \"[object Object]0\";\n", "a = \"[object Object]0\";\n");
    expectPrintedNormalAndMangle( "a = 0 + {}", "a = \"0[object Object]\";\n", "a = \"0[object Object]\";\n");
    expectPrintedNormalAndMangle( "a = {} + b", "a = {} + b;\n", "a = {} + b;\n");
    expectPrintedNormalAndMangle( "a = b + {}", "a = b + {};\n", "a = b + {};\n");
    expectPrintedNormalAndMangle( "a = {toString:()=>1} + 0", "a = { toString: () => 1 } + 0;\n", "a = { toString: () => 1 } + 0;\n");
    expectPrintedNormalAndMangle( "a = 0 + {toString:()=>1}", "a = 0 + { toString: () => 1 };\n", "a = 0 + { toString: () => 1 };\n");
    expectPrintedNormalAndMangle( "a = '' + `${b}`", "a = `${b}`;\n", "a = `${b}`;\n");
    expectPrintedNormalAndMangle( "a = `${b}` + ''", "a = `${b}`;\n", "a = `${b}`;\n");
    expectPrintedNormalAndMangle( "a = '' + typeof b", "a = typeof b;\n", "a = typeof b;\n");
    expectPrintedNormalAndMangle( "a = typeof b + ''", "a = typeof b;\n", "a = typeof b;\n");
    expectPrintedNormalAndMangle( "a = [] + `${b}`", "a = `${b}`;\n", "a = `${b}`;\n");
    expectPrintedNormalAndMangle( "a = `${b}` + []", "a = `${b}`;\n", "a = `${b}`;\n");
    expectPrintedNormalAndMangle( "a = [] + typeof b", "a = typeof b;\n", "a = typeof b;\n");
    expectPrintedNormalAndMangle( "a = typeof b + []", "a = typeof b;\n", "a = typeof b;\n");
    expectPrintedNormalAndMangle( "a = [b] + `${b}`", "a = [b] + `${b}`;\n", "a = [b] + `${b}`;\n");
    expectPrintedNormalAndMangle( "a = `${b}` + [b]", "a = `${b}` + [b];\n", "a = `${b}` + [b];\n");
    expectPrintedNormalAndMangle( "a = {} + `${b}`", "a = `[object Object]${b}`;\n", "a = `[object Object]${b}`;\n");
    expectPrintedNormalAndMangle( "a = `${b}` + {}", "a = `${b}[object Object]`;\n", "a = `${b}[object Object]`;\n");
    expectPrintedNormalAndMangle( "a = {} + typeof b", "a = {} + typeof b;\n", "a = {} + typeof b;\n");
    expectPrintedNormalAndMangle( "a = typeof b + {}", "a = typeof b + {};\n", "a = typeof b + {};\n");
    expectPrintedNormalAndMangle( "a = {toString:()=>1} + `${b}`", "a = { toString: () => 1 } + `${b}`;\n", "a = { toString: () => 1 } + `${b}`;\n");
    expectPrintedNormalAndMangle( "a = `${b}` + {toString:()=>1}", "a = `${b}` + { toString: () => 1 };\n", "a = `${b}` + { toString: () => 1 };\n");
    expectPrintedNormalAndMangle( "a = '' + false", "a = \"false\";\n", "a = \"false\";\n");
    expectPrintedNormalAndMangle( "a = '' + true", "a = \"true\";\n", "a = \"true\";\n");
    expectPrintedNormalAndMangle( "a = false + ''", "a = \"false\";\n", "a = \"false\";\n");
    expectPrintedNormalAndMangle( "a = true + ''", "a = \"true\";\n", "a = \"true\";\n");
    expectPrintedNormalAndMangle( "a = 1 + false + ''", "a = 1 + false + \"\";\n", "a = 1 + false + \"\";\n");
    expectPrintedNormalAndMangle( "a = 0 + true + ''", "a = 0 + true + \"\";\n", "a = 0 + true + \"\";\n");
    expectPrintedNormalAndMangle( "a = '' + null", "a = \"null\";\n", "a = \"null\";\n");
    expectPrintedNormalAndMangle( "a = null + ''", "a = \"null\";\n", "a = \"null\";\n");
    expectPrintedNormalAndMangle( "a = '' + undefined", "a = \"undefined\";\n", "a = \"undefined\";\n");
    expectPrintedNormalAndMangle( "a = undefined + ''", "a = \"undefined\";\n", "a = \"undefined\";\n");
    expectPrintedNormalAndMangle( "a = '' + 0n", "a = \"0\";\n", "a = \"0\";\n");
    expectPrintedNormalAndMangle( "a = '' + 1n", "a = \"1\";\n", "a = \"1\";\n");
    expectPrintedNormalAndMangle( "a = '' + 123n", "a = \"123\";\n", "a = \"123\";\n");
    expectPrintedNormalAndMangle( "a = '' + 1_2_3n", "a = \"123\";\n", "a = \"123\";\n");
    expectPrintedNormalAndMangle( "a = '' + 0b0n", "a = \"\" + 0b0n;\n", "a = \"\" + 0b0n;\n");
    expectPrintedNormalAndMangle( "a = '' + 0o0n", "a = \"\" + 0o0n;\n", "a = \"\" + 0o0n;\n");
    expectPrintedNormalAndMangle( "a = '' + 0x0n", "a = \"\" + 0x0n;\n", "a = \"\" + 0x0n;\n");
    expectPrintedNormalAndMangle( "a = '' + /a\\\\b/ig", "a = \"/a\\\\\\\\b/ig\";\n", "a = \"/a\\\\\\\\b/ig\";\n");
    expectPrintedNormalAndMangle( "a = /a\\\\b/ig + ''", "a = \"/a\\\\\\\\b/ig\";\n", "a = \"/a\\\\\\\\b/ig\";\n");
    expectPrintedNormalAndMangle( "a = '' + ''.constructor", "a = \"function String() { [native code] }\";\n", "a = \"function String() { [native code] }\";\n");
    expectPrintedNormalAndMangle( "a = ''.constructor + ''", "a = \"function String() { [native code] }\";\n", "a = \"function String() { [native code] }\";\n");
    expectPrintedNormalAndMangle( "a = '' + /./.constructor", "a = \"function RegExp() { [native code] }\";\n", "a = \"function RegExp() { [native code] }\";\n");
    expectPrintedNormalAndMangle( "a = /./.constructor + ''", "a = \"function RegExp() { [native code] }\";\n", "a = \"function RegExp() { [native code] }\";\n");
}

TEST(JsParser, TestMangleStringLength) {
    expectPrinted( "a = ''.length", "a = \"\".length;\n");
    expectPrintedMangle( "''.length++", "\"\".length++;\n");
    expectPrintedMangle( "''.length = a", "\"\".length = a;\n");
    expectPrintedMangle( "a = ''.len", "a = \"\".len;\n");
    expectPrintedMangle( "a = [].length", "a = [].length;\n");
    expectPrintedMangle( "a = ''.length", "a = 0;\n");
    expectPrintedMangle( "a = ``.length", "a = 0;\n");
    expectPrintedMangle( "a = b``.length", "a = b``.length;\n");
    expectPrintedMangle( "a = 'abc'.length", "a = 3;\n");
    expectPrintedMangle( "a = 'ȧḃċ'.length", "a = 3;\n");
    expectPrintedMangle( "a = '👯‍♂️'.length", "a = 5;\n");
}

TEST(JsParser, TestMangleStringIndex) {
    expectPrinted( "a = 'abc'[0]", "a = \"abc\"[0];\n");
    expectPrintedMangle( "a = 'abc'[-1]", "a = \"abc\"[-1];\n");
    expectPrintedMangle( "a = 'abc'[-0]", "a = \"a\";\n");
    expectPrintedMangle( "a = 'abc'[0]", "a = \"a\";\n");
    expectPrintedMangle( "a = 'abc'[2]", "a = \"c\";\n");
    expectPrintedMangle( "a = 'abc'[3]", "a = \"abc\"[3];\n");
    expectPrintedMangle( "a = 'abc'[NaN]", "a = \"abc\"[NaN];\n");
    expectPrintedMangle( "a = 'abc'[-1e100]", "a = \"abc\"[-1e100];\n");
    expectPrintedMangle( "a = 'abc'[1e100]", "a = \"abc\"[1e100];\n");
    expectPrintedMangle( "a = 'abc'[-Infinity]", "a = \"abc\"[-Infinity];\n");
    expectPrintedMangle( "a = 'abc'[Infinity]", "a = \"abc\"[Infinity];\n");
}

TEST(JsParser, TestMangleNot) {
    // These can be mangled
    expectPrintedNormalAndMangle( "a = !(b == c)", "a = !(b == c);\n", "a = b != c;\n");
    expectPrintedNormalAndMangle( "a = !(b != c)", "a = !(b != c);\n", "a = b == c;\n");
    expectPrintedNormalAndMangle( "a = !(b === c)", "a = !(b === c);\n", "a = b !== c;\n");
    expectPrintedNormalAndMangle( "a = !(b !== c)", "a = !(b !== c);\n", "a = b === c;\n");
    expectPrintedNormalAndMangle( "if (!(a, b)) return c", "if (!(a, b)) return c;\n", "if (a, !b) return c;\n");
    // These can't be mangled due to NaN and other special cases
    expectPrintedNormalAndMangle( "a = !(b < c)", "a = !(b < c);\n", "a = !(b < c);\n");
    expectPrintedNormalAndMangle( "a = !(b > c)", "a = !(b > c);\n", "a = !(b > c);\n");
    expectPrintedNormalAndMangle( "a = !(b <= c)", "a = !(b <= c);\n", "a = !(b <= c);\n");
    expectPrintedNormalAndMangle( "a = !(b >= c)", "a = !(b >= c);\n", "a = !(b >= c);\n");
}

TEST(JsParser, TestMangleDoubleNot) {
    expectPrintedNormalAndMangle( "a = !!b", "a = !!b;\n", "a = !!b;\n");
    expectPrintedNormalAndMangle( "a = !!!b", "a = !!!b;\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = !!-b", "a = !!-b;\n", "a = !!-b;\n");
    expectPrintedNormalAndMangle( "a = !!void b", "a = !!void b;\n", "a = !!void b;\n");
    expectPrintedNormalAndMangle( "a = !!delete b", "a = !!delete b;\n", "a = delete b;\n");
    expectPrintedNormalAndMangle( "a = !!(b + c)", "a = !!(b + c);\n", "a = !!(b + c);\n");
    expectPrintedNormalAndMangle( "a = !!(b == c)", "a = !!(b == c);\n", "a = b == c;\n");
    expectPrintedNormalAndMangle( "a = !!(b != c)", "a = !!(b != c);\n", "a = b != c;\n");
    expectPrintedNormalAndMangle( "a = !!(b === c)", "a = !!(b === c);\n", "a = b === c;\n");
    expectPrintedNormalAndMangle( "a = !!(b !== c)", "a = !!(b !== c);\n", "a = b !== c;\n");
    expectPrintedNormalAndMangle( "a = !!(b < c)", "a = !!(b < c);\n", "a = b < c;\n");
    expectPrintedNormalAndMangle( "a = !!(b > c)", "a = !!(b > c);\n", "a = b > c;\n");
    expectPrintedNormalAndMangle( "a = !!(b <= c)", "a = !!(b <= c);\n", "a = b <= c;\n");
    expectPrintedNormalAndMangle( "a = !!(b >= c)", "a = !!(b >= c);\n", "a = b >= c;\n");
    expectPrintedNormalAndMangle( "a = !!(b in c)", "a = !!(b in c);\n", "a = b in c;\n");
    expectPrintedNormalAndMangle( "a = !!(b instanceof c)", "a = !!(b instanceof c);\n", "a = b instanceof c;\n");
    expectPrintedNormalAndMangle( "a = !!(b && c)", "a = !!(b && c);\n", "a = !!(b && c);\n");
    expectPrintedNormalAndMangle( "a = !!(b || c)", "a = !!(b || c);\n", "a = !!(b || c);\n");
    expectPrintedNormalAndMangle( "a = !!(b ?? c)", "a = !!(b ?? c);\n", "a = !!(b ?? c);\n");
    expectPrintedNormalAndMangle( "a = !!(!b && c)", "a = !!(!b && c);\n", "a = !!(!b && c);\n");
    expectPrintedNormalAndMangle( "a = !!(!b || c)", "a = !!(!b || c);\n", "a = !!(!b || c);\n");
    expectPrintedNormalAndMangle( "a = !!(!b ?? c)", "a = !!!b;\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = !!(b && !c)", "a = !!(b && !c);\n", "a = !!(b && !c);\n");
    expectPrintedNormalAndMangle( "a = !!(b || !c)", "a = !!(b || !c);\n", "a = !!(b || !c);\n");
    expectPrintedNormalAndMangle( "a = !!(b ?? !c)", "a = !!(b ?? !c);\n", "a = !!(b ?? !c);\n");
    expectPrintedNormalAndMangle( "a = !!(!b && !c)", "a = !!(!b && !c);\n", "a = !b && !c;\n");
    expectPrintedNormalAndMangle( "a = !!(!b || !c)", "a = !!(!b || !c);\n", "a = !b || !c;\n");
    expectPrintedNormalAndMangle( "a = !!(!b ?? !c)", "a = !!!b;\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = !!(b, c)", "a = !!(b, c);\n", "a = (b, !!c);\n");
}

TEST(JsParser, TestMangleBooleanConstructor) {
    expectPrintedNormalAndMangle( "a = Boolean(b); var Boolean", "a = Boolean(b);\nvar Boolean;\n", "a = Boolean(b);\nvar Boolean;\n");
    expectPrintedNormalAndMangle( "a = Boolean()", "a = Boolean();\n", "a = false;\n");
    expectPrintedNormalAndMangle( "a = Boolean(b)", "a = Boolean(b);\n", "a = !!b;\n");
    expectPrintedNormalAndMangle( "a = Boolean(!b)", "a = Boolean(!b);\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = Boolean(!!b)", "a = Boolean(!!b);\n", "a = !!b;\n");
    expectPrintedNormalAndMangle( "a = Boolean(b ? true : false)", "a = Boolean(b ? true : false);\n", "a = !!b;\n");
    expectPrintedNormalAndMangle( "a = Boolean(b ? false : true)", "a = Boolean(b ? false : true);\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = Boolean(b ? c > 0 : c < 0)", "a = Boolean(b ? c > 0 : c < 0);\n", "a = b ? c > 0 : c < 0;\n");
    // Check for calling "SimplifyBooleanExpr" on the argument
    expectPrintedNormalAndMangle( "a = Boolean((b | c) !== 0)", "a = Boolean((b | c) !== 0);\n", "a = (b | c) !== 0;\n");
    expectPrintedNormalAndMangle( "a = Boolean((b >>> c) !== 0)", "a = Boolean(b >>> c !== 0);\n", "a = !!(b >>> c);\n");
    expectPrintedNormalAndMangle( "a = Boolean(b ? (c | d) !== 0 : (d | e) !== 0)",
        "a = Boolean(b ? (c | d) !== 0 : (d | e) !== 0);\n", "a = b ? (c | d) !== 0 : (d | e) !== 0;\n");
    expectPrintedNormalAndMangle( "a = Boolean(b ? (c >>> d) !== 0 : (d >>> e) !== 0)",
        "a = Boolean(b ? c >>> d !== 0 : d >>> e !== 0);\n", "a = !!(b ? c >>> d : d >>> e);\n");
}

TEST(JsParser, TestMangleNumberConstructor) {
    expectPrintedNormalAndMangle( "a = Number(x)", "a = Number(x);\n", "a = Number(x);\n");
    expectPrintedNormalAndMangle( "a = Number(0n)", "a = Number(0n);\n", "a = Number(0n);\n");
    expectPrintedNormalAndMangle( "a = Number(false); var Number", "a = Number(false);\nvar Number;\n", "a = Number(false);\nvar Number;\n");
    expectPrintedNormalAndMangle( "a = Number(0xFFFF_FFFF_FFFF_FFFFn)", "a = Number(0xFFFFFFFFFFFFFFFFn);\n", "a = Number(0xFFFFFFFFFFFFFFFFn);\n");
    expectPrintedNormalAndMangle( "a = Number()", "a = Number();\n", "a = 0;\n");
    expectPrintedNormalAndMangle( "a = Number(-123)", "a = Number(-123);\n", "a = -123;\n");
    expectPrintedNormalAndMangle( "a = Number(false)", "a = Number(false);\n", "a = 0;\n");
    expectPrintedNormalAndMangle( "a = Number(true)", "a = Number(true);\n", "a = 1;\n");
    expectPrintedNormalAndMangle( "a = Number(undefined)", "a = Number(void 0);\n", "a = NaN;\n");
    expectPrintedNormalAndMangle( "a = Number(null)", "a = Number(null);\n", "a = 0;\n");
    expectPrintedNormalAndMangle( "a = Number(b ? !c : !d)", "a = Number(b ? !c : !d);\n", "a = +(b ? !c : !d);\n");
}

TEST(JsParser, TestMangleStringConstructor) {
    expectPrintedNormalAndMangle( "a = String(x)", "a = String(x);\n", "a = String(x);\n");
    expectPrintedNormalAndMangle( "a = String('x'); var String", "a = String(\"x\");\nvar String;\n", "a = String(\"x\");\nvar String;\n");
    expectPrintedNormalAndMangle( "a = String()", "a = String();\n", "a = \"\";\n");
    expectPrintedNormalAndMangle( "a = String('x')", "a = String(\"x\");\n", "a = \"x\";\n");
    expectPrintedNormalAndMangle( "a = String(b ? 'x' : 'y')", "a = String(b ? \"x\" : \"y\");\n", "a = b ? \"x\" : \"y\";\n");
}

TEST(JsParser, TestMangleBigIntConstructor) {
    expectPrintedNormalAndMangle( "a = BigInt(x)", "a = BigInt(x);\n", "a = BigInt(x);\n");
    expectPrintedNormalAndMangle( "a = BigInt(0n); var BigInt", "a = BigInt(0n);\nvar BigInt;\n", "a = BigInt(0n);\nvar BigInt;\n");
    // Note: This throws instead of returning "0n"
    expectPrintedNormalAndMangle( "a = BigInt()", "a = BigInt();\n", "a = BigInt();\n");
    // Note: Transforming this into "0n" is unsafe because that syntax may not be supported
    expectPrintedNormalAndMangle( "a = BigInt('0')", "a = BigInt(\"0\");\n", "a = BigInt(\"0\");\n");
    expectPrintedNormalAndMangle( "a = BigInt(0n)", "a = BigInt(0n);\n", "a = 0n;\n");
    expectPrintedNormalAndMangle( "a = BigInt(b ? 0n : 1n)", "a = BigInt(b ? 0n : 1n);\n", "a = b ? 0n : 1n;\n");
}

TEST(JsParser, TestMangleCharCodeAt) {
    expectPrinted( "a = 'xy'.charCodeAt(0)", "a = \"xy\".charCodeAt(0);\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt()", "a = 120;\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(0)", "a = 120;\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(1)", "a = 121;\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(-1)", "a = NaN;\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(2)", "a = NaN;\n");
    expectPrintedMangle( "a = '🧀'.charCodeAt()", "a = 55358;\n");
    expectPrintedMangle( "a = '🧀'.charCodeAt(0)", "a = 55358;\n");
    expectPrintedMangle( "a = '🧀'.charCodeAt(1)", "a = 56768;\n");
    expectPrintedMangle( "a = '🧀'.charCodeAt(-1)", "a = NaN;\n");
    expectPrintedMangle( "a = '🧀'.charCodeAt(2)", "a = NaN;\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(NaN)", "a = \"xy\".charCodeAt(NaN);\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(-Infinity)", "a = \"xy\".charCodeAt(-Infinity);\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(Infinity)", "a = \"xy\".charCodeAt(Infinity);\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(0.5)", "a = \"xy\".charCodeAt(0.5);\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(1e99)", "a = \"xy\".charCodeAt(1e99);\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt('1')", "a = \"xy\".charCodeAt(\"1\");\n");
    expectPrintedMangle( "a = 'xy'.charCodeAt(1, 2)", "a = \"xy\".charCodeAt(1, 2);\n");
}

TEST(JsParser, TestMangleFromCharCode) {
    expectPrinted( "a = String.fromCharCode(120, 121)", "a = String.fromCharCode(120, 121);\n");
    expectPrintedMangle( "a = String.fromCharCode()", "a = \"\";\n");
    expectPrintedMangle( "a = String.fromCharCode(0)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode(120)", "a = \"x\";\n");
    expectPrintedMangle( "a = String.fromCharCode(120, 121)", "a = \"xy\";\n");
    expectPrintedMangle( "a = String.fromCharCode(55358, 56768)", "a = \"🧀\";\n");
    expectPrintedMangle( "a = String.fromCharCode(0x10000)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode(0x10078, 0x10079)", "a = \"xy\";\n");
    expectPrintedMangle( "a = String.fromCharCode(0x1_0000_FFFF)", "a = \"\uFFFF\";\n");
    expectPrintedMangle( "a = String.fromCharCode(NaN)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode(-Infinity)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode(Infinity)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode(null)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode(undefined)", "a = \"\\0\";\n");
    expectPrintedMangle( "a = String.fromCharCode('123')", "a = \"{\";\n");
    expectPrintedMangle( "a = String.fromCharCode(x)", "a = String.fromCharCode(x);\n");
    expectPrintedMangle( "a = String.fromCharCode('x')", "a = String.fromCharCode(\"x\");\n");
    expectPrintedMangle( "a = String.fromCharCode('0.5')", "a = String.fromCharCode(\"0.5\");\n");
}

TEST(JsParser, TestMangleToString) {
    expectPrinted( "a = \"xy\".toString()", "a = \"xy\".toString();\n");
    expectPrintedMangle( "a = false.toString()", "a = \"false\";\n");
    expectPrintedMangle( "a = true.toString()", "a = \"true\";\n");
    expectPrintedMangle( "a = \"xy\".toString()", "a = \"xy\";\n");
    expectPrintedMangle( "a = 0 .toString()", "a = \"0\";\n");
    expectPrintedMangle( "a = (-0).toString()", "a = \"0\";\n");
    expectPrintedMangle( "a = 123 .toString()", "a = \"123\";\n");
    expectPrintedMangle( "a = (-123).toString()", "a = \"-123\";\n");
    expectPrintedMangle( "a = NaN.toString()", "a = \"NaN\";\n");
    expectPrintedMangle( "a = Infinity.toString()", "a = \"Infinity\";\n");
    expectPrintedMangle( "a = (-Infinity).toString()", "a = \"-Infinity\";\n");
    expectPrintedMangle( "a = /a\\\\b/ig.toString()", "a = \"/a\\\\\\\\b/ig\";\n");
    // Handle a radix other than 10
    expectPrintedMangle( "a = 100 .toString(0)", "a = 100 .toString(0);\n");
    expectPrintedMangle( "a = 100 .toString(1)", "a = 100 .toString(1);\n");
    expectPrintedMangle( "a = 100 .toString(2)", "a = \"1100100\";\n");
    expectPrintedMangle( "a = 100 .toString(5)", "a = \"400\";\n");
    expectPrintedMangle( "a = 100 .toString(8)", "a = \"144\";\n");
    expectPrintedMangle( "a = 100 .toString(13)", "a = \"79\";\n");
    expectPrintedMangle( "a = 100 .toString(16)", "a = \"64\";\n");
    expectPrintedMangle( "a = 10000 .toString(19)", "a = \"18d6\";\n");
    expectPrintedMangle( "a = 10000 .toString(23)", "a = \"iki\";\n");
    expectPrintedMangle( "a = 1000000 .toString(29)", "a = \"1c01m\";\n");
    expectPrintedMangle( "a = 1000000 .toString(31)", "a = \"12hi2\";\n");
    expectPrintedMangle( "a = 1000000 .toString(36)", "a = \"lfls\";\n");
    expectPrintedMangle( "a = (-1000000).toString(36)", "a = \"-lfls\";\n");
    expectPrintedMangle( "a = 0 .toString(36)", "a = \"0\";\n");
    expectPrintedMangle( "a = (-0).toString(36)", "a = \"0\";\n");
    expectPrintedMangle( "a = false.toString(b)", "a = false.toString(b);\n");
    expectPrintedMangle( "a = true.toString(b)", "a = true.toString(b);\n");
    expectPrintedMangle( "a = \"xy\".toString(b)", "a = \"xy\".toString(b);\n");
    expectPrintedMangle( "a = 123 .toString(b)", "a = 123 .toString(b);\n");
    expectPrintedMangle( "a = 0.5.toString()", "a = 0.5.toString();\n");
    expectPrintedMangle( "a = 1e99.toString(b)", "a = 1e99.toString(b);\n");
    expectPrintedMangle( "a = /./.toString(b)", "a = /./.toString(b);\n");
}

TEST(JsParser, TestMangleIf) {
    expectPrintedNormalAndMangle( "1 ? a() : b()", "1 ? a() : b();\n", "a();\n");
    expectPrintedNormalAndMangle( "0 ? a() : b()", "0 ? a() : b();\n", "b();\n");
    expectPrintedNormalAndMangle( "a ? a : b", "a ? a : b;\n", "a || b;\n");
    expectPrintedNormalAndMangle( "a ? b : a", "a ? b : a;\n", "a && b;\n");
    expectPrintedNormalAndMangle( "a.x ? a.x : b", "a.x ? a.x : b;\n", "a.x ? a.x : b;\n");
    expectPrintedNormalAndMangle( "a.x ? b : a.x", "a.x ? b : a.x;\n", "a.x ? b : a.x;\n");
    expectPrintedNormalAndMangle( "a ? b() : c()", "a ? b() : c();\n", "a ? b() : c();\n");
    expectPrintedNormalAndMangle( "!a ? b() : c()", "!a ? b() : c();\n", "a ? c() : b();\n");
    expectPrintedNormalAndMangle( "!!a ? b() : c()", "!!a ? b() : c();\n", "a ? b() : c();\n");
    expectPrintedNormalAndMangle( "!!!a ? b() : c()", "!!!a ? b() : c();\n", "a ? c() : b();\n");
    expectPrintedNormalAndMangle( "if (1) a(); else b()", "if (1) a();\nelse b();\n", "a();\n");
    expectPrintedNormalAndMangle( "if (0) a(); else b()", "if (0) a();\nelse b();\n", "b();\n");
    expectPrintedNormalAndMangle( "if (a) b(); else c()", "if (a) b();\nelse c();\n", "a ? b() : c();\n");
    expectPrintedNormalAndMangle( "if (!a) b(); else c()", "if (!a) b();\nelse c();\n", "a ? c() : b();\n");
    expectPrintedNormalAndMangle( "if (!!a) b(); else c()", "if (!!a) b();\nelse c();\n", "a ? b() : c();\n");
    expectPrintedNormalAndMangle( "if (!!!a) b(); else c()", "if (!!!a) b();\nelse c();\n", "a ? c() : b();\n");
    expectPrintedNormalAndMangle( "if (1) a()", "if (1) a();\n", "a();\n");
    expectPrintedNormalAndMangle( "if (0) a()", "if (0) a();\n", "");
    expectPrintedNormalAndMangle( "if (a) b()", "if (a) b();\n", "a && b();\n");
    expectPrintedNormalAndMangle( "if (!a) b()", "if (!a) b();\n", "a || b();\n");
    expectPrintedNormalAndMangle( "if (!!a) b()", "if (!!a) b();\n", "a && b();\n");
    expectPrintedNormalAndMangle( "if (!!!a) b()", "if (!!!a) b();\n", "a || b();\n");
    expectPrintedNormalAndMangle( "if (1) {} else a()", "if (1) {\n} else a();\n", "");
    expectPrintedNormalAndMangle( "if (0) {} else a()", "if (0) {\n} else a();\n", "a();\n");
    expectPrintedNormalAndMangle( "if (a) {} else b()", "if (a) {\n} else b();\n", "a || b();\n");
    expectPrintedNormalAndMangle( "if (!a) {} else b()", "if (!a) {\n} else b();\n", "a && b();\n");
    expectPrintedNormalAndMangle( "if (!!a) {} else b()", "if (!!a) {\n} else b();\n", "a || b();\n");
    expectPrintedNormalAndMangle( "if (!!!a) {} else b()", "if (!!!a) {\n} else b();\n", "a && b();\n");
    expectPrintedNormalAndMangle( "if (a) {} else throw b", "if (a) {\n} else throw b;\n", "if (!a)\n  throw b;\n");
    expectPrintedNormalAndMangle( "if (!a) {} else throw b", "if (!a) {\n} else throw b;\n", "if (a)\n  throw b;\n");
    expectPrintedNormalAndMangle( "a(); if (b) throw c", "a();\nif (b) throw c;\n", "if (a(), b) throw c;\n");
    expectPrintedNormalAndMangle( "if (a) if (b) throw c", "if (a) {\n  if (b) throw c;\n}\n", "if (a && b) throw c;\n");
    expectPrintedMangle( "if (true) { let a = b; if (c) throw d }",
        "{\n  let a = b;\n  if (c) throw d;\n}\n");
    expectPrintedMangle( "if (true) { if (a) throw b; if (c) throw d }",
        "if (a) throw b;\nif (c) throw d;\n");
    expectPrintedMangle( "if (false) throw a; else { let b = c; if (d) throw e }",
        "{\n  let b = c;\n  if (d) throw e;\n}\n");
    expectPrintedMangle( "if (false) throw a; else { if (b) throw c; if (d) throw e }",
        "if (b) throw c;\nif (d) throw e;\n");
    expectPrintedMangle( "if (a) { if (b) throw c; else { let d = e; if (f) throw g } }",
        "if (a) {\n  if (b) throw c;\n  {\n    let d = e;\n    if (f) throw g;\n  }\n}\n");
    expectPrintedMangle( "if (a) { if (b) throw c; else if (d) throw e; else if (f) throw g }",
        "if (a) {\n  if (b) throw c;\n  if (d) throw e;\n  if (f) throw g;\n}\n");
    expectPrintedNormalAndMangle( "a = b ? true : false", "a = b ? true : false;\n", "a = !!b;\n");
    expectPrintedNormalAndMangle( "a = b ? false : true", "a = b ? false : true;\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = !b ? true : false", "a = !b ? true : false;\n", "a = !b;\n");
    expectPrintedNormalAndMangle( "a = !b ? false : true", "a = !b ? false : true;\n", "a = !!b;\n");
    expectPrintedNormalAndMangle( "a = b == c ? true : false", "a = b == c ? true : false;\n", "a = b == c;\n");
    expectPrintedNormalAndMangle( "a = b != c ? true : false", "a = b != c ? true : false;\n", "a = b != c;\n");
    expectPrintedNormalAndMangle( "a = b === c ? true : false", "a = b === c ? true : false;\n", "a = b === c;\n");
    expectPrintedNormalAndMangle( "a = b !== c ? true : false", "a = b !== c ? true : false;\n", "a = b !== c;\n");
    expectPrintedNormalAndMangle( "a ? b(c) : b(d)", "a ? b(c) : b(d);\n", "a ? b(c) : b(d);\n");
    expectPrintedNormalAndMangle( "let a; a ? b(c) : b(d)", "let a;\na ? b(c) : b(d);\n", "let a;\na ? b(c) : b(d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c) : b(d)", "let a, b;\na ? b(c) : b(d);\n", "let a, b;\nb(a ? c : d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c, 0) : b(d)", "let a, b;\na ? b(c, 0) : b(d);\n", "let a, b;\na ? b(c, 0) : b(d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c) : b(d, 0)", "let a, b;\na ? b(c) : b(d, 0);\n", "let a, b;\na ? b(c) : b(d, 0);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c, 0) : b(d, 1)", "let a, b;\na ? b(c, 0) : b(d, 1);\n", "let a, b;\na ? b(c, 0) : b(d, 1);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c, 0) : b(d, 0)", "let a, b;\na ? b(c, 0) : b(d, 0);\n", "let a, b;\nb(a ? c : d, 0);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(...c) : b(d)", "let a, b;\na ? b(...c) : b(d);\n", "let a, b;\na ? b(...c) : b(d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c) : b(...d)", "let a, b;\na ? b(c) : b(...d);\n", "let a, b;\na ? b(c) : b(...d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(...c) : b(...d)", "let a, b;\na ? b(...c) : b(...d);\n", "let a, b;\nb(...a ? c : d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(a) : b(c)", "let a, b;\na ? b(a) : b(c);\n", "let a, b;\nb(a || c);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(c) : b(a)", "let a, b;\na ? b(c) : b(a);\n", "let a, b;\nb(a && c);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(...a) : b(...c)", "let a, b;\na ? b(...a) : b(...c);\n", "let a, b;\nb(...a || c);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b(...c) : b(...a)", "let a, b;\na ? b(...c) : b(...a);\n", "let a, b;\nb(...a && c);\n");
    // Note: "a.x" may change "b" and "b.y" may change "a" in the examples
    // below, so the presence of these expressions must prevent reordering
    expectPrintedNormalAndMangle( "let a; a.x ? b(c) : b(d)", "let a;\na.x ? b(c) : b(d);\n", "let a;\na.x ? b(c) : b(d);\n");
    expectPrintedNormalAndMangle( "let a, b; a.x ? b(c) : b(d)", "let a, b;\na.x ? b(c) : b(d);\n", "let a, b;\na.x ? b(c) : b(d);\n");
    expectPrintedNormalAndMangle( "let a, b; a ? b.y(c) : b.y(d)", "let a, b;\na ? b.y(c) : b.y(d);\n", "let a, b;\na ? b.y(c) : b.y(d);\n");
    expectPrintedNormalAndMangle( "let a, b; a.x ? b.y(c) : b.y(d)", "let a, b;\na.x ? b.y(c) : b.y(d);\n", "let a, b;\na.x ? b.y(c) : b.y(d);\n");
    expectPrintedNormalAndMangle( "a ? b : c ? b : d", "a ? b : c ? b : d;\n", "a || c ? b : d;\n");
    expectPrintedNormalAndMangle( "a ? b ? c : d : d", "a ? b ? c : d : d;\n", "a && b ? c : d;\n");
    expectPrintedNormalAndMangle( "a ? c : (b, c)", "a ? c : (b, c);\n", "a || b, c;\n");
    expectPrintedNormalAndMangle( "a ? (b, c) : c", "a ? (b, c) : c;\n", "a && b, c;\n");
    expectPrintedNormalAndMangle( "a ? c : (b, d)", "a ? c : (b, d);\n", "a ? c : (b, d);\n");
    expectPrintedNormalAndMangle( "a ? (b, c) : d", "a ? (b, c) : d;\n", "a ? (b, c) : d;\n");
    expectPrintedNormalAndMangle( "a ? b || c : c", "a ? b || c : c;\n", "a && b || c;\n");
    expectPrintedNormalAndMangle( "a ? b || c : d", "a ? b || c : d;\n", "a ? b || c : d;\n");
    expectPrintedNormalAndMangle( "a ? b && c : c", "a ? b && c : c;\n", "a ? b && c : c;\n");
    expectPrintedNormalAndMangle( "a ? c : b && c", "a ? c : b && c;\n", "(a || b) && c;\n");
    expectPrintedNormalAndMangle( "a ? c : b && d", "a ? c : b && d;\n", "a ? c : b && d;\n");
    expectPrintedNormalAndMangle( "a ? c : b || c", "a ? c : b || c;\n", "a ? c : b || c;\n");
    expectPrintedNormalAndMangle( "a = b == null ? c : b", "a = b == null ? c : b;\n", "a = b == null ? c : b;\n");
    expectPrintedNormalAndMangle( "a = b != null ? b : c", "a = b != null ? b : c;\n", "a = b != null ? b : c;\n");
    expectPrintedNormalAndMangle( "let b; a = b == null ? c : b", "let b;\na = b == null ? c : b;\n", "let b;\na = b ?? c;\n");
    expectPrintedNormalAndMangle( "let b; a = b != null ? b : c", "let b;\na = b != null ? b : c;\n", "let b;\na = b ?? c;\n");
    expectPrintedNormalAndMangle( "let b; a = b == null ? b : c", "let b;\na = b == null ? b : c;\n", "let b;\na = b == null ? b : c;\n");
    expectPrintedNormalAndMangle( "let b; a = b != null ? c : b", "let b;\na = b != null ? c : b;\n", "let b;\na = b != null ? c : b;\n");
    expectPrintedNormalAndMangle( "let b; a = null == b ? c : b", "let b;\na = null == b ? c : b;\n", "let b;\na = b ?? c;\n");
    expectPrintedNormalAndMangle( "let b; a = null != b ? b : c", "let b;\na = null != b ? b : c;\n", "let b;\na = b ?? c;\n");
    expectPrintedNormalAndMangle( "let b; a = null == b ? b : c", "let b;\na = null == b ? b : c;\n", "let b;\na = b == null ? b : c;\n");
    expectPrintedNormalAndMangle( "let b; a = null != b ? c : b", "let b;\na = null != b ? c : b;\n", "let b;\na = b != null ? c : b;\n");
    // Don't do this if the condition has side effects
    expectPrintedNormalAndMangle( "let b; a = b.x == null ? c : b.x", "let b;\na = b.x == null ? c : b.x;\n", "let b;\na = b.x == null ? c : b.x;\n");
    expectPrintedNormalAndMangle( "let b; a = b.x != null ? b.x : c", "let b;\na = b.x != null ? b.x : c;\n", "let b;\na = b.x != null ? b.x : c;\n");
    expectPrintedNormalAndMangle( "let b; a = null == b.x ? c : b.x", "let b;\na = null == b.x ? c : b.x;\n", "let b;\na = b.x == null ? c : b.x;\n");
    expectPrintedNormalAndMangle( "let b; a = null != b.x ? b.x : c", "let b;\na = null != b.x ? b.x : c;\n", "let b;\na = b.x != null ? b.x : c;\n");
    // Don't do this for strict equality comparisons
    expectPrintedNormalAndMangle( "let b; a = b === null ? c : b", "let b;\na = b === null ? c : b;\n", "let b;\na = b === null ? c : b;\n");
    expectPrintedNormalAndMangle( "let b; a = b !== null ? b : c", "let b;\na = b !== null ? b : c;\n", "let b;\na = b !== null ? b : c;\n");
    expectPrintedNormalAndMangle( "let b; a = null === b ? c : b", "let b;\na = null === b ? c : b;\n", "let b;\na = b === null ? c : b;\n");
    expectPrintedNormalAndMangle( "let b; a = null !== b ? b : c", "let b;\na = null !== b ? b : c;\n", "let b;\na = b !== null ? b : c;\n");
    expectPrintedNormalAndMangle( "let b; a = null === b || b === undefined ? c : b", "let b;\na = null === b || b === void 0 ? c : b;\n", "let b;\na = b ?? c;\n");
    expectPrintedNormalAndMangle( "let b; a = b !== undefined && b !== null ? b : c", "let b;\na = b !== void 0 && b !== null ? b : c;\n", "let b;\na = b ?? c;\n");
    // Distinguish between negative an non-negative zero (i.e. Object.is)
    // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Equality_comparisons_and_sameness
    expectPrintedNormalAndMangle( "a(b ? 0 : 0)", "a(b ? 0 : 0);\n", "a((b, 0));\n");
    expectPrintedNormalAndMangle( "a(b ? +0 : -0)", "a(b ? 0 : -0);\n", "a(b ? 0 : -0);\n");
    expectPrintedNormalAndMangle( "a(b ? +0 : 0)", "a(b ? 0 : 0);\n", "a((b, 0));\n");
    expectPrintedNormalAndMangle( "a(b ? -0 : 0)", "a(b ? -0 : 0);\n", "a(b ? -0 : 0);\n");
    expectPrintedNormalAndMangle( "a ? b : b", "a ? b : b;\n", "a, b;\n");
    expectPrintedNormalAndMangle( "let a; a ? b : b", "let a;\na ? b : b;\n", "let a;\nb;\n");
    expectPrintedNormalAndMangle( "a ? -b : -b", "a ? -b : -b;\n", "a, -b;\n");
    expectPrintedNormalAndMangle( "a ? b.c : b.c", "a ? b.c : b.c;\n", "a, b.c;\n");
    expectPrintedNormalAndMangle( "a ? b?.c : b?.c", "a ? b?.c : b?.c;\n", "a, b?.c;\n");
    expectPrintedNormalAndMangle( "a ? b[c] : b[c]", "a ? b[c] : b[c];\n", "a, b[c];\n");
    expectPrintedNormalAndMangle( "a ? b() : b()", "a ? b() : b();\n", "a, b();\n");
    expectPrintedNormalAndMangle( "a ? b?.() : b?.()", "a ? b?.() : b?.();\n", "a, b?.();\n");
    expectPrintedNormalAndMangle( "a ? b?.[c] : b?.[c]", "a ? b?.[c] : b?.[c];\n", "a, b?.[c];\n");
    expectPrintedNormalAndMangle( "a ? b == c : b == c", "a ? b == c : b == c;\n", "a, b == c;\n");
    expectPrintedNormalAndMangle( "a ? b.c(d + e[f]) : b.c(d + e[f])", "a ? b.c(d + e[f]) : b.c(d + e[f]);\n", "a, b.c(d + e[f]);\n");
    expectPrintedNormalAndMangle( "a ? -b : !b", "a ? -b : !b;\n", "a ? -b : b;\n");
    expectPrintedNormalAndMangle( "a ? b() : b(c)", "a ? b() : b(c);\n", "a ? b() : b(c);\n");
    expectPrintedNormalAndMangle( "a ? b(c) : b(d)", "a ? b(c) : b(d);\n", "a ? b(c) : b(d);\n");
    expectPrintedNormalAndMangle( "a ? b?.c : b.c", "a ? b?.c : b.c;\n", "a ? b?.c : b.c;\n");
    expectPrintedNormalAndMangle( "a ? b?.() : b()", "a ? b?.() : b();\n", "a ? b?.() : b();\n");
    expectPrintedNormalAndMangle( "a ? b?.[c] : b[c]", "a ? b?.[c] : b[c];\n", "a ? b?.[c] : b[c];\n");
    expectPrintedNormalAndMangle( "a ? b == c : b != c", "a ? b == c : b != c;\n", "a ? b == c : b != c;\n");
    expectPrintedNormalAndMangle( "a ? b.c(d + e[f]) : b.c(d + e[g])", "a ? b.c(d + e[f]) : b.c(d + e[g]);\n", "a ? b.c(d + e[f]) : b.c(d + e[g]);\n");
    expectPrintedNormalAndMangle( "(a, b) ? c : d", "(a, b) ? c : d;\n", "a, b ? c : d;\n");
    expectPrintedNormalAndMangle( "return a && ((b && c) && (d && e))", "return a && (b && c && (d && e));\n", "return a && b && c && d && e;\n");
    expectPrintedNormalAndMangle( "return a || ((b || c) || (d || e))", "return a || (b || c || (d || e));\n", "return a || b || c || d || e;\n");
    expectPrintedNormalAndMangle( "return a ?? ((b ?? c) ?? (d ?? e))", "return a ?? (b ?? c ?? (d ?? e));\n", "return a ?? b ?? c ?? d ?? e;\n");
    expectPrintedNormalAndMangle( "if (a) if (b) if (c) d", "if (a) {\n  if (b) {\n    if (c) d;\n  }\n}\n", "a && b && c && d;\n");
    expectPrintedNormalAndMangle( "if (!a) if (!b) if (!c) d", "if (!a) {\n  if (!b) {\n    if (!c) d;\n  }\n}\n", "a || b || c || d;\n");
    expectPrintedNormalAndMangle( "let a, b, c; return a != null ? a : b != null ? b : c", "let a, b, c;\nreturn a != null ? a : b != null ? b : c;\n", "let a, b, c;\nreturn a ?? b ?? c;\n");
    expectPrintedMangle( "if (a) return c; if (b) return d;", "if (a) return c;\nif (b) return d;\n");
    expectPrintedMangle( "if (a) return c; if (b) return c;", "if (a || b) return c;\n");
    expectPrintedMangle( "if (a) return c; if (b) return;", "if (a) return c;\nif (b) return;\n");
    expectPrintedMangle( "if (a) return; if (b) return c;", "if (a) return;\nif (b) return c;\n");
    expectPrintedMangle( "if (a) return; if (b) return;", "if (a || b) return;\n");
    expectPrintedMangle( "if (a) throw c; if (b) throw d;", "if (a) throw c;\nif (b) throw d;\n");
    expectPrintedMangle( "if (a) throw c; if (b) throw c;", "if (a || b) throw c;\n");
    expectPrintedMangle( "while (x) { if (a) break; if (b) break; }", "for (; x && !(a || b); )\n  ;\n");
    expectPrintedMangle( "while (x) { if (a) continue; if (b) continue; }", "for (; x; )\n  a || b;\n");
    expectPrintedMangle( "while (x) { debugger; if (a) break; if (b) break; }", "for (; x; ) {\n  debugger;\n  if (a || b) break;\n}\n");
    expectPrintedMangle( "while (x) { debugger; if (a) continue; if (b) continue; }", "for (; x; ) {\n  debugger;\n  a || b;\n}\n");
    expectPrintedMangle( "x: while (x) y: while (y) { if (a) break x; if (b) break y; }",
        "x: for (; x; ) y: for (; y; ) {\n  if (a) break x;\n  if (b) break y;\n}\n");
    expectPrintedMangle( "x: while (x) y: while (y) { if (a) continue x; if (b) continue y; }",
        "x: for (; x; ) y: for (; y; ) {\n  if (a) continue x;\n  if (b) continue y;\n}\n");
    expectPrintedMangle( "x: while (x) y: while (y) { if (a) break x; if (b) break x; }",
        "x: for (; x; ) for (; y; )\n  if (a || b) break x;\n");
    expectPrintedMangle( "x: while (x) y: while (y) { if (a) continue x; if (b) continue x; }",
        "x: for (; x; ) for (; y; )\n  if (a || b) continue x;\n");
    expectPrintedMangle( "x: while (x) y: while (y) { if (a) break y; if (b) break y; }",
        "for (; x; ) y: for (; y; )\n  if (a || b) break y;\n");
    expectPrintedMangle( "x: while (x) y: while (y) { if (a) continue y; if (b) continue y; }",
        "for (; x; ) y: for (; y; )\n  if (a || b) continue y;\n");
    expectPrintedNormalAndMangle( "if (x ? y : 0) foo()", "if (x ? y : 0) foo();\n", "x && y && foo();\n");
    expectPrintedNormalAndMangle( "if (x ? y : 1) foo()", "if (x ? y : 1) foo();\n", "(!x || y) && foo();\n");
    expectPrintedNormalAndMangle( "if (x ? 0 : y) foo()", "if (x ? 0 : y) foo();\n", "!x && y && foo();\n");
    expectPrintedNormalAndMangle( "if (x ? 1 : y) foo()", "if (x ? 1 : y) foo();\n", "(x || y) && foo();\n");
    expectPrintedNormalAndMangle( "if (x ? y : 0) ; else foo()", "if (x ? y : 0) ;\nelse foo();\n", "x && y || foo();\n");
    expectPrintedNormalAndMangle( "if (x ? y : 1) ; else foo()", "if (x ? y : 1) ;\nelse foo();\n", "!x || y || foo();\n");
    expectPrintedNormalAndMangle( "if (x ? 0 : y) ; else foo()", "if (x ? 0 : y) ;\nelse foo();\n", "!x && y || foo();\n");
    expectPrintedNormalAndMangle( "if (x ? 1 : y) ; else foo()", "if (x ? 1 : y) ;\nelse foo();\n", "x || y || foo();\n");
    expectPrintedNormalAndMangle( "(x ? y : 0) && foo();", "(x ? y : 0) && foo();\n", "x && y && foo();\n");
    expectPrintedNormalAndMangle( "(x ? y : 1) && foo();", "(x ? y : 1) && foo();\n", "(!x || y) && foo();\n");
    expectPrintedNormalAndMangle( "(x ? 0 : y) && foo();", "(x ? 0 : y) && foo();\n", "!x && y && foo();\n");
    expectPrintedNormalAndMangle( "(x ? 1 : y) && foo();", "(x ? 1 : y) && foo();\n", "(x || y) && foo();\n");
    expectPrintedNormalAndMangle( "(x ? y : 0) || foo();", "(x ? y : 0) || foo();\n", "x && y || foo();\n");
    expectPrintedNormalAndMangle( "(x ? y : 1) || foo();", "(x ? y : 1) || foo();\n", "!x || y || foo();\n");
    expectPrintedNormalAndMangle( "(x ? 0 : y) || foo();", "(x ? 0 : y) || foo();\n", "!x && y || foo();\n");
    expectPrintedNormalAndMangle( "(x ? 1 : y) || foo();", "(x ? 1 : y) || foo();\n", "x || y || foo();\n");
    expectPrintedNormalAndMangle( "if (!!a || !!b) throw 0", "if (!!a || !!b) throw 0;\n", "if (a || b) throw 0;\n");
    expectPrintedNormalAndMangle( "if (!!a && !!b) throw 0", "if (!!a && !!b) throw 0;\n", "if (a && b) throw 0;\n");
    expectPrintedNormalAndMangle( "if (!!a ? !!b : !!c) throw 0", "if (!!a ? !!b : !!c) throw 0;\n", "if (a ? b : c) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a + b) !== 0) throw 0", "if (a + b !== 0) throw 0;\n", "if (a + b !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a | b) !== 0) throw 0", "if ((a | b) !== 0) throw 0;\n", "if ((a | b) !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a & b) !== 0) throw 0", "if ((a & b) !== 0) throw 0;\n", "if ((a & b) !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a ^ b) !== 0) throw 0", "if ((a ^ b) !== 0) throw 0;\n", "if ((a ^ b) !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a << b) !== 0) throw 0", "if (a << b !== 0) throw 0;\n", "if (a << b !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a >> b) !== 0) throw 0", "if (a >> b !== 0) throw 0;\n", "if (a >> b !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a >>> b) !== 0) throw 0", "if (a >>> b !== 0) throw 0;\n", "if (a >>> b) throw 0;\n");
    expectPrintedNormalAndMangle( "if (+a !== 0) throw 0", "if (+a !== 0) throw 0;\n", "if (+a != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (~a !== 0) throw 0", "if (~a !== 0) throw 0;\n", "if (~a !== 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a + b)) throw 0", "if (0 != a + b) throw 0;\n", "if (a + b != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a | b)) throw 0", "if (0 != (a | b)) throw 0;\n", "if ((a | b) != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a & b)) throw 0", "if (0 != (a & b)) throw 0;\n", "if ((a & b) != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a ^ b)) throw 0", "if (0 != (a ^ b)) throw 0;\n", "if ((a ^ b) != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a << b)) throw 0", "if (0 != a << b) throw 0;\n", "if (a << b != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a >> b)) throw 0", "if (0 != a >> b) throw 0;\n", "if (a >> b != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != (a >>> b)) throw 0", "if (0 != a >>> b) throw 0;\n", "if (a >>> b) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != +a) throw 0", "if (0 != +a) throw 0;\n", "if (+a != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 != ~a) throw 0", "if (0 != ~a) throw 0;\n", "if (~a != 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a + b) === 0) throw 0", "if (a + b === 0) throw 0;\n", "if (a + b === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a | b) === 0) throw 0", "if ((a | b) === 0) throw 0;\n", "if ((a | b) === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a & b) === 0) throw 0", "if ((a & b) === 0) throw 0;\n", "if ((a & b) === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a ^ b) === 0) throw 0", "if ((a ^ b) === 0) throw 0;\n", "if ((a ^ b) === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a << b) === 0) throw 0", "if (a << b === 0) throw 0;\n", "if (a << b === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a >> b) === 0) throw 0", "if (a >> b === 0) throw 0;\n", "if (a >> b === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if ((a >>> b) === 0) throw 0", "if (a >>> b === 0) throw 0;\n", "if (!(a >>> b)) throw 0;\n");
    expectPrintedNormalAndMangle( "if (+a === 0) throw 0", "if (+a === 0) throw 0;\n", "if (+a == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (~a === 0) throw 0", "if (~a === 0) throw 0;\n", "if (~a === 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a + b)) throw 0", "if (0 == a + b) throw 0;\n", "if (a + b == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a | b)) throw 0", "if (0 == (a | b)) throw 0;\n", "if ((a | b) == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a & b)) throw 0", "if (0 == (a & b)) throw 0;\n", "if ((a & b) == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a ^ b)) throw 0", "if (0 == (a ^ b)) throw 0;\n", "if ((a ^ b) == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a << b)) throw 0", "if (0 == a << b) throw 0;\n", "if (a << b == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a >> b)) throw 0", "if (0 == a >> b) throw 0;\n", "if (a >> b == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == (a >>> b)) throw 0", "if (0 == a >>> b) throw 0;\n", "if (!(a >>> b)) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == +a) throw 0", "if (0 == +a) throw 0;\n", "if (+a == 0) throw 0;\n");
    expectPrintedNormalAndMangle( "if (0 == ~a) throw 0", "if (0 == ~a) throw 0;\n", "if (~a == 0) throw 0;\n");
}

TEST(JsParser, TestMangleWrapToAvoidAmbiguousElse) {
    expectPrintedMangle( "if (a) { if (b) return c } else return d", "if (a) {\n  if (b) return c;\n} else return d;\n");
    expectPrintedMangle( "if (a) while (1) { if (b) return c } else return d", "if (a) {\n  for (; ; )\n    if (b) return c;\n} else return d;\n");
    expectPrintedMangle( "if (a) for (;;) { if (b) return c } else return d", "if (a) {\n  for (; ; )\n    if (b) return c;\n} else return d;\n");
    expectPrintedMangle( "if (a) for (x in y) { if (b) return c } else return d", "if (a) {\n  for (x in y)\n    if (b) return c;\n} else return d;\n");
    expectPrintedMangle( "if (a) for (x of y) { if (b) return c } else return d", "if (a) {\n  for (x of y)\n    if (b) return c;\n} else return d;\n");
    expectPrintedMangle( "if (a) with (x) { if (b) return c } else return d", "if (a) {\n  with (x)\n    if (b) return c;\n} else return d;\n");
    expectPrintedMangle( "if (a) x: { if (b) break x } else return c", "if (a) {\n  x:\n    if (b) break x;\n} else return c;\n");
}

TEST(JsParser, TestMangleOptionalChain) {
    expectPrintedMangle( "let a; return a != null ? a.b : undefined", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "let a; return a != null ? a[b] : undefined", "let a;\nreturn a?.[b];\n");
    expectPrintedMangle( "let a; return a != null ? a(b) : undefined", "let a;\nreturn a?.(b);\n");
    expectPrintedMangle( "let a; return a == null ? undefined : a.b", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "let a; return a == null ? undefined : a[b]", "let a;\nreturn a?.[b];\n");
    expectPrintedMangle( "let a; return a == null ? undefined : a(b)", "let a;\nreturn a?.(b);\n");
    expectPrintedMangle( "let a; return null != a ? a.b : undefined", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "let a; return null != a ? a[b] : undefined", "let a;\nreturn a?.[b];\n");
    expectPrintedMangle( "let a; return null != a ? a(b) : undefined", "let a;\nreturn a?.(b);\n");
    expectPrintedMangle( "let a; return null == a ? undefined : a.b", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "let a; return null == a ? undefined : a[b]", "let a;\nreturn a?.[b];\n");
    expectPrintedMangle( "let a; return null == a ? undefined : a(b)", "let a;\nreturn a?.(b);\n");
    expectPrintedMangle( "return a != null ? a.b : undefined", "return a != null ? a.b : void 0;\n");
    expectPrintedMangle( "let a; return a != null ? a.b : null", "let a;\nreturn a != null ? a.b : null;\n");
    expectPrintedMangle( "let a; return a != null ? b.a : undefined", "let a;\nreturn a != null ? b.a : void 0;\n");
    expectPrintedMangle( "let a; return a != 0 ? a.b : undefined", "let a;\nreturn a != 0 ? a.b : void 0;\n");
    expectPrintedMangle( "let a; return a !== null ? a.b : undefined", "let a;\nreturn a !== null ? a.b : void 0;\n");
    expectPrintedMangle( "let a; return a != undefined ? a.b : undefined", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "let a; return a != null ? a?.b : undefined", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "let a; return a != null ? a.b.c[d](e) : undefined", "let a;\nreturn a?.b.c[d](e);\n");
    expectPrintedMangle( "let a; return a != null ? a?.b.c[d](e) : undefined", "let a;\nreturn a?.b.c[d](e);\n");
    expectPrintedMangle( "let a; return a != null ? a.b.c?.[d](e) : undefined", "let a;\nreturn a?.b.c?.[d](e);\n");
    expectPrintedMangle( "let a; return a != null ? a?.b.c?.[d](e) : undefined", "let a;\nreturn a?.b.c?.[d](e);\n");
    expectPrintedMangleTarget( 2019, "let a; return a != null ? a.b : undefined", "let a;\nreturn a != null ? a.b : void 0;\n");
    expectPrintedMangleTarget( 2020, "let a; return a != null ? a.b : undefined", "let a;\nreturn a?.b;\n");
    expectPrintedMangle( "a != null && a.b()", "a?.b();\n");
    expectPrintedMangle( "a == null || a.b()", "a?.b();\n");
    expectPrintedMangle( "null != a && a.b()", "a?.b();\n");
    expectPrintedMangle( "null == a || a.b()", "a?.b();\n");
    expectPrintedMangle( "a == null && a.b()", "a == null && a.b();\n");
    expectPrintedMangle( "a != null || a.b()", "a != null || a.b();\n");
    expectPrintedMangle( "null == a && a.b()", "a == null && a.b();\n");
    expectPrintedMangle( "null != a || a.b()", "a != null || a.b();\n");
    expectPrintedMangle( "x = a != null && a.b()", "x = a != null && a.b();\n");
    expectPrintedMangle( "x = a == null || a.b()", "x = a == null || a.b();\n");
    expectPrintedMangle( "if (a != null) a.b()", "a?.b();\n");
    expectPrintedMangle( "if (a == null) ; else a.b()", "a?.b();\n");
    expectPrintedMangle( "if (a == null) a.b()", "a == null && a.b();\n");
    expectPrintedMangle( "if (a != null) ; else a.b()", "a != null || a.b();\n");
}

TEST(JsParser, TestMangleNullOrUndefinedWithSideEffects) {
    expectPrintedNormalAndMangle( "x(y ?? 1)", "x(y ?? 1);\n", "x(y ?? 1);\n");
    expectPrintedNormalAndMangle( "x(y.z ?? 1)", "x(y.z ?? 1);\n", "x(y.z ?? 1);\n");
    expectPrintedNormalAndMangle( "x(y[z] ?? 1)", "x(y[z] ?? 1);\n", "x(y[z] ?? 1);\n");
    expectPrintedNormalAndMangle( "x(0 ?? 1)", "x(0);\n", "x(0);\n");
    expectPrintedNormalAndMangle( "x(0n ?? 1)", "x(0n);\n", "x(0n);\n");
    expectPrintedNormalAndMangle( "x('' ?? 1)", "x(\"\");\n", "x(\"\");\n");
    expectPrintedNormalAndMangle( "x(/./ ?? 1)", "x(/./);\n", "x(/./);\n");
    expectPrintedNormalAndMangle( "x({} ?? 1)", "x({});\n", "x({});\n");
    expectPrintedNormalAndMangle( "x((() => {}) ?? 1)", "x((() => {\n}));\n", "x((() => {\n}));\n");
    expectPrintedNormalAndMangle( "x(class {} ?? 1)", "x(class {\n});\n", "x(class {\n});\n");
    expectPrintedNormalAndMangle( "x(function() {} ?? 1)", "x(function() {\n});\n", "x(function() {\n});\n");
    expectPrintedNormalAndMangle( "x(null ?? 1)", "x(1);\n", "x(1);\n");
    expectPrintedNormalAndMangle( "x(undefined ?? 1)", "x(1);\n", "x(1);\n");
    expectPrintedNormalAndMangle( "x(void y ?? 1)", "x(void y ?? 1);\n", "x(void y ?? 1);\n");
    expectPrintedNormalAndMangle( "x(-y ?? 1)", "x(-y);\n", "x(-y);\n");
    expectPrintedNormalAndMangle( "x(+y ?? 1)", "x(+y);\n", "x(+y);\n");
    expectPrintedNormalAndMangle( "x(!y ?? 1)", "x(!y);\n", "x(!y);\n");
    expectPrintedNormalAndMangle( "x(~y ?? 1)", "x(~y);\n", "x(~y);\n");
    expectPrintedNormalAndMangle( "x(--y ?? 1)", "x(--y);\n", "x(--y);\n");
    expectPrintedNormalAndMangle( "x(++y ?? 1)", "x(++y);\n", "x(++y);\n");
    expectPrintedNormalAndMangle( "x(y-- ?? 1)", "x(y--);\n", "x(y--);\n");
    expectPrintedNormalAndMangle( "x(y++ ?? 1)", "x(y++);\n", "x(y++);\n");
    expectPrintedNormalAndMangle( "x(delete y ?? 1)", "x(delete y);\n", "x(delete y);\n");
    expectPrintedNormalAndMangle( "x(typeof y ?? 1)", "x(typeof y);\n", "x(typeof y);\n");
    expectPrintedNormalAndMangle( "x((y, 0) ?? 1)", "x((y, 0));\n", "x((y, 0));\n");
    expectPrintedNormalAndMangle( "x((y, !z) ?? 1)", "x((y, !z));\n", "x((y, !z));\n");
    expectPrintedNormalAndMangle( "x((y, null) ?? 1)", "x((y, null) ?? 1);\n", "x((y, null ?? 1));\n");
    expectPrintedNormalAndMangle( "x((y, void z) ?? 1)", "x((y, void z) ?? 1);\n", "x((y, void z ?? 1));\n");
    expectPrintedNormalAndMangle( "x((y + z) ?? 1)", "x(y + z);\n", "x(y + z);\n");
    expectPrintedNormalAndMangle( "x((y - z) ?? 1)", "x(y - z);\n", "x(y - z);\n");
    expectPrintedNormalAndMangle( "x((y * z) ?? 1)", "x(y * z);\n", "x(y * z);\n");
    expectPrintedNormalAndMangle( "x((y / z) ?? 1)", "x(y / z);\n", "x(y / z);\n");
    expectPrintedNormalAndMangle( "x((y % z) ?? 1)", "x(y % z);\n", "x(y % z);\n");
    expectPrintedNormalAndMangle( "x((y ** z) ?? 1)", "x(y ** z);\n", "x(y ** z);\n");
    expectPrintedNormalAndMangle( "x((y << z) ?? 1)", "x(y << z);\n", "x(y << z);\n");
    expectPrintedNormalAndMangle( "x((y >> z) ?? 1)", "x(y >> z);\n", "x(y >> z);\n");
    expectPrintedNormalAndMangle( "x((y >>> z) ?? 1)", "x(y >>> z);\n", "x(y >>> z);\n");
    expectPrintedNormalAndMangle( "x((y | z) ?? 1)", "x(y | z);\n", "x(y | z);\n");
    expectPrintedNormalAndMangle( "x((y & z) ?? 1)", "x(y & z);\n", "x(y & z);\n");
    expectPrintedNormalAndMangle( "x((y ^ z) ?? 1)", "x(y ^ z);\n", "x(y ^ z);\n");
    expectPrintedNormalAndMangle( "x((y < z) ?? 1)", "x(y < z);\n", "x(y < z);\n");
    expectPrintedNormalAndMangle( "x((y > z) ?? 1)", "x(y > z);\n", "x(y > z);\n");
    expectPrintedNormalAndMangle( "x((y <= z) ?? 1)", "x(y <= z);\n", "x(y <= z);\n");
    expectPrintedNormalAndMangle( "x((y >= z) ?? 1)", "x(y >= z);\n", "x(y >= z);\n");
    expectPrintedNormalAndMangle( "x((y == z) ?? 1)", "x(y == z);\n", "x(y == z);\n");
    expectPrintedNormalAndMangle( "x((y != z) ?? 1)", "x(y != z);\n", "x(y != z);\n");
    expectPrintedNormalAndMangle( "x((y === z) ?? 1)", "x(y === z);\n", "x(y === z);\n");
    expectPrintedNormalAndMangle( "x((y !== z) ?? 1)", "x(y !== z);\n", "x(y !== z);\n");
    expectPrintedNormalAndMangle( "x((y || z) ?? 1)", "x((y || z) ?? 1);\n", "x((y || z) ?? 1);\n");
    expectPrintedNormalAndMangle( "x((y && z) ?? 1)", "x((y && z) ?? 1);\n", "x((y && z) ?? 1);\n");
    expectPrintedNormalAndMangle( "x((y ?? z) ?? 1)", "x(y ?? z ?? 1);\n", "x(y ?? z ?? 1);\n");
}

TEST(JsParser, TestMangleBooleanWithSideEffects) {
    const std::vector<std::string> falsyNoSideEffects = {"false", "\"\"", "0", "0n", "null", "void 0"};
    const std::vector<std::string> truthyNoSideEffects = {"true", "\" \"", "1", "1n", "/./", "(() => {\n})", "function() {\n}", "[1, 2]", "{ a: 0 }"};
    for (const std::string& value : falsyNoSideEffects) {
        expectPrintedMangle( "y(x && "+value+")", "y(x && "+value+");\n");
        expectPrintedMangle( "y(x || "+value+")", "y(x || "+value+");\n");
        expectPrintedMangle( "y(!(x && "+value+"))", "y(!(x && false));\n");
        expectPrintedMangle( "y(!(x || "+value+"))", "y(!x);\n");
        expectPrintedMangle( "if (x && "+value+") y", "x;\n");
        expectPrintedMangle( "if (x || "+value+") y", "x && y;\n");
        expectPrintedMangle( "if (x && "+value+") y; else z", "x, z;\n");
        expectPrintedMangle( "if (x || "+value+") y; else z", "x ? y : z;\n");
        expectPrintedMangle( "y(x && "+value+" ? y : z)", "y((x, z));\n");
        expectPrintedMangle( "y(x || "+value+" ? y : z)", "y(x ? y : z);\n");
        expectPrintedMangle( "while ("+value+") x()", "for (; false; ) x();\n");
        expectPrintedMangle( "for (; "+value+"; ) x()", "for (; false; ) x();\n");
    }
    for (const std::string& value : truthyNoSideEffects) {
        expectPrintedMangle( "y(x && "+value+")", "y(x && "+value+");\n");
        expectPrintedMangle( "y(x || "+value+")", "y(x || "+value+");\n");
        expectPrintedMangle( "y(!(x && "+value+"))", "y(!x);\n");
        expectPrintedMangle( "y(!(x || "+value+"))", "y(!(x || true));\n");
        expectPrintedMangle( "if (x && "+value+") y", "x && y;\n");
        expectPrintedMangle( "if (x || "+value+") y", "x, y;\n");
        expectPrintedMangle( "if (x && "+value+") y; else z", "x ? y : z;\n");
        expectPrintedMangle( "if (x || "+value+") y; else z", "x, y;\n");
        expectPrintedMangle( "y(x && "+value+" ? y : z)", "y(x ? y : z);\n");
        expectPrintedMangle( "y(x || "+value+" ? y : z)", "y((x, y));\n");
        expectPrintedMangle( "while ("+value+") x()", "for (; ; ) x();\n");
        expectPrintedMangle( "for (; "+value+"; ) x()", "for (; ; ) x();\n");
    }
    const std::vector<std::string> falsyHasSideEffects = {"void foo()"};
    const std::vector<std::string> truthyHasSideEffects = {"typeof foo()", "[foo()]", "{ [foo()]: 0 }"};
    for (const std::string& value : falsyHasSideEffects) {
        expectPrintedMangle( "y(x && "+value+")", "y(x && "+value+");\n");
        expectPrintedMangle( "y(x || "+value+")", "y(x || "+value+");\n");
        expectPrintedMangle( "y(!(x && "+value+"))", "y(!(x && "+value+"));\n");
        expectPrintedMangle( "y(!(x || "+value+"))", "y(!(x || "+value+"));\n");
        expectPrintedMangle( "if (x || "+value+") y", "(x || "+value+") && y;\n");
        expectPrintedMangle( "if (x || "+value+") y; else z", "x || "+value+" ? y : z;\n");
        expectPrintedMangle( "y(x || "+value+" ? y : z)", "y(x || "+value+" ? y : z);\n");
        expectPrintedMangle( "while ("+value+") x()", "for (; "+value+"; ) x();\n");
        expectPrintedMangle( "for (; "+value+"; ) x()", "for (; "+value+"; ) x();\n");
    }
    for (const std::string& value : truthyHasSideEffects) {
        expectPrintedMangle( "y(x && "+value+")", "y(x && "+value+");\n");
        expectPrintedMangle( "y(x || "+value+")", "y(x || "+value+");\n");
        expectPrintedMangle( "y(!(x || "+value+"))", "y(!(x || "+value+"));\n");
        expectPrintedMangle( "y(!(x && "+value+"))", "y(!(x && "+value+"));\n");
        expectPrintedMangle( "if (x && "+value+") y", "x && "+value+" && y;\n");
        expectPrintedMangle( "if (x && "+value+") y; else z", "x && "+value+" ? y : z;\n");
        expectPrintedMangle( "y(x && "+value+" ? y : z)", "y(x && "+value+" ? y : z);\n");
        expectPrintedMangle( "while ("+value+") x()", "for (; "+value+"; ) x();\n");
        expectPrintedMangle( "for (; "+value+"; ) x()", "for (; "+value+"; ) x();\n");
    }
}

TEST(JsParser, TestMangleReturn) {
    expectPrintedMangle( "function foo() { x(); return; }", "function foo() {\n  x();\n}\n");
    expectPrintedMangle( "let foo = function() { x(); return; }", "let foo = function() {\n  x();\n};\n");
    expectPrintedMangle( "let foo = () => { x(); return; }", "let foo = () => {\n  x();\n};\n");
    expectPrintedMangle( "function foo() { x(); return y; }", "function foo() {\n  return x(), y;\n}\n");
    expectPrintedMangle( "let foo = function() { x(); return y; }", "let foo = function() {\n  return x(), y;\n};\n");
    expectPrintedMangle( "let foo = () => { x(); return y; }", "let foo = () => (x(), y);\n");
    // Don't trim a trailing top-level return because we may be compiling a partial module
    expectPrintedMangle( "x(); return;", "x();\nreturn;\n");
    expectPrintedMangle( "function foo() { a = b; if (a) return a; if (b) c = b; return c; }",
        "function foo() {\n  return a = b, a || (b && (c = b), c);\n}\n");
    expectPrintedMangle( "function foo() { a = b; if (a) return; if (b) c = b; return c; }",
        "function foo() {\n  if (a = b, !a)\n    return b && (c = b), c;\n}\n");
    expectPrintedMangle( "function foo() { if (!a) return b; return c; }", "function foo() {\n  return a ? c : b;\n}\n");
    expectPrintedMangle( "if (1) return a(); else return b()", "return a();\n");
    expectPrintedMangle( "if (0) return a(); else return b()", "return b();\n");
    expectPrintedMangle( "if (a) return b(); else return c()", "return a ? b() : c();\n");
    expectPrintedMangle( "if (!a) return b(); else return c()", "return a ? c() : b();\n");
    expectPrintedMangle( "if (!!a) return b(); else return c()", "return a ? b() : c();\n");
    expectPrintedMangle( "if (!!!a) return b(); else return c()", "return a ? c() : b();\n");
    expectPrintedMangle( "if (1) return a(); return b()", "return a();\n");
    expectPrintedMangle( "if (0) return a(); return b()", "return b();\n");
    expectPrintedMangle( "if (a) return b(); return c()", "return a ? b() : c();\n");
    expectPrintedMangle( "if (!a) return b(); return c()", "return a ? c() : b();\n");
    expectPrintedMangle( "if (!!a) return b(); return c()", "return a ? b() : c();\n");
    expectPrintedMangle( "if (!!!a) return b(); return c()", "return a ? c() : b();\n");
    expectPrintedMangle( "if (a) return b; else return c; return d;\n", "return a ? b : c;\n");
    // Optimize implicit return
    expectPrintedMangle( "function x() { if (y) return; z(); }", "function x() {\n  y || z();\n}\n");
    expectPrintedMangle( "function x() { if (y) return; else z(); w(); }", "function x() {\n  y || (z(), w());\n}\n");
    expectPrintedMangle( "function x() { t(); if (y) return; z(); }", "function x() {\n  t(), !y && z();\n}\n");
    expectPrintedMangle( "function x() { t(); if (y) return; else z(); w(); }", "function x() {\n  t(), !y && (z(), w());\n}\n");
    expectPrintedMangle( "function x() { debugger; if (y) return; z(); }", "function x() {\n  debugger;\n  y || z();\n}\n");
    expectPrintedMangle( "function x() { debugger; if (y) return; else z(); w(); }", "function x() {\n  debugger;\n  y || (z(), w());\n}\n");
    expectPrintedMangle( "function x() { if (y) { if (z) return; } }",
        "function x() {\n  y && z;\n}\n");
    expectPrintedMangle( "function x() { if (y) { if (z) return; w(); } }",
        "function x() {\n  if (y) {\n    if (z) return;\n    w();\n  }\n}\n");
    expectPrintedMangle( "function foo(x) { if (!x.y) {} else return x }", "function foo(x) {\n  if (x.y)\n    return x;\n}\n");
    expectPrintedMangle( "function foo(x) { if (!x.y) return undefined; return x }", "function foo(x) {\n  if (x.y)\n    return x;\n}\n");
    // Do not optimize implicit return for statements that care about scope
    expectPrintedMangle( "function x() { if (y) return; function y() {} }", "function x() {\n  if (y) return;\n  function y() {\n  }\n}\n");
    expectPrintedMangle( "function x() { if (y) return; let y }", "function x() {\n  if (y) return;\n  let y;\n}\n");
    expectPrintedMangle( "function x() { if (y) return; var y }", "function x() {\n  if (!y)\n    var y;\n}\n");
}

TEST(JsParser, TestMangleThrow) {
    expectPrintedNormalAndMangle(
        "function foo() { a = b; if (a) throw a; if (b) c = b; throw c; }",
        "function foo() {\n  a = b;\n  if (a) throw a;\n  if (b) c = b;\n  throw c;\n}\n",
        "function foo() {\n  throw a = b, a || (b && (c = b), c);\n}\n");
    expectPrintedNormalAndMangle(
        "function foo() { if (!a) throw b; throw c; }",
        "function foo() {\n  if (!a) throw b;\n  throw c;\n}\n",
        "function foo() {\n  throw a ? c : b;\n}\n");
    expectPrintedNormalAndMangle( "if (1) throw a(); else throw b()", "if (1) throw a();\nelse throw b();\n", "throw a();\n");
    expectPrintedNormalAndMangle( "if (0) throw a(); else throw b()", "if (0) throw a();\nelse throw b();\n", "throw b();\n");
    expectPrintedNormalAndMangle( "if (a) throw b(); else throw c()", "if (a) throw b();\nelse throw c();\n", "throw a ? b() : c();\n");
    expectPrintedNormalAndMangle( "if (!a) throw b(); else throw c()", "if (!a) throw b();\nelse throw c();\n", "throw a ? c() : b();\n");
    expectPrintedNormalAndMangle( "if (!!a) throw b(); else throw c()", "if (!!a) throw b();\nelse throw c();\n", "throw a ? b() : c();\n");
    expectPrintedNormalAndMangle( "if (!!!a) throw b(); else throw c()", "if (!!!a) throw b();\nelse throw c();\n", "throw a ? c() : b();\n");
    expectPrintedNormalAndMangle( "if (1) throw a(); throw b()", "if (1) throw a();\nthrow b();\n", "throw a();\n");
    expectPrintedNormalAndMangle( "if (0) throw a(); throw b()", "if (0) throw a();\nthrow b();\n", "throw b();\n");
    expectPrintedNormalAndMangle( "if (a) throw b(); throw c()", "if (a) throw b();\nthrow c();\n", "throw a ? b() : c();\n");
    expectPrintedNormalAndMangle( "if (!a) throw b(); throw c()", "if (!a) throw b();\nthrow c();\n", "throw a ? c() : b();\n");
    expectPrintedNormalAndMangle( "if (!!a) throw b(); throw c()", "if (!!a) throw b();\nthrow c();\n", "throw a ? b() : c();\n");
    expectPrintedNormalAndMangle( "if (!!!a) throw b(); throw c()", "if (!!!a) throw b();\nthrow c();\n", "throw a ? c() : b();\n");
}

TEST(JsParser, TestMangleInitializer) {
    expectPrintedNormalAndMangle( "const a = undefined", "const a = void 0;\n", "const a = void 0;\n");
    expectPrintedNormalAndMangle( "let a = undefined", "let a = void 0;\n", "let a;\n");
    expectPrintedNormalAndMangle( "let {} = undefined", "let {} = void 0;\n", "let {} = void 0;\n");
    expectPrintedNormalAndMangle( "let [] = undefined", "let [] = void 0;\n", "let [] = void 0;\n");
    expectPrintedNormalAndMangle( "var a = undefined", "var a = void 0;\n", "var a = void 0;\n");
    expectPrintedNormalAndMangle( "var {} = undefined", "var {} = void 0;\n", "var {} = void 0;\n");
    expectPrintedNormalAndMangle( "var [] = undefined", "var [] = void 0;\n", "var [] = void 0;\n");
}

TEST(JsParser, TestMangleCall) {
    expectPrintedNormalAndMangle( "x = foo(1, ...[], 2)", "x = foo(1, ...[], 2);\n", "x = foo(1, 2);\n");
    expectPrintedNormalAndMangle( "x = foo(1, ...2, 3)", "x = foo(1, ...2, 3);\n", "x = foo(1, ...2, 3);\n");
    expectPrintedNormalAndMangle( "x = foo(1, ...[2], 3)", "x = foo(1, ...[2], 3);\n", "x = foo(1, 2, 3);\n");
    expectPrintedNormalAndMangle( "x = foo(1, ...[2, 3], 4)", "x = foo(1, ...[2, 3], 4);\n", "x = foo(1, 2, 3, 4);\n");
    expectPrintedNormalAndMangle( "x = foo(1, ...[2, ...y, 3], 4)", "x = foo(1, ...[2, ...y, 3], 4);\n", "x = foo(1, 2, ...y, 3, 4);\n");
    expectPrintedNormalAndMangle( "x = foo(1, ...{a, b}, 4)", "x = foo(1, ...{ a, b }, 4);\n", "x = foo(1, ...{ a, b }, 4);\n");
    // Holes must become undefined
    expectPrintedNormalAndMangle( "x = foo(1, ...[,2,,], 3)", "x = foo(1, ...[, 2, ,], 3);\n", "x = foo(1, void 0, 2, void 0, 3);\n");
}

TEST(JsParser, TestMangleNew) {
    expectPrintedNormalAndMangle( "x = new foo(1, ...[], 2)", "x = new foo(1, ...[], 2);\n", "x = new foo(1, 2);\n");
    expectPrintedNormalAndMangle( "x = new foo(1, ...2, 3)", "x = new foo(1, ...2, 3);\n", "x = new foo(1, ...2, 3);\n");
    expectPrintedNormalAndMangle( "x = new foo(1, ...[2], 3)", "x = new foo(1, ...[2], 3);\n", "x = new foo(1, 2, 3);\n");
    expectPrintedNormalAndMangle( "x = new foo(1, ...[2, 3], 4)", "x = new foo(1, ...[2, 3], 4);\n", "x = new foo(1, 2, 3, 4);\n");
    expectPrintedNormalAndMangle( "x = new foo(1, ...[2, ...y, 3], 4)", "x = new foo(1, ...[2, ...y, 3], 4);\n", "x = new foo(1, 2, ...y, 3, 4);\n");
    expectPrintedNormalAndMangle( "x = new foo(1, ...{a, b}, 4)", "x = new foo(1, ...{ a, b }, 4);\n", "x = new foo(1, ...{ a, b }, 4);\n");
    // Holes must become undefined
    expectPrintedNormalAndMangle( "x = new foo(1, ...[,2,,], 3)", "x = new foo(1, ...[, 2, ,], 3);\n", "x = new foo(1, void 0, 2, void 0, 3);\n");
}

TEST(JsParser, TestMangleArray) {
    expectPrintedNormalAndMangle( "x = [1, ...[], 2]", "x = [1, ...[], 2];\n", "x = [1, 2];\n");
    expectPrintedNormalAndMangle( "x = [1, ...2, 3]", "x = [1, ...2, 3];\n", "x = [1, ...2, 3];\n");
    expectPrintedNormalAndMangle( "x = [1, ...[2], 3]", "x = [1, ...[2], 3];\n", "x = [1, 2, 3];\n");
    expectPrintedNormalAndMangle( "x = [1, ...[2, 3], 4]", "x = [1, ...[2, 3], 4];\n", "x = [1, 2, 3, 4];\n");
    expectPrintedNormalAndMangle( "x = [1, ...[2, ...y, 3], 4]", "x = [1, ...[2, ...y, 3], 4];\n", "x = [1, 2, ...y, 3, 4];\n");
    expectPrintedNormalAndMangle( "x = [1, ...{a, b}, 4]", "x = [1, ...{ a, b }, 4];\n", "x = [1, ...{ a, b }, 4];\n");
    // Holes must become undefined, which is different than a hole
    expectPrintedNormalAndMangle( "x = [1, ...[,2,,], 3]", "x = [1, ...[, 2, ,], 3];\n", "x = [1, void 0, 2, void 0, 3];\n");
}

TEST(JsParser, TestMangleObject) {
    expectPrintedNormalAndMangle( "x = {['y']: z}", "x = { [\"y\"]: z };\n", "x = { y: z };\n");
    expectPrintedNormalAndMangle( "x = {['y']() {}}", "x = { [\"y\"]() {\n} };\n", "x = { y() {\n} };\n");
    expectPrintedNormalAndMangle( "x = {get ['y']() {}}", "x = { get [\"y\"]() {\n} };\n", "x = { get y() {\n} };\n");
    expectPrintedNormalAndMangle( "x = {set ['y'](z) {}}", "x = { set [\"y\"](z) {\n} };\n", "x = { set y(z) {\n} };\n");
    expectPrintedNormalAndMangle( "x = {async ['y']() {}}", "x = { async [\"y\"]() {\n} };\n", "x = { async y() {\n} };\n");
    expectPrintedNormalAndMangle( "({['y']: z} = x)", "({ [\"y\"]: z } = x);\n", "({ y: z } = x);\n");
    expectPrintedNormalAndMangle( "x = {a, ...{}, b}", "x = { a, ...{}, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...b, c}", "x = { a, ...b, c };\n", "x = { a, ...b, c };\n");
    expectPrintedNormalAndMangle( "x = {a, ...{b}, c}", "x = { a, ...{ b }, c };\n", "x = { a, b, c };\n");
    expectPrintedNormalAndMangle( "x = {a, ...{b() {}}, c}", "x = { a, ...{ b() {\n} }, c };\n", "x = { a, b() {\n}, c };\n");
    expectPrintedNormalAndMangle( "x = {a, ...{b, c}, d}", "x = { a, ...{ b, c }, d };\n", "x = { a, b, c, d };\n");
    expectPrintedNormalAndMangle( "x = {a, ...{b, ...y, c}, d}", "x = { a, ...{ b, ...y, c }, d };\n", "x = { a, b, ...y, c, d };\n");
    expectPrintedNormalAndMangle( "x = {a, ...[b, c], d}", "x = { a, ...[b, c], d };\n", "x = { a, ...[b, c], d };\n");
    // Computed properties should be ok
    expectPrintedNormalAndMangle( "x = {a, ...{[b]: c}, d}", "x = { a, ...{ [b]: c }, d };\n", "x = { a, [b]: c, d };\n");
    expectPrintedNormalAndMangle( "x = {a, ...{[b]() {}}, c}", "x = { a, ...{ [b]() {\n} }, c };\n", "x = { a, [b]() {\n}, c };\n");
    // Getters and setters are not supported
    expectPrintedNormalAndMangle(
        "x = {a, ...{b, get c() { return y++ }, d}, e}",
        "x = { a, ...{ b, get c() {\n  return y++;\n}, d }, e };\n",
        "x = { a, b, ...{ get c() {\n  return y++;\n}, d }, e };\n");
    expectPrintedNormalAndMangle(
        "x = {a, ...{b, set c(_) { throw _ }, d}, e}",
        "x = { a, ...{ b, set c(_) {\n  throw _;\n}, d }, e };\n",
        "x = { a, b, ...{ set c(_) {\n  throw _;\n}, d }, e };\n");
    // "__proto__" is not supported
    expectPrintedNormalAndMangle(
        "x = {a, ...{b, __proto__: c, d}, e}",
        "x = { a, ...{ b, __proto__: c, d }, e };\n",
        "x = { a, b, ...{ __proto__: c, d }, e };\n");
    expectPrintedNormalAndMangle(
        "x = {a, ...{b, ['__proto__']: c, d}, e}",
        "x = { a, ...{ b, [\"__proto__\"]: c, d }, e };\n",
        "x = { a, b, [\"__proto__\"]: c, d, e };\n");
    expectPrintedNormalAndMangle(
        "x = {a, ...{b, __proto__() {}, c}, d}",
        "x = { a, ...{ b, __proto__() {\n}, c }, d };\n",
        "x = { a, b, __proto__() {\n}, c, d };\n");
    // Spread is ignored for certain values
    expectPrintedNormalAndMangle( "x = {a, ...true, b}", "x = { a, ...true, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...null, b}", "x = { a, ...null, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...void 0, b}", "x = { a, ...void 0, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...123, b}", "x = { a, ...123, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...123n, b}", "x = { a, ...123n, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, .../x/, b}", "x = { a, .../x/, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...function(){}, b}", "x = { a, ...function() {\n}, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...()=>{}, b}", "x = { a, ...() => {\n}, b };\n", "x = { a, b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...'123', b}", "x = { a, ...\"123\", b };\n", "x = { a, ...\"123\", b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...[1, 2, 3], b}", "x = { a, ...[1, 2, 3], b };\n", "x = { a, ...[1, 2, 3], b };\n");
    expectPrintedNormalAndMangle( "x = {a, ...(()=>{})(), b}", "x = { a, .../* @__PURE__ */ (() => {\n})(), b };\n", "x = { a, b };\n");
    // Check simple cases of object simplification (advanced cases are checked in end-to-end tests)
    expectPrintedNormalAndMangle( "x = {['y']: z}.y", "x = { [\"y\"]: z }.y;\n", "x = { y: z }.y;\n");
    expectPrintedNormalAndMangle( "x = {['y']: z}.y; var z", "x = { [\"y\"]: z }.y;\nvar z;\n", "x = z;\nvar z;\n");
    expectPrintedNormalAndMangle( "x = {foo: foo(), y: 1}.y", "x = { foo: foo(), y: 1 }.y;\n", "x = { foo: foo(), y: 1 }.y;\n");
    expectPrintedNormalAndMangle( "x = {foo: /* @__PURE__ */ foo(), y: 1}.y", "x = { foo: /* @__PURE__ */ foo(), y: 1 }.y;\n", "x = 1;\n");
    expectPrintedNormalAndMangle( "x = {__proto__: null}.y", "x = { __proto__: null }.y;\n", "x = void 0;\n");
    expectPrintedNormalAndMangle( "x = {__proto__: null, y: 1}.y", "x = { __proto__: null, y: 1 }.y;\n", "x = 1;\n");
    expectPrintedNormalAndMangle( "x = {__proto__: null}.__proto__", "x = { __proto__: null }.__proto__;\n", "x = void 0;\n");
    expectPrintedNormalAndMangle( "x = {['__proto__']: null}.y", "x = { [\"__proto__\"]: null }.y;\n", "x = { [\"__proto__\"]: null }.y;\n");
    expectPrintedNormalAndMangle( "x = {['__proto__']: null, y: 1}.y", "x = { [\"__proto__\"]: null, y: 1 }.y;\n", "x = { [\"__proto__\"]: null, y: 1 }.y;\n");
    expectPrintedNormalAndMangle( "x = {['__proto__']: null}.__proto__", "x = { [\"__proto__\"]: null }.__proto__;\n", "x = { [\"__proto__\"]: null }.__proto__;\n");
    expectPrintedNormalAndMangle( "x = {y: 1}?.y", "x = { y: 1 }?.y;\n", "x = 1;\n");
    expectPrintedNormalAndMangle( "x = {y: 1}?.['y']", "x = { y: 1 }?.[\"y\"];\n", "x = 1;\n");
    expectPrintedNormalAndMangle( "x = {y: {z: 1}}?.y.z", "x = { y: { z: 1 } }?.y.z;\n", "x = 1;\n");
    expectPrintedNormalAndMangle( "x = {y: {z: 1}}?.y?.z", "x = { y: { z: 1 } }?.y?.z;\n", "x = { z: 1 }?.z;\n");
    expectPrintedNormalAndMangle( "x = {y() {}}?.y()", "x = { y() {\n} }?.y();\n", "x = { y() {\n} }.y();\n");
    // Don't change the value of "this" for tagged template literals if the original syntax had a value for "this"
    expectPrintedNormalAndMangle( "function f(x) { return {x}.x`` }", "function f(x) {\n  return { x }.x``;\n}\n", "function f(x) {\n  return { x }.x``;\n}\n");
    expectPrintedNormalAndMangle( "function f(x) { return (0, {x}.x)`` }", "function f(x) {\n  return (0, { x }.x)``;\n}\n", "function f(x) {\n  return x``;\n}\n");
}

TEST(JsParser, TestMangleObjectJSX) {
    expectPrintedJSX( "x = <foo bar {...{}} />", "x = <foo bar {...{}} />;\n", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true, ...{} });\n");
    expectPrintedJSX( "x = <foo bar {...null} />", "x = <foo bar {...null} />;\n", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true, ...null });\n");
    expectPrintedJSX( "x = <foo bar {...{bar}} />", "x = <foo bar {...{ bar }} />;\n", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true, ...{ bar } });\n");
    expectPrintedJSX( "x = <foo bar {...bar} />", "x = <foo bar {...bar} />;\n", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true, ...bar });\n");
    expectPrintedMangleJSX( "x = <foo bar {...{}} />", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true });\n");
    expectPrintedMangleJSX( "x = <foo bar {...null} />", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true });\n");
    expectPrintedMangleJSX( "x = <foo bar {...{bar}} />", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true, bar });\n");
    expectPrintedMangleJSX( "x = <foo bar {...bar} />", "x = /* @__PURE__ */ React.createElement(\"foo\", { bar: true, ...bar });\n");
}

TEST(JsParser, TestMangleArrow) {
    expectPrintedNormalAndMangle( "var a = () => {}", "var a = () => {\n};\n", "var a = () => {\n};\n");
    expectPrintedNormalAndMangle( "var a = () => 123", "var a = () => 123;\n", "var a = () => 123;\n");
    expectPrintedNormalAndMangle( "var a = () => void 0", "var a = () => void 0;\n", "var a = () => {\n};\n");
    expectPrintedNormalAndMangle( "var a = () => undefined", "var a = () => void 0;\n", "var a = () => {\n};\n");
    expectPrintedNormalAndMangle( "var a = () => {return}", "var a = () => {\n  return;\n};\n", "var a = () => {\n};\n");
    expectPrintedNormalAndMangle( "var a = () => {return 123}", "var a = () => {\n  return 123;\n};\n", "var a = () => 123;\n");
    expectPrintedNormalAndMangle( "var a = () => {throw 123}", "var a = () => {\n  throw 123;\n};\n", "var a = () => {\n  throw 123;\n};\n");
}

TEST(JsParser, TestMangleIIFE) {
    expectPrintedNormalAndMangle( "var a = (() => {})()", "var a = /* @__PURE__ */ (() => {\n})();\n", "var a = void 0;\n");
    expectPrintedNormalAndMangle( "(() => a)()", "(() => a)();\n", "a;\n");
    expectPrintedNormalAndMangle( "(() => a)(...[])", "(() => a)(...[]);\n", "a;\n");
    expectPrintedNormalAndMangle( "(() => a())()", "(() => a())();\n", "a();\n");
    expectPrintedNormalAndMangle( "(() => { a() })()", "(() => {\n  a();\n})();\n", "a();\n");
    expectPrintedNormalAndMangle( "(() => { return a() })()", "(() => {\n  return a();\n})();\n", "a();\n");
    expectPrintedNormalAndMangle( "(() => {})()", "/* @__PURE__ */ (() => {\n})();\n", "");
    expectPrintedNormalAndMangle( "(() => { let b = a; b() })()", "(() => {\n  let b = a;\n  b();\n})();\n", "a();\n");
    expectPrintedNormalAndMangle( "(() => { let b = a; return b() })()", "(() => {\n  let b = a;\n  return b();\n})();\n", "a();\n");
    expectPrintedNormalAndMangle( "(async () => {})()", "(async () => {\n})();\n", "");
    expectPrintedNormalAndMangle( "(async () => { a() })()", "(async () => {\n  a();\n})();\n", "(async () => a())();\n");
    expectPrintedNormalAndMangle( "(async () => { let b = a; b() })()", "(async () => {\n  let b = a;\n  b();\n})();\n", "(async () => a())();\n");
    expectPrintedNormalAndMangle( "var a = (function() {})()", "var a = /* @__PURE__ */ (function() {\n})();\n", "var a = /* @__PURE__ */ (function() {\n})();\n");
    expectPrintedNormalAndMangle( "(function() {})()", "/* @__PURE__ */ (function() {\n})();\n", "");
    expectPrintedNormalAndMangle( "(function*() {})()", "(function* () {\n})();\n", "");
    expectPrintedNormalAndMangle( "(async function() {})()", "(async function() {\n})();\n", "");
    expectPrintedNormalAndMangle( "(function() { a() })()", "(function() {\n  a();\n})();\n", "(function() {\n  a();\n})();\n");
    expectPrintedNormalAndMangle( "(function*() { a() })()", "(function* () {\n  a();\n})();\n", "(function* () {\n  a();\n})();\n");
    expectPrintedNormalAndMangle( "(async function() { a() })()", "(async function() {\n  a();\n})();\n", "(async function() {\n  a();\n})();\n");
    expectPrintedNormalAndMangle( "(() => x)()", "(() => x)();\n", "x;\n");
    expectPrintedNormalAndMangle( "(() => { return x })()", "(() => {\n  return x;\n})();\n", "x;\n");
    expectPrintedNormalAndMangle( "(() => { x })()", "(() => {\n  x;\n})();\n", "x;\n");
    expectPrintedNormalAndMangle( "return (() => x)()", "return (() => x)();\n", "return x;\n");
    expectPrintedNormalAndMangle( "return (() => { return x })()", "return (() => {\n  return x;\n})();\n", "return x;\n");
    expectPrintedNormalAndMangle( "return (() => { x })()", "return (() => {\n  x;\n})();\n", "return void x;\n");
    expectPrintedNormalAndMangle( "/* @__PURE__ */ (() => x)()", "/* @__PURE__ */ (() => x)();\n", "");
    expectPrintedNormalAndMangle( "/* @__PURE__ */ (() => x)(y, z)", "/* @__PURE__ */ (() => x)(y, z);\n", "y, z;\n");
    expectPrintedNormalAndMangle( "let x = () => { let y = () => z(); y() }",
        "let x = () => {\n  let y = () => z();\n  y();\n};\n",
        "let x = () => {\n  z();\n};\n");
    expectPrintedNormalAndMangle( "let x = () => { let y = () => z(); return y() }",
        "let x = () => {\n  let y = () => z();\n  return y();\n};\n",
        "let x = () => z();\n");
    expectPrintedNormalAndMangle( "let x = () => { let y = () => { z() }; y() }",
        "let x = () => {\n  let y = () => {\n    z();\n  };\n  y();\n};\n",
        "let x = () => {\n  z();\n};\n");
    expectPrintedNormalAndMangle( "let x = () => { let y = () => { z() }; return y() }",
        "let x = () => {\n  let y = () => {\n    z();\n  };\n  return y();\n};\n",
        "let x = () => {\n  z();\n};\n");
    expectPrintedNormalAndMangle( "let x = () => { let y = () => { z() }; let x = y(); foo(x) }",
        "let x = () => {\n  let y = () => {\n    z();\n  };\n  let x = y();\n  foo(x);\n};\n",
        "let x = () => {\n  let x = void z();\n  foo(x);\n};\n");
}

TEST(JsParser, TestMangleTemplate) {
    expectPrintedNormalAndMangle( "_ = `a${x}b${y}c`", "_ = `a${x}b${y}c`;\n", "_ = `a${x}b${y}c`;\n");
    expectPrintedNormalAndMangle( "_ = `a${x}b${'y'}c`", "_ = `a${x}b${\"y\"}c`;\n", "_ = `a${x}byc`;\n");
    expectPrintedNormalAndMangle( "_ = `a${'x'}b${y}c`", "_ = `a${\"x\"}b${y}c`;\n", "_ = `axb${y}c`;\n");
    expectPrintedNormalAndMangle( "_ = `a${'x'}b${'y'}c`", "_ = `a${\"x\"}b${\"y\"}c`;\n", "_ = `axbyc`;\n");
    expectPrintedNormalAndMangle( "tag`a${x}b${y}c`", "tag`a${x}b${y}c`;\n", "tag`a${x}b${y}c`;\n");
    expectPrintedNormalAndMangle( "tag`a${x}b${'y'}c`", "tag`a${x}b${\"y\"}c`;\n", "tag`a${x}b${\"y\"}c`;\n");
    expectPrintedNormalAndMangle( "tag`a${'x'}b${y}c`", "tag`a${\"x\"}b${y}c`;\n", "tag`a${\"x\"}b${y}c`;\n");
    expectPrintedNormalAndMangle( "tag`a${'x'}b${'y'}c`", "tag`a${\"x\"}b${\"y\"}c`;\n", "tag`a${\"x\"}b${\"y\"}c`;\n");
    expectPrintedNormalAndMangle( "(1, x)``", "(1, x)``;\n", "x``;\n");
    expectPrintedNormalAndMangle( "(1, x.y)``", "(1, x.y)``;\n", "(0, x.y)``;\n");
    expectPrintedNormalAndMangle( "(1, x[y])``", "(1, x[y])``;\n", "(0, x[y])``;\n");
    expectPrintedNormalAndMangle( "(true && x)``", "x``;\n", "x``;\n");
    expectPrintedNormalAndMangle( "(true && x.y)``", "(0, x.y)``;\n", "(0, x.y)``;\n");
    expectPrintedNormalAndMangle( "(true && x[y])``", "(0, x[y])``;\n", "(0, x[y])``;\n");
    expectPrintedNormalAndMangle( "(false || x)``", "x``;\n", "x``;\n");
    expectPrintedNormalAndMangle( "(false || x.y)``", "(0, x.y)``;\n", "(0, x.y)``;\n");
    expectPrintedNormalAndMangle( "(false || x[y])``", "(0, x[y])``;\n", "(0, x[y])``;\n");
    expectPrintedNormalAndMangle( "(null ?? x)``", "x``;\n", "x``;\n");
    expectPrintedNormalAndMangle( "(null ?? x.y)``", "(0, x.y)``;\n", "(0, x.y)``;\n");
    expectPrintedNormalAndMangle( "(null ?? x[y])``", "(0, x[y])``;\n", "(0, x[y])``;\n");
    expectPrintedMangleTarget( 2015, "class Foo { #foo() { return this.#foo`` } }", "var _Foo_instances, foo_fn;\n"
        "class Foo {\n"
        "  constructor() {\n"
        "    __privateAdd(this, _Foo_instances);\n"
        "  }\n"
        "}\n"
        "_Foo_instances = new WeakSet(), foo_fn = function() {\n"
        "  return __privateMethod(this, _Foo_instances, foo_fn).bind(this)" "``" ";\n"
        "};\n"
        "");
    expectPrintedMangleTarget( 2015, "class Foo { #foo() { return (0, this.#foo)`` } }", "var _Foo_instances, foo_fn;\n"
        "class Foo {\n"
        "  constructor() {\n"
        "    __privateAdd(this, _Foo_instances);\n"
        "  }\n"
        "}\n"
        "_Foo_instances = new WeakSet(), foo_fn = function() {\n"
        "  return __privateMethod(this, _Foo_instances, foo_fn)" "``" ";\n"
        "};\n"
        "");
    expectPrintedNormalAndMangle(
        "function f(a) { let c = a.b; return c`` }",
        "function f(a) {\n  let c = a.b;\n  return c``;\n}\n",
        "function f(a) {\n  return (0, a.b)``;\n}\n");
    expectPrintedNormalAndMangle(
        "function f(a) { let c = a.b; return c`${x}` }",
        "function f(a) {\n  let c = a.b;\n  return c`${x}`;\n}\n",
        "function f(a) {\n  return (0, a.b)`${x}`;\n}\n");
}

TEST(JsParser, TestMangleTypeofIdentifier) {
    expectPrintedNormalAndMangle( "return typeof (123, x)", "return typeof (123, x);\n", "return typeof (0, x);\n");
    expectPrintedNormalAndMangle( "return typeof (123, x.y)", "return typeof (123, x.y);\n", "return typeof x.y;\n");
    expectPrintedNormalAndMangle( "return typeof (123, x); var x", "return typeof (123, x);\nvar x;\n", "return typeof x;\nvar x;\n");
    expectPrintedNormalAndMangle( "return typeof (true && x)", "return typeof (0, x);\n", "return typeof (0, x);\n");
    expectPrintedNormalAndMangle( "return typeof (true && x.y)", "return typeof x.y;\n", "return typeof x.y;\n");
    expectPrintedNormalAndMangle( "return typeof (true && x); var x", "return typeof x;\nvar x;\n", "return typeof x;\nvar x;\n");
    expectPrintedNormalAndMangle( "return typeof (false || x)", "return typeof (0, x);\n", "return typeof (0, x);\n");
    expectPrintedNormalAndMangle( "return typeof (false || x.y)", "return typeof x.y;\n", "return typeof x.y;\n");
    expectPrintedNormalAndMangle( "return typeof (false || x); var x", "return typeof x;\nvar x;\n", "return typeof x;\nvar x;\n");
}

TEST(JsParser, TestMangleTypeofEqualsUndefined) {
    expectPrintedNormalAndMangle( "return typeof x !== 'undefined'", "return typeof x !== \"undefined\";\n", "return typeof x < \"u\";\n");
    expectPrintedNormalAndMangle( "return typeof x != 'undefined'", "return typeof x != \"undefined\";\n", "return typeof x < \"u\";\n");
    expectPrintedNormalAndMangle( "return 'undefined' !== typeof x", "return \"undefined\" !== typeof x;\n", "return typeof x < \"u\";\n");
    expectPrintedNormalAndMangle( "return 'undefined' != typeof x", "return \"undefined\" != typeof x;\n", "return typeof x < \"u\";\n");
    expectPrintedNormalAndMangle( "return typeof x === 'undefined'", "return typeof x === \"undefined\";\n", "return typeof x > \"u\";\n");
    expectPrintedNormalAndMangle( "return typeof x == 'undefined'", "return typeof x == \"undefined\";\n", "return typeof x > \"u\";\n");
    expectPrintedNormalAndMangle( "return 'undefined' === typeof x", "return \"undefined\" === typeof x;\n", "return typeof x > \"u\";\n");
    expectPrintedNormalAndMangle( "return 'undefined' == typeof x", "return \"undefined\" == typeof x;\n", "return typeof x > \"u\";\n");
}

TEST(JsParser, TestMangleEquals) {
    expectPrintedNormalAndMangle( "return typeof x === y", "return typeof x === y;\n", "return typeof x === y;\n");
    expectPrintedNormalAndMangle( "return typeof x !== y", "return typeof x !== y;\n", "return typeof x !== y;\n");
    expectPrintedNormalAndMangle( "return y === typeof x", "return y === typeof x;\n", "return y === typeof x;\n");
    expectPrintedNormalAndMangle( "return y !== typeof x", "return y !== typeof x;\n", "return y !== typeof x;\n");
    expectPrintedNormalAndMangle( "return typeof x === 'string'", "return typeof x === \"string\";\n", "return typeof x == \"string\";\n");
    expectPrintedNormalAndMangle( "return typeof x !== 'string'", "return typeof x !== \"string\";\n", "return typeof x != \"string\";\n");
    expectPrintedNormalAndMangle( "return 'string' === typeof x", "return \"string\" === typeof x;\n", "return typeof x == \"string\";\n");
    expectPrintedNormalAndMangle( "return 'string' !== typeof x", "return \"string\" !== typeof x;\n", "return typeof x != \"string\";\n");
    expectPrintedNormalAndMangle( "return a === 0", "return a === 0;\n", "return a === 0;\n");
    expectPrintedNormalAndMangle( "return a !== 0", "return a !== 0;\n", "return a !== 0;\n");
    expectPrintedNormalAndMangle( "return +a === 0", "return +a === 0;\n", "return +a == 0;\n");// No BigInt hazard
    expectPrintedNormalAndMangle( "return +a !== 0", "return +a !== 0;\n", "return +a != 0;\n");
    expectPrintedNormalAndMangle( "return -a === 0", "return -a === 0;\n", "return -a === 0;\n");// BigInt hazard
    expectPrintedNormalAndMangle( "return -a !== 0", "return -a !== 0;\n", "return -a !== 0;\n");
    expectPrintedNormalAndMangle( "return a === ''", "return a === \"\";\n", "return a === \"\";\n");
    expectPrintedNormalAndMangle( "return a !== ''", "return a !== \"\";\n", "return a !== \"\";\n");
    expectPrintedNormalAndMangle( "return (a + '!') === 'a!'", "return a + \"!\" === \"a!\";\n", "return a + \"!\" == \"a!\";\n");
    expectPrintedNormalAndMangle( "return (a + '!') !== 'a!'", "return a + \"!\" !== \"a!\";\n", "return a + \"!\" != \"a!\";\n");
    expectPrintedNormalAndMangle( "return (a += '!') === 'a!'", "return (a += \"!\") === \"a!\";\n", "return (a += \"!\") == \"a!\";\n");
    expectPrintedNormalAndMangle( "return (a += '!') !== 'a!'", "return (a += \"!\") !== \"a!\";\n", "return (a += \"!\") != \"a!\";\n");
    expectPrintedNormalAndMangle( "return a === false", "return a === false;\n", "return a === false;\n");
    expectPrintedNormalAndMangle( "return a === true", "return a === true;\n", "return a === true;\n");
    expectPrintedNormalAndMangle( "return a !== false", "return a !== false;\n", "return a !== false;\n");
    expectPrintedNormalAndMangle( "return a !== true", "return a !== true;\n", "return a !== true;\n");
    expectPrintedNormalAndMangle( "return !a === false", "return !a === false;\n", "return !!a;\n");
    expectPrintedNormalAndMangle( "return !a === true", "return !a === true;\n", "return !a;\n");
    expectPrintedNormalAndMangle( "return !a !== false", "return !a !== false;\n", "return !a;\n");
    expectPrintedNormalAndMangle( "return !a !== true", "return !a !== true;\n", "return !!a;\n");
    expectPrintedNormalAndMangle( "return false === !a", "return false === !a;\n", "return !!a;\n");
    expectPrintedNormalAndMangle( "return true === !a", "return true === !a;\n", "return !a;\n");
    expectPrintedNormalAndMangle( "return false !== !a", "return false !== !a;\n", "return !a;\n");
    expectPrintedNormalAndMangle( "return true !== !a", "return true !== !a;\n", "return !!a;\n");
    expectPrintedNormalAndMangle( "return a === !b", "return a === !b;\n", "return a === !b;\n");
    expectPrintedNormalAndMangle( "return a === !b", "return a === !b;\n", "return a === !b;\n");
    expectPrintedNormalAndMangle( "return a !== !b", "return a !== !b;\n", "return a !== !b;\n");
    expectPrintedNormalAndMangle( "return a !== !b", "return a !== !b;\n", "return a !== !b;\n");
    expectPrintedNormalAndMangle( "return !a === !b", "return !a === !b;\n", "return !a == !b;\n");
    expectPrintedNormalAndMangle( "return !a === !b", "return !a === !b;\n", "return !a == !b;\n");
    expectPrintedNormalAndMangle( "return !a !== !b", "return !a !== !b;\n", "return !a != !b;\n");
    expectPrintedNormalAndMangle( "return !a !== !b", "return !a !== !b;\n", "return !a != !b;\n");
    // These have BigInt hazards and should not be changed
    expectPrintedNormalAndMangle( "return (a, -1n) !== -1", "return (a, -1n) !== -1;\n", "return a, -1n !== -1;\n");
    expectPrintedNormalAndMangle( "return (a, ~1n) !== -1", "return (a, ~1n) !== -1;\n", "return a, ~1n !== -1;\n");
    expectPrintedNormalAndMangle( "return (a -= 1n) !== -1", "return (a -= 1n) !== -1;\n", "return (a -= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a *= 1n) !== -1", "return (a *= 1n) !== -1;\n", "return (a *= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a **= 1n) !== -1", "return (a **= 1n) !== -1;\n", "return (a **= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a /= 1n) !== -1", "return (a /= 1n) !== -1;\n", "return (a /= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a %= 1n) !== -1", "return (a %= 1n) !== -1;\n", "return (a %= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a &= 1n) !== -1", "return (a &= 1n) !== -1;\n", "return (a &= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a |= 1n) !== -1", "return (a |= 1n) !== -1;\n", "return (a |= 1n) !== -1;\n");
    expectPrintedNormalAndMangle( "return (a ^= 1n) !== -1", "return (a ^= 1n) !== -1;\n", "return (a ^= 1n) !== -1;\n");
}

TEST(JsParser, TestMangleUnaryInsideComma) {
    expectPrintedNormalAndMangle( "return -(a, b)", "return -(a, b);\n", "return a, -b;\n");
    expectPrintedNormalAndMangle( "return +(a, b)", "return +(a, b);\n", "return a, +b;\n");
    expectPrintedNormalAndMangle( "return ~(a, b)", "return ~(a, b);\n", "return a, ~b;\n");
    expectPrintedNormalAndMangle( "return !(a, b)", "return !(a, b);\n", "return a, !b;\n");
    expectPrintedNormalAndMangle( "return void (a, b)", "return void (a, b);\n", "return a, void b;\n");
    expectPrintedNormalAndMangle( "return typeof (a, b)", "return typeof (a, b);\n", "return typeof (a, b);\n");
    expectPrintedNormalAndMangle( "return delete (a, b)", "return delete (a, b);\n", "return delete (a, b);\n");
}

TEST(JsParser, TestMangleBinaryInsideComma) {
    expectPrintedNormalAndMangle( "(a, b) && c", "(a, b) && c;\n", "a, b && c;\n");
    expectPrintedNormalAndMangle( "(a, b) == c", "(a, b) == c;\n", "a, b == c;\n");
    expectPrintedNormalAndMangle( "(a, b) + c", "(a, b) + c;\n", "a, b + c;\n");
    expectPrintedNormalAndMangle( "a && (b, c)", "a && (b, c);\n", "a && (b, c);\n");
    expectPrintedNormalAndMangle( "a == (b, c)", "a == (b, c);\n", "a == (b, c);\n");
    expectPrintedNormalAndMangle( "a + (b, c)", "a + (b, c);\n", "a + (b, c);\n");
}

TEST(JsParser, TestMangleUnaryConstantFolding) {
    expectPrintedNormalAndMangle( "x = +5", "x = 5;\n", "x = 5;\n");
    expectPrintedNormalAndMangle( "x = -5", "x = -5;\n", "x = -5;\n");
    expectPrintedNormalAndMangle( "x = ~5", "x = ~5;\n", "x = -6;\n");
    expectPrintedNormalAndMangle( "x = !5", "x = false;\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = typeof 5", "x = \"number\";\n", "x = \"number\";\n");
    expectPrintedNormalAndMangle( "x = +''", "x = 0;\n", "x = 0;\n");
    expectPrintedNormalAndMangle( "x = +[]", "x = 0;\n", "x = 0;\n");
    expectPrintedNormalAndMangle( "x = +{}", "x = NaN;\n", "x = NaN;\n");
    expectPrintedNormalAndMangle( "x = +/1/", "x = NaN;\n", "x = NaN;\n");
    expectPrintedNormalAndMangle( "x = +[1]", "x = +[1];\n", "x = +[1];\n");
    expectPrintedNormalAndMangle( "x = +'123'", "x = 123;\n", "x = 123;\n");
    expectPrintedNormalAndMangle( "x = +'-123'", "x = -123;\n", "x = -123;\n");
    expectPrintedNormalAndMangle( "x = +'0x10'", "x = +\"0x10\";\n", "x = +\"0x10\";\n");
    expectPrintedNormalAndMangle( "x = +{toString:()=>1}", "x = +{ toString: () => 1 };\n", "x = +{ toString: () => 1 };\n");
    expectPrintedNormalAndMangle( "x = +{valueOf:()=>1}", "x = +{ valueOf: () => 1 };\n", "x = +{ valueOf: () => 1 };\n");
}

TEST(JsParser, TestMangleBinaryConstantFolding) {
    expectPrintedNormalAndMangle( "x = 3 + 6", "x = 3 + 6;\n", "x = 9;\n");
    expectPrintedNormalAndMangle( "x = 3 - 6", "x = 3 - 6;\n", "x = -3;\n");
    expectPrintedNormalAndMangle( "x = 3 * 6", "x = 3 * 6;\n", "x = 18;\n");
    expectPrintedNormalAndMangle( "x = 3 / 6", "x = 3 / 6;\n", "x = 3 / 6;\n");
    expectPrintedNormalAndMangle( "x = 3 % 6", "x = 3 % 6;\n", "x = 3 % 6;\n");
    expectPrintedNormalAndMangle( "x = 3 ** 6", "x = 3 ** 6;\n", "x = 3 ** 6;\n");
    expectPrintedNormalAndMangle( "x = 0 / 0", "x = 0 / 0;\n", "x = NaN;\n");
    expectPrintedNormalAndMangle( "x = 123 / 0", "x = 123 / 0;\n", "x = Infinity;\n");
    expectPrintedNormalAndMangle( "x = 123 / -0", "x = 123 / -0;\n", "x = -Infinity;\n");
    expectPrintedNormalAndMangle( "x = -123 / 0", "x = -123 / 0;\n", "x = -Infinity;\n");
    expectPrintedNormalAndMangle( "x = -123 / -0", "x = -123 / -0;\n", "x = Infinity;\n");
    expectPrintedNormalAndMangle( "x = 3 < 6", "x = 3 < 6;\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 3 > 6", "x = 3 > 6;\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 3 <= 6", "x = 3 <= 6;\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 3 >= 6", "x = 3 >= 6;\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 3 == 6", "x = false;\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 3 != 6", "x = true;\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 3 === 6", "x = false;\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 3 !== 6", "x = true;\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 'a' < 'b'", "x = \"a\" < \"b\";\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 'a' > 'b'", "x = \"a\" > \"b\";\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 'a' <= 'b'", "x = \"a\" <= \"b\";\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 'a' >= 'b'", "x = \"a\" >= \"b\";\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 'ab' < 'abc'", "x = \"ab\" < \"abc\";\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 'ab' > 'abc'", "x = \"ab\" > \"abc\";\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 'ab' <= 'abc'", "x = \"ab\" <= \"abc\";\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = 'ab' >= 'abc'", "x = \"ab\" >= \"abc\";\n", "x = false;\n");
    // This checks for comparing by code point vs. by code unit
    expectPrintedNormalAndMangle( "x = '𐙩' < 'ﬡ'", "x = \"𐙩\" < \"ﬡ\";\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = '𐙩' > 'ﬡ'", "x = \"𐙩\" > \"ﬡ\";\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = '𐙩' <= 'ﬡ'", "x = \"𐙩\" <= \"ﬡ\";\n", "x = true;\n");
    expectPrintedNormalAndMangle( "x = '𐙩' >= 'ﬡ'", "x = \"𐙩\" >= \"ﬡ\";\n", "x = false;\n");
    expectPrintedNormalAndMangle( "x = 3 in 6", "x = 3 in 6;\n", "x = 3 in 6;\n");
    expectPrintedNormalAndMangle( "x = 3 instanceof 6", "x = 3 instanceof 6;\n", "x = 3 instanceof 6;\n");
    expectPrintedNormalAndMangle( "x = (3, 6)", "x = (3, 6);\n", "x = 6;\n");
    expectPrintedNormalAndMangle( "x = 10 << 0", "x = 10 << 0;\n", "x = 10;\n");
    expectPrintedNormalAndMangle( "x = 10 << 1", "x = 10 << 1;\n", "x = 20;\n");
    expectPrintedNormalAndMangle( "x = 10 << 16", "x = 10 << 16;\n", "x = 655360;\n");
    expectPrintedNormalAndMangle( "x = 10 << 17", "x = 10 << 17;\n", "x = 10 << 17;\n");
    expectPrintedNormalAndMangle( "x = 10 >> 0", "x = 10 >> 0;\n", "x = 10;\n");
    expectPrintedNormalAndMangle( "x = 10 >> 1", "x = 10 >> 1;\n", "x = 5;\n");
    expectPrintedNormalAndMangle( "x = 10 >>> 0", "x = 10 >>> 0;\n", "x = 10;\n");
    expectPrintedNormalAndMangle( "x = 10 >>> 1", "x = 10 >>> 1;\n", "x = 5;\n");
    expectPrintedNormalAndMangle( "x = -10 >>> 1", "x = -10 >>> 1;\n", "x = -10 >>> 1;\n");
    expectPrintedNormalAndMangle( "x = -1 >>> 0", "x = -1 >>> 0;\n", "x = -1 >>> 0;\n");
    expectPrintedNormalAndMangle( "x = -123 >>> 5", "x = -123 >>> 5;\n", "x = -123 >>> 5;\n");
    expectPrintedNormalAndMangle( "x = -123 >>> 6", "x = -123 >>> 6;\n", "x = 67108862;\n");
    expectPrintedNormalAndMangle( "x = 3 & 6", "x = 3 & 6;\n", "x = 2;\n");
    expectPrintedNormalAndMangle( "x = 3 | 6", "x = 3 | 6;\n", "x = 7;\n");
    expectPrintedNormalAndMangle( "x = 3 ^ 6", "x = 3 ^ 6;\n", "x = 5;\n");
    expectPrintedNormalAndMangle( "x = 3 && 6", "x = 6;\n", "x = 6;\n");
    expectPrintedNormalAndMangle( "x = 3 || 6", "x = 3;\n", "x = 3;\n");
    expectPrintedNormalAndMangle( "x = 3 ?? 6", "x = 3;\n", "x = 3;\n");
}

TEST(JsParser, TestMangleNestedLogical) {
    expectPrintedNormalAndMangle( "(a && b) && c", "a && b && c;\n", "a && b && c;\n");
    expectPrintedNormalAndMangle( "a && (b && c)", "a && (b && c);\n", "a && b && c;\n");
    expectPrintedNormalAndMangle( "(a || b) && c", "(a || b) && c;\n", "(a || b) && c;\n");
    expectPrintedNormalAndMangle( "a && (b || c)", "a && (b || c);\n", "a && (b || c);\n");
    expectPrintedNormalAndMangle( "(a || b) || c", "a || b || c;\n", "a || b || c;\n");
    expectPrintedNormalAndMangle( "a || (b || c)", "a || (b || c);\n", "a || b || c;\n");
    expectPrintedNormalAndMangle( "(a && b) || c", "a && b || c;\n", "a && b || c;\n");
    expectPrintedNormalAndMangle( "a || (b && c)", "a || b && c;\n", "a || b && c;\n");
}

TEST(JsParser, TestMangleEqualsUndefined) {
    expectPrintedNormalAndMangle( "return a === void 0", "return a === void 0;\n", "return a === void 0;\n");
    expectPrintedNormalAndMangle( "return a !== void 0", "return a !== void 0;\n", "return a !== void 0;\n");
    expectPrintedNormalAndMangle( "return void 0 === a", "return void 0 === a;\n", "return a === void 0;\n");
    expectPrintedNormalAndMangle( "return void 0 !== a", "return void 0 !== a;\n", "return a !== void 0;\n");
    expectPrintedNormalAndMangle( "return a == void 0", "return a == void 0;\n", "return a == null;\n");
    expectPrintedNormalAndMangle( "return a != void 0", "return a != void 0;\n", "return a != null;\n");
    expectPrintedNormalAndMangle( "return void 0 == a", "return void 0 == a;\n", "return a == null;\n");
    expectPrintedNormalAndMangle( "return void 0 != a", "return void 0 != a;\n", "return a != null;\n");
    expectPrintedNormalAndMangle( "return a === null || a === undefined", "return a === null || a === void 0;\n", "return a == null;\n");
    expectPrintedNormalAndMangle( "return a === null || a !== undefined", "return a === null || a !== void 0;\n", "return a === null || a !== void 0;\n");
    expectPrintedNormalAndMangle( "return a !== null || a === undefined", "return a !== null || a === void 0;\n", "return a !== null || a === void 0;\n");
    expectPrintedNormalAndMangle( "return a === null && a === undefined", "return a === null && a === void 0;\n", "return a === null && a === void 0;\n");
    expectPrintedNormalAndMangle( "return a.x === null || a.x === undefined", "return a.x === null || a.x === void 0;\n", "return a.x === null || a.x === void 0;\n");
    expectPrintedNormalAndMangle( "return a === undefined || a === null", "return a === void 0 || a === null;\n", "return a == null;\n");
    expectPrintedNormalAndMangle( "return a === undefined || a !== null", "return a === void 0 || a !== null;\n", "return a === void 0 || a !== null;\n");
    expectPrintedNormalAndMangle( "return a !== undefined || a === null", "return a !== void 0 || a === null;\n", "return a !== void 0 || a === null;\n");
    expectPrintedNormalAndMangle( "return a === undefined && a === null", "return a === void 0 && a === null;\n", "return a === void 0 && a === null;\n");
    expectPrintedNormalAndMangle( "return a.x === undefined || a.x === null", "return a.x === void 0 || a.x === null;\n", "return a.x === void 0 || a.x === null;\n");
    expectPrintedNormalAndMangle( "return a !== null && a !== undefined", "return a !== null && a !== void 0;\n", "return a != null;\n");
    expectPrintedNormalAndMangle( "return a !== null && a === undefined", "return a !== null && a === void 0;\n", "return a !== null && a === void 0;\n");
    expectPrintedNormalAndMangle( "return a === null && a !== undefined", "return a === null && a !== void 0;\n", "return a === null && a !== void 0;\n");
    expectPrintedNormalAndMangle( "return a !== null || a !== undefined", "return a !== null || a !== void 0;\n", "return a !== null || a !== void 0;\n");
    expectPrintedNormalAndMangle( "return a.x !== null && a.x !== undefined", "return a.x !== null && a.x !== void 0;\n", "return a.x !== null && a.x !== void 0;\n");
    expectPrintedNormalAndMangle( "return a !== undefined && a !== null", "return a !== void 0 && a !== null;\n", "return a != null;\n");
    expectPrintedNormalAndMangle( "return a !== undefined && a === null", "return a !== void 0 && a === null;\n", "return a !== void 0 && a === null;\n");
    expectPrintedNormalAndMangle( "return a === undefined && a !== null", "return a === void 0 && a !== null;\n", "return a === void 0 && a !== null;\n");
    expectPrintedNormalAndMangle( "return a !== undefined || a !== null", "return a !== void 0 || a !== null;\n", "return a !== void 0 || a !== null;\n");
    expectPrintedNormalAndMangle( "return a.x !== undefined && a.x !== null", "return a.x !== void 0 && a.x !== null;\n", "return a.x !== void 0 && a.x !== null;\n");
}

TEST(JsParser, TestMangleUnusedFunctionExpressionNames) {
    expectPrintedNormalAndMangle( "x = function y() {}", "x = function y() {\n};\n", "x = function() {\n};\n");
    expectPrintedNormalAndMangle( "x = function y() { return y }", "x = function y() {\n  return y;\n};\n", "x = function y() {\n  return y;\n};\n");
    expectPrintedNormalAndMangle( "x = function y() { return eval('y') }", "x = function y() {\n  return eval(\"y\");\n};\n", "x = function y() {\n  return eval(\"y\");\n};\n");
    expectPrintedNormalAndMangle( "x = function y() { if (0) return y }", "x = function y() {\n  if (0) return y;\n};\n", "x = function() {\n};\n");
}

TEST(JsParser, TestMangleClass) {
    expectPrintedNormalAndMangle( "class x {['y'] = z}", "class x {\n  [\"y\"] = z;\n}\n", "class x {\n  y = z;\n}\n");
    expectPrintedNormalAndMangle( "class x {['y']() {}}", "class x {\n  [\"y\"]() {\n  }\n}\n", "class x {\n  y() {\n  }\n}\n");
    expectPrintedNormalAndMangle( "class x {get ['y']() {}}", "class x {\n  get [\"y\"]() {\n  }\n}\n", "class x {\n  get y() {\n  }\n}\n");
    expectPrintedNormalAndMangle( "class x {set ['y'](z) {}}", "class x {\n  set [\"y\"](z) {\n  }\n}\n", "class x {\n  set y(z) {\n  }\n}\n");
    expectPrintedNormalAndMangle( "class x {async ['y']() {}}", "class x {\n  async [\"y\"]() {\n  }\n}\n", "class x {\n  async y() {\n  }\n}\n");
    expectPrintedNormalAndMangle( "x = class {['y'] = z}", "x = class {\n  [\"y\"] = z;\n};\n", "x = class {\n  y = z;\n};\n");
    expectPrintedNormalAndMangle( "x = class {['y']() {}}", "x = class {\n  [\"y\"]() {\n  }\n};\n", "x = class {\n  y() {\n  }\n};\n");
    expectPrintedNormalAndMangle( "x = class {get ['y']() {}}", "x = class {\n  get [\"y\"]() {\n  }\n};\n", "x = class {\n  get y() {\n  }\n};\n");
    expectPrintedNormalAndMangle( "x = class {set ['y'](z) {}}", "x = class {\n  set [\"y\"](z) {\n  }\n};\n", "x = class {\n  set y(z) {\n  }\n};\n");
    expectPrintedNormalAndMangle( "x = class {async ['y']() {}}", "x = class {\n  async [\"y\"]() {\n  }\n};\n", "x = class {\n  async y() {\n  }\n};\n");
}

TEST(JsParser, TestMangleUnusedClassExpressionNames) {
    expectPrintedNormalAndMangle( "x = class y {}", "x = class y {\n};\n", "x = class {\n};\n");
    expectPrintedNormalAndMangle( "x = class y { foo() { return y } }", "x = class y {\n  foo() {\n    return y;\n  }\n};\n", "x = class y {\n  foo() {\n    return y;\n  }\n};\n");
    expectPrintedNormalAndMangle( "x = class y { foo() { if (0) return y } }", "x = class y {\n  foo() {\n    if (0) return _y;\n  }\n};\n", "x = class {\n  foo() {\n  }\n};\n");
}

TEST(JsParser, TestMangleUnused) {
    expectPrintedNormalAndMangle( "null", "null;\n", "");
    expectPrintedNormalAndMangle( "void 0", "", "");
    expectPrintedNormalAndMangle( "void 0", "", "");
    expectPrintedNormalAndMangle( "false", "false;\n", "");
    expectPrintedNormalAndMangle( "true", "true;\n", "");
    expectPrintedNormalAndMangle( "123", "123;\n", "");
    expectPrintedNormalAndMangle( "123n", "123n;\n", "");
    expectPrintedNormalAndMangle( "'abc'", "\"abc\";\n", "\"abc\";\n");// Technically a directive, not a string expression
    expectPrintedNormalAndMangle( "0; 'abc'", "0;\n\"abc\";\n", "");// Actually a string expression
    expectPrintedNormalAndMangle( "'abc'; 'use strict'", "\"abc\";\n\"use strict\";\n", "\"abc\";\n\"use strict\";\n");
    expectPrintedNormalAndMangle( "function f() { 'abc'; 'use strict' }", "function f() {\n  \"abc\";\n  \"use strict\";\n}\n", "function f() {\n  \"abc\";\n  \"use strict\";\n}\n");
    expectPrintedNormalAndMangle( "this", "this;\n", "");
    expectPrintedNormalAndMangle( "/regex/", "/regex/;\n", "");
    expectPrintedNormalAndMangle( "(function() {})", "(function() {\n});\n", "");
    expectPrintedNormalAndMangle( "(() => {})", "(() => {\n});\n", "");
    expectPrintedNormalAndMangle( "import.meta", "import.meta;\n", "");
    // Unary operators
    expectPrintedNormalAndMangle( "+x", "+x;\n", "+x;\n");
    expectPrintedNormalAndMangle( "-x", "-x;\n", "-x;\n");
    expectPrintedNormalAndMangle( "!x", "!x;\n", "x;\n");
    expectPrintedNormalAndMangle( "~x", "~x;\n", "~x;\n");
    expectPrintedNormalAndMangle( "++x", "++x;\n", "++x;\n");
    expectPrintedNormalAndMangle( "--x", "--x;\n", "--x;\n");
    expectPrintedNormalAndMangle( "x++", "x++;\n", "x++;\n");
    expectPrintedNormalAndMangle( "x--", "x--;\n", "x--;\n");
    expectPrintedNormalAndMangle( "void x", "void x;\n", "x;\n");
    expectPrintedNormalAndMangle( "delete x", "delete x;\n", "delete x;\n");
    expectPrintedNormalAndMangle( "typeof x", "typeof x;\n", "");
    expectPrintedNormalAndMangle( "typeof x()", "typeof x();\n", "x();\n");
    expectPrintedNormalAndMangle( "typeof (0, x)", "typeof (0, x);\n", "x;\n");
    expectPrintedNormalAndMangle( "typeof (0 || x)", "typeof (0, x);\n", "x;\n");
    expectPrintedNormalAndMangle( "typeof (1 && x)", "typeof (0, x);\n", "x;\n");
    expectPrintedNormalAndMangle( "typeof (1 ? x : 0)", "typeof (1 ? x : 0);\n", "x;\n");
    expectPrintedNormalAndMangle( "typeof (0 ? 1 : x)", "typeof (0 ? 1 : x);\n", "x;\n");
    // Binary operators
    expectPrintedNormalAndMangle( "a + b", "a + b;\n", "a + b;\n");
    expectPrintedNormalAndMangle( "a - b", "a - b;\n", "a - b;\n");
    expectPrintedNormalAndMangle( "a * b", "a * b;\n", "a * b;\n");
    expectPrintedNormalAndMangle( "a / b", "a / b;\n", "a / b;\n");
    expectPrintedNormalAndMangle( "a % b", "a % b;\n", "a % b;\n");
    expectPrintedNormalAndMangle( "a ** b", "a ** b;\n", "a ** b;\n");
    expectPrintedNormalAndMangle( "a & b", "a & b;\n", "a & b;\n");
    expectPrintedNormalAndMangle( "a | b", "a | b;\n", "a | b;\n");
    expectPrintedNormalAndMangle( "a ^ b", "a ^ b;\n", "a ^ b;\n");
    expectPrintedNormalAndMangle( "a << b", "a << b;\n", "a << b;\n");
    expectPrintedNormalAndMangle( "a >> b", "a >> b;\n", "a >> b;\n");
    expectPrintedNormalAndMangle( "a >>> b", "a >>> b;\n", "a >>> b;\n");
    expectPrintedNormalAndMangle( "a === b", "a === b;\n", "a, b;\n");
    expectPrintedNormalAndMangle( "a !== b", "a !== b;\n", "a, b;\n");
    expectPrintedNormalAndMangle( "a == b", "a == b;\n", "a == b;\n");
    expectPrintedNormalAndMangle( "a != b", "a != b;\n", "a != b;\n");
    expectPrintedNormalAndMangle( "a, b", "a, b;\n", "a, b;\n");
    expectPrintedNormalAndMangle( "a + '' == b", "a + \"\" == b;\n", "a + \"\" == b;\n");
    expectPrintedNormalAndMangle( "a + '' != b", "a + \"\" != b;\n", "a + \"\" != b;\n");
    expectPrintedNormalAndMangle( "a + '' == b + ''", "a + \"\" == b + \"\";\n", "a + \"\", b + \"\";\n");
    expectPrintedNormalAndMangle( "a + '' != b + ''", "a + \"\" != b + \"\";\n", "a + \"\", b + \"\";\n");
    expectPrintedNormalAndMangle( "a + '' == (b | c)", "a + \"\" == (b | c);\n", "a + \"\", b | c;\n");
    expectPrintedNormalAndMangle( "a + '' != (b | c)", "a + \"\" != (b | c);\n", "a + \"\", b | c;\n");
    expectPrintedNormalAndMangle( "typeof a == b + ''", "typeof a == b + \"\";\n", "b + \"\";\n");
    expectPrintedNormalAndMangle( "typeof a != b + ''", "typeof a != b + \"\";\n", "b + \"\";\n");
    expectPrintedNormalAndMangle( "typeof a == 'b'", "typeof a == \"b\";\n", "");
    expectPrintedNormalAndMangle( "typeof a != 'b'", "typeof a != \"b\";\n", "");
    // Known globals can be removed
    expectPrintedNormalAndMangle( "Object", "Object;\n", "");
    expectPrintedNormalAndMangle( "Object()", "Object();\n", "Object();\n");
    expectPrintedNormalAndMangle( "NonObject", "NonObject;\n", "NonObject;\n");
    expectPrintedNormalAndMangle( "var bound; unbound", "var bound;\nunbound;\n", "var bound;\nunbound;\n");
    expectPrintedNormalAndMangle( "var bound; bound", "var bound;\nbound;\n", "var bound;\n");
    expectPrintedNormalAndMangle( "foo, 123, bar", "foo, 123, bar;\n", "foo, bar;\n");
    expectPrintedNormalAndMangle( "[[foo,, 123,, bar]]", "[[foo, , 123, , bar]];\n", "foo, bar;\n");
    expectPrintedNormalAndMangle( "var bound; [123, unbound, ...unbound, 234]", "var bound;\n[123, unbound, ...unbound, 234];\n", "var bound;\n[unbound, ...unbound];\n");
    expectPrintedNormalAndMangle( "var bound; [123, bound, ...bound, 234]", "var bound;\n[123, bound, ...bound, 234];\n", "var bound;\n[...bound];\n");
    expectPrintedNormalAndMangle(
        "({foo, x: 123, [y]: 123, z: z, bar})",
        "({ foo, x: 123, [y]: 123, z, bar });\n",
        "foo, y + \"\", z, bar;\n");
    expectPrintedNormalAndMangle(
        "var bound; ({x: 123, unbound, ...unbound, [unbound]: null, y: 234})",
        "var bound;\n({ x: 123, unbound, ...unbound, [unbound]: null, y: 234 });\n",
        "var bound;\n({ unbound, ...unbound, [unbound]: 0 });\n");
    expectPrintedNormalAndMangle(
        "var bound; ({x: 123, bound, ...bound, [bound]: null, y: 234})",
        "var bound;\n({ x: 123, bound, ...bound, [bound]: null, y: 234 });\n",
        "var bound;\n({ ...bound, [bound]: 0 });\n");
    expectPrintedNormalAndMangle(
        "var bound; ({x: 123, bound, ...bound, [bound]: foo(), y: 234})",
        "var bound;\n({ x: 123, bound, ...bound, [bound]: foo(), y: 234 });\n",
        "var bound;\n({ ...bound, [bound]: foo() });\n");
    expectPrintedNormalAndMangle( "console.log(1, foo(), bar())", "console.log(1, foo(), bar());\n", "console.log(1, foo(), bar());\n");
    expectPrintedNormalAndMangle( "/* @__PURE__ */ console.log(1, foo(), bar())", "/* @__PURE__ */ console.log(1, foo(), bar());\n", "foo(), bar();\n");
    expectPrintedNormalAndMangle( "new TestCase(1, foo(), bar())", "new TestCase(1, foo(), bar());\n", "new TestCase(1, foo(), bar());\n");
    expectPrintedNormalAndMangle( "/* @__PURE__ */ new TestCase(1, foo(), bar())", "/* @__PURE__ */ new TestCase(1, foo(), bar());\n", "foo(), bar();\n");
    expectPrintedNormalAndMangle( "let x = (1, 2)", "let x = (1, 2);\n", "let x = 2;\n");
    expectPrintedNormalAndMangle( "let x = (y, 2)", "let x = (y, 2);\n", "let x = (y, 2);\n");
    expectPrintedNormalAndMangle( "let x = (/* @__PURE__ */ foo(bar), 2)", "let x = (/* @__PURE__ */ foo(bar), 2);\n", "let x = (bar, 2);\n");
    expectPrintedNormalAndMangle( "let x = (2, y)", "let x = (2, y);\n", "let x = y;\n");
    expectPrintedNormalAndMangle( "let x = (2, y)()", "let x = (2, y)();\n", "let x = y();\n");
    expectPrintedNormalAndMangle( "let x = (true && y)()", "let x = y();\n", "let x = y();\n");
    expectPrintedNormalAndMangle( "let x = (false || y)()", "let x = y();\n", "let x = y();\n");
    expectPrintedNormalAndMangle( "let x = (null ?? y)()", "let x = y();\n", "let x = y();\n");
    expectPrintedNormalAndMangle( "let x = (1 ? y : 2)()", "let x = (1 ? y : 2)();\n", "let x = y();\n");
    expectPrintedNormalAndMangle( "let x = (0 ? 1 : y)()", "let x = (0 ? 1 : y)();\n", "let x = y();\n");
    // Make sure call targets with "this" values are preserved
    expectPrintedNormalAndMangle( "let x = (2, y.z)", "let x = (2, y.z);\n", "let x = y.z;\n");
    expectPrintedNormalAndMangle( "let x = (2, y.z)()", "let x = (2, y.z)();\n", "let x = (0, y.z)();\n");
    expectPrintedNormalAndMangle( "let x = (true && y.z)()", "let x = (0, y.z)();\n", "let x = (0, y.z)();\n");
    expectPrintedNormalAndMangle( "let x = (false || y.z)()", "let x = (0, y.z)();\n", "let x = (0, y.z)();\n");
    expectPrintedNormalAndMangle( "let x = (null ?? y.z)()", "let x = (0, y.z)();\n", "let x = (0, y.z)();\n");
    expectPrintedNormalAndMangle( "let x = (1 ? y.z : 2)()", "let x = (1 ? y.z : 2)();\n", "let x = (0, y.z)();\n");
    expectPrintedNormalAndMangle( "let x = (0 ? 1 : y.z)()", "let x = (0 ? 1 : y.z)();\n", "let x = (0, y.z)();\n");
    expectPrintedNormalAndMangle( "let x = (2, y[z])", "let x = (2, y[z]);\n", "let x = y[z];\n");
    expectPrintedNormalAndMangle( "let x = (2, y[z])()", "let x = (2, y[z])();\n", "let x = (0, y[z])();\n");
    expectPrintedNormalAndMangle( "let x = (true && y[z])()", "let x = (0, y[z])();\n", "let x = (0, y[z])();\n");
    expectPrintedNormalAndMangle( "let x = (false || y[z])()", "let x = (0, y[z])();\n", "let x = (0, y[z])();\n");
    expectPrintedNormalAndMangle( "let x = (null ?? y[z])()", "let x = (0, y[z])();\n", "let x = (0, y[z])();\n");
    expectPrintedNormalAndMangle( "let x = (1 ? y[z] : 2)()", "let x = (1 ? y[z] : 2)();\n", "let x = (0, y[z])();\n");
    expectPrintedNormalAndMangle( "let x = (0 ? 1 : y[z])()", "let x = (0 ? 1 : y[z])();\n", "let x = (0, y[z])();\n");
    // Make sure the return value of "delete" is preserved
    expectPrintedNormalAndMangle( "delete (x)", "delete x;\n", "delete x;\n");
    expectPrintedNormalAndMangle( "delete (x); var x", "delete x;\nvar x;\n", "delete x;\nvar x;\n");
    expectPrintedNormalAndMangle( "delete (x.y)", "delete x.y;\n", "delete x.y;\n");
    expectPrintedNormalAndMangle( "delete (x[y])", "delete x[y];\n", "delete x[y];\n");
    expectPrintedNormalAndMangle( "delete (x?.y)", "delete x?.y;\n", "delete x?.y;\n");
    expectPrintedNormalAndMangle( "delete (x?.[y])", "delete x?.[y];\n", "delete x?.[y];\n");
    expectPrintedNormalAndMangle( "delete (2, x)", "delete (2, x);\n", "delete (0, x);\n");
    expectPrintedNormalAndMangle( "delete (2, x); var x", "delete (2, x);\nvar x;\n", "delete (0, x);\nvar x;\n");
    expectPrintedNormalAndMangle( "delete (2, x.y)", "delete (2, x.y);\n", "delete (0, x.y);\n");
    expectPrintedNormalAndMangle( "delete (2, x[y])", "delete (2, x[y]);\n", "delete (0, x[y]);\n");
    expectPrintedNormalAndMangle( "delete (2, x?.y)", "delete (2, x?.y);\n", "delete (0, x?.y);\n");
    expectPrintedNormalAndMangle( "delete (2, x?.[y])", "delete (2, x?.[y]);\n", "delete (0, x?.[y]);\n");
    expectPrintedNormalAndMangle( "delete (true && x)", "delete (0, x);\n", "delete (0, x);\n");
    expectPrintedNormalAndMangle( "delete (false || x)", "delete (0, x);\n", "delete (0, x);\n");
    expectPrintedNormalAndMangle( "delete (null ?? x)", "delete (0, x);\n", "delete (0, x);\n");
    expectPrintedNormalAndMangle( "delete (1 ? x : 2)", "delete (1 ? x : 2);\n", "delete (0, x);\n");
    expectPrintedNormalAndMangle( "delete (0 ? 1 : x)", "delete (0 ? 1 : x);\n", "delete (0, x);\n");
    expectPrintedNormalAndMangle( "delete (NaN)", "delete NaN;\n", "delete NaN;\n");
    expectPrintedNormalAndMangle( "delete (Infinity)", "delete Infinity;\n", "delete Infinity;\n");
    expectPrintedNormalAndMangle( "delete (-Infinity)", "delete -Infinity;\n", "delete -Infinity;\n");
    expectPrintedNormalAndMangle( "delete (1, NaN)", "delete (1, NaN);\n", "delete (0, NaN);\n");
    expectPrintedNormalAndMangle( "delete (1, Infinity)", "delete (1, Infinity);\n", "delete (0, Infinity);\n");
    expectPrintedNormalAndMangle( "delete (1, -Infinity)", "delete (1, -Infinity);\n", "delete -Infinity;\n");
    expectPrintedNormalAndMangle( "foo ? 1 : 2", "foo ? 1 : 2;\n", "foo;\n");
    expectPrintedNormalAndMangle( "foo ? 1 : bar", "foo ? 1 : bar;\n", "foo || bar;\n");
    expectPrintedNormalAndMangle( "foo ? bar : 2", "foo ? bar : 2;\n", "foo && bar;\n");
    expectPrintedNormalAndMangle( "foo ? bar : baz", "foo ? bar : baz;\n", "foo ? bar : baz;\n");
    for (const std::string& op : std::vector<std::string>{"&&", "||", "??"}) {
        expectPrintedNormalAndMangle( "foo "+op+" bar", "foo "+op+" bar;\n", "foo "+op+" bar;\n");
        expectPrintedNormalAndMangle( "var foo; foo "+op+" bar", "var foo;\nfoo "+op+" bar;\n", "var foo;\nfoo "+op+" bar;\n");
        expectPrintedNormalAndMangle( "var bar; foo "+op+" bar", "var bar;\nfoo "+op+" bar;\n", "var bar;\nfoo;\n");
        expectPrintedNormalAndMangle( "var foo, bar; foo "+op+" bar", "var foo, bar;\nfoo "+op+" bar;\n", "var foo, bar;\n");
    }
    expectPrintedNormalAndMangle( "tag`a${b}c${d}e`", "tag`a${b}c${d}e`;\n", "tag`a${b}c${d}e`;\n");
    expectPrintedNormalAndMangle( "`a${b}c${d}e`", "`a${b}c${d}e`;\n", "`${b}${d}`;\n");
    // These can't be reduced to string addition due to "valueOf". See:
    // https://github.com/terser/terser/issues/1128#issuecomment-994209801
    expectPrintedNormalAndMangle( "`stuff ${x} ${1}`", "`stuff ${x} ${1}`;\n", "`${x}`;\n");
    expectPrintedNormalAndMangle( "`stuff ${1} ${y}`", "`stuff ${1} ${y}`;\n", "`${y}`;\n");
    expectPrintedNormalAndMangle( "`stuff ${x} ${y}`", "`stuff ${x} ${y}`;\n", "`${x}${y}`;\n");
    expectPrintedNormalAndMangle( "`stuff ${x ? 1 : 2} ${y}`", "`stuff ${x ? 1 : 2} ${y}`;\n", "x, `${y}`;\n");
    expectPrintedNormalAndMangle( "`stuff ${x} ${y ? 1 : 2}`", "`stuff ${x} ${y ? 1 : 2}`;\n", "`${x}`, y;\n");
    expectPrintedNormalAndMangle( "`stuff ${x} ${y ? 1 : 2} ${z}`", "`stuff ${x} ${y ? 1 : 2} ${z}`;\n", "`${x}`, y, `${z}`;\n");
    expectPrintedNormalAndMangle( "'a' + b + 'c' + d", "\"a\" + b + \"c\" + d;\n", "\"\" + b + d;\n");
    expectPrintedNormalAndMangle( "a + 'b' + c + 'd'", "a + \"b\" + c + \"d\";\n", "a + \"\" + c;\n");
    expectPrintedNormalAndMangle( "a + b + 'c' + 'd'", "a + b + \"cd\";\n", "a + b + \"\";\n");
    expectPrintedNormalAndMangle( "'a' + 'b' + c + d", "\"ab\" + c + d;\n", "\"\" + c + d;\n");
    expectPrintedNormalAndMangle( "(a + '') + (b + '')", "a + (b + \"\");\n", "a + (b + \"\");\n");
    // Make sure identifiers inside "with" statements are kept
    expectPrintedNormalAndMangle( "with (a) []", "with (a) [];\n", "with (a) ;\n");
    expectPrintedNormalAndMangle( "var a; with (b) a", "var a;\nwith (b) a;\n", "var a;\nwith (b) a;\n");
}

TEST(JsParser, TestMangleInlineLocals) {
    auto check = [](const std::string& a, const std::string& b) {
        expectPrintedMangle("function wrapper(arg0, arg1) {" + a + "}",
            "function wrapper(arg0, arg1) {" + ReplaceAll("\n" + b, "\n", "\n  ") + "\n}\n");
    };
    check("var x = 1; return x", "var x = 1;\nreturn x;");
    check("let x = 1; return x", "return 1;");
    check("const x = 1; return x", "return 1;");
    check("using x = 1; return x", "using x = 1;\nreturn x;");
    check("return async () => { await using x = 1; return x }", "return async () => {\n  await using x = 1;\n  return x;\n};");
    check("let x = 1; if (false) x++; return x", "return 1;");
    check("let x = 1; if (true) x++; return x", "let x = 1;\nreturn x++, x;");
    check("let x = 1; return x + x", "let x = 1;\nreturn x + x;");
    // Can substitute into normal unary operators
    check("let x = 1; return +x", "return +1;");
    check("let x = 1; return -x", "return -1;");
    check("let x = 1; return !x", "return !1;");
    check("let x = 1; return ~x", "return ~1;");
    check("let x = 1; return void x", "let x = 1;");
    check("let x = 1; return typeof x", "return typeof 1;");
    // Can substitute into template literals
    check("let x = 1; return `<${x}>`", "return `<1>`;");
    check("let x = 1n; return `<${x}>`", "return `<1>`;");
    check("let x = null; return `<${x}>`", "return `<null>`;");
    check("let x = undefined; return `<${x}>`", "return `<undefined>`;");
    check("let x = false; return `<${x}>`", "return `<false>`;");
    check("let x = true; return `<${x}>`", "return `<true>`;");
    // Check substituting a side-effect free value into normal binary operators
    check("let x = 1; return x + 2", "return 1 + 2;");
    check("let x = 1; return 2 + x", "return 2 + 1;");
    check("let x = 1; return x + arg0", "return 1 + arg0;");
    check("let x = 1; return arg0 + x", "return arg0 + 1;");
    check("let x = 1; return x + fn()", "return 1 + fn();");
    check("let x = 1; return fn() + x", "return fn() + 1;");
    check("let x = 1; return x + undef", "return 1 + undef;");
    check("let x = 1; return undef + x", "return undef + 1;");
    // Check substituting a value with side-effects into normal binary operators
    check("let x = fn(); return x + 2", "return fn() + 2;");
    check("let x = fn(); return 2 + x", "return 2 + fn();");
    check("let x = fn(); return x + arg0", "return fn() + arg0;");
    check("let x = fn(); return arg0 + x", "let x = fn();\nreturn arg0 + x;");
    check("let x = fn(); return x + fn2()", "return fn() + fn2();");
    check("let x = fn(); return fn2() + x", "let x = fn();\nreturn fn2() + x;");
    check("let x = fn(); return x + undef", "return fn() + undef;");
    check("let x = fn(); return undef + x", "let x = fn();\nreturn undef + x;");
    // Cannot substitute into mutating unary operators
    check("let x = 1; ++x", "let x = 1;\n++x;");
    check("let x = 1; --x", "let x = 1;\n--x;");
    check("let x = 1; x++", "let x = 1;\nx++;");
    check("let x = 1; x--", "let x = 1;\nx--;");
    check("let x = 1; delete x", "let x = 1;\ndelete x;");
    // Cannot substitute into mutating binary operators
    check("let x = 1; x = 2", "let x = 1;\nx = 2;");
    check("let x = 1; x += 2", "let x = 1;\nx += 2;");
    check("let x = 1; x ||= 2", "let x = 1;\nx ||= 2;");
    // Can substitute past mutating binary operators when the left operand has no side effects
    check("let x = 1; arg0 = x", "arg0 = 1;");
    check("let x = 1; arg0 += x", "arg0 += 1;");
    check("let x = 1; arg0 ||= x", "arg0 ||= 1;");
    check("let x = fn(); arg0 = x", "arg0 = fn();");
    check("let x = fn(); arg0 += x", "let x = fn();\narg0 += x;");
    check("let x = fn(); arg0 ||= x", "let x = fn();\narg0 ||= x;");
    // Cannot substitute past mutating binary operators when the left operand has side effects
    check("let x = 1; y.z = x", "let x = 1;\ny.z = x;");
    check("let x = 1; y.z += x", "let x = 1;\ny.z += x;");
    check("let x = 1; y.z ||= x", "let x = 1;\ny.z ||= x;");
    check("let x = fn(); y.z = x", "let x = fn();\ny.z = x;");
    check("let x = fn(); y.z += x", "let x = fn();\ny.z += x;");
    check("let x = fn(); y.z ||= x", "let x = fn();\ny.z ||= x;");
    // Can substitute code without side effects into branches
    check("let x = arg0; return x ? y : z;", "return arg0 ? y : z;");
    check("let x = arg0; return arg1 ? x : y;", "return arg1 ? arg0 : y;");
    check("let x = arg0; return arg1 ? y : x;", "return arg1 ? y : arg0;");
    check("let x = arg0; return x || y;", "return arg0 || y;");
    check("let x = arg0; return x && y;", "return arg0 && y;");
    check("let x = arg0; return x ?? y;", "return arg0 ?? y;");
    check("let x = arg0; return arg1 || x;", "return arg1 || arg0;");
    check("let x = arg0; return arg1 && x;", "return arg1 && arg0;");
    check("let x = arg0; return arg1 ?? x;", "return arg1 ?? arg0;");
    // Can substitute code without side effects into branches past an expression with side effects
    check("let x = arg0; return y ? x : z;", "let x = arg0;\nreturn y ? x : z;");
    check("let x = arg0; return y ? z : x;", "let x = arg0;\nreturn y ? z : x;");
    check("let x = arg0; return (arg1 ? 1 : 2) ? x : 3;", "return arg0;");
    check("let x = arg0; return (arg1 ? 1 : 2) ? 3 : x;", "let x = arg0;\nreturn 3;");
    check("let x = arg0; return (arg1 ? y : 1) ? x : 2;", "let x = arg0;\nreturn !arg1 || y ? x : 2;");
    check("let x = arg0; return (arg1 ? 1 : y) ? x : 2;", "let x = arg0;\nreturn arg1 || y ? x : 2;");
    check("let x = arg0; return (arg1 ? y : 1) ? 2 : x;", "let x = arg0;\nreturn !arg1 || y ? 2 : x;");
    check("let x = arg0; return (arg1 ? 1 : y) ? 2 : x;", "let x = arg0;\nreturn arg1 || y ? 2 : x;");
    check("let x = arg0; return y || x;", "let x = arg0;\nreturn y || x;");
    check("let x = arg0; return y && x;", "let x = arg0;\nreturn y && x;");
    check("let x = arg0; return y ?? x;", "let x = arg0;\nreturn y ?? x;");
    // Cannot substitute code with side effects into branches
    check("let x = fn(); return x ? arg0 : y;", "return fn() ? arg0 : y;");
    check("let x = fn(); return arg0 ? x : y;", "let x = fn();\nreturn arg0 ? x : y;");
    check("let x = fn(); return arg0 ? y : x;", "let x = fn();\nreturn arg0 ? y : x;");
    check("let x = fn(); return x || arg0;", "return fn() || arg0;");
    check("let x = fn(); return x && arg0;", "return fn() && arg0;");
    check("let x = fn(); return x ?? arg0;", "return fn() ?? arg0;");
    check("let x = fn(); return arg0 || x;", "let x = fn();\nreturn arg0 || x;");
    check("let x = fn(); return arg0 && x;", "let x = fn();\nreturn arg0 && x;");
    check("let x = fn(); return arg0 ?? x;", "let x = fn();\nreturn arg0 ?? x;");
    // Test chaining
    check("let x = fn(); let y = x[prop]; let z = y.val; throw z", "throw fn()[prop].val;");
    check("let x = fn(), y = x[prop], z = y.val; throw z", "throw fn()[prop].val;");
    // Can substitute an initializer with side effects
    check("let x = 0; let y = ++x; return y",
        "let x = 0;\nreturn ++x;");
    // Can substitute an initializer without side effects past an expression without side effects
    check("let x = 0; let y = x; return [x, y]",
        "let x = 0;\nreturn [x, x];");
    // Cannot substitute an initializer with side effects past an expression without side effects
    check("let x = 0; let y = ++x; return [x, y]",
        "let x = 0, y = ++x;\nreturn [x, y];");
    // Cannot substitute an initializer without side effects past an expression with side effects
    check("let x = 0; let y = {valueOf() { x = 1 }}; let z = x; return [y == 1, z]",
        "let x = 0, y = { valueOf() {\n  x = 1;\n} }, z = x;\nreturn [y == 1, z];");
    // Cannot inline past a spread operator, since that evaluates code
    check("let x = arg0; return [...x];", "return [...arg0];");
    check("let x = arg0; return [x, ...arg1];", "return [arg0, ...arg1];");
    check("let x = arg0; return [...arg1, x];", "let x = arg0;\nreturn [...arg1, x];");
    check("let x = arg0; return arg1(...x);", "return arg1(...arg0);");
    check("let x = arg0; return arg1(x, ...arg1);", "return arg1(arg0, ...arg1);");
    check("let x = arg0; return arg1(...arg1, x);", "let x = arg0;\nreturn arg1(...arg1, x);");
    // Test various statement kinds
    check("let x = arg0; arg1(x);", "arg1(arg0);");
    check("let x = arg0; throw x;", "throw arg0;");
    check("let x = arg0; return x;", "return arg0;");
    check("let x = arg0; if (x) return 1;", "if (arg0) return 1;");
    check("let x = arg0; switch (x) { case 0: return 1; }", "if (arg0 === 0)\n  return 1;");
    check("let x = arg0; let y = x; return y + y;", "let y = arg0;\nreturn y + y;");
    // Loops must not be substituted into because they evaluate multiple times
    check("let x = arg0; do {} while (x);", "let x = arg0;\ndo\n  ;\nwhile (x);");
    check("let x = arg0; while (x) return 1;", "let x = arg0;\nfor (; x; ) return 1;");
    check("let x = arg0; for (; x; ) return 1;", "let x = arg0;\nfor (; x; ) return 1;");
    // Can substitute an expression without side effects into a branch due to optional chaining
    check("let x = arg0; return arg1?.[x];", "return arg1?.[arg0];");
    check("let x = arg0; return arg1?.(x);", "return arg1?.(arg0);");
    // Cannot substitute an expression with side effects into a branch due to optional chaining,
    // since that would change the expression with side effects from being unconditionally
    // evaluated to being conditionally evaluated, which is a behavior change
    check("let x = fn(); return arg1?.[x];", "let x = fn();\nreturn arg1?.[x];");
    check("let x = fn(); return arg1?.(x);", "let x = fn();\nreturn arg1?.(x);");
    // Can substitute an expression past an optional chaining operation, since it has side effects
    check("let x = arg0; return arg1?.a === x;", "let x = arg0;\nreturn arg1?.a === x;");
    check("let x = arg0; return arg1?.[0] === x;", "let x = arg0;\nreturn arg1?.[0] === x;");
    check("let x = arg0; return arg1?.(0) === x;", "let x = arg0;\nreturn arg1?.(0) === x;");
    check("let x = arg0; return arg1?.a[x];", "let x = arg0;\nreturn arg1?.a[x];");
    check("let x = arg0; return arg1?.a(x);", "let x = arg0;\nreturn arg1?.a(x);");
    check("let x = arg0; return arg1?.[a][x];", "let x = arg0;\nreturn arg1?.[a][x];");
    check("let x = arg0; return arg1?.[a](x);", "let x = arg0;\nreturn arg1?.[a](x);");
    check("let x = arg0; return arg1?.(a)[x];", "let x = arg0;\nreturn arg1?.(a)[x];");
    check("let x = arg0; return arg1?.(a)(x);", "let x = arg0;\nreturn arg1?.(a)(x);");
    // Can substitute into an object as long as there are no side effects
    // beforehand. Note that computed properties must call "toString()" which
    // can have side effects.
    check("let x = arg0; return {x};", "return { x: arg0 };");
    check("let x = arg0; return {x: y, y: x};", "let x = arg0;\nreturn { x: y, y: x };");
    check("let x = arg0; return {x: arg1, y: x};", "return { x: arg1, y: arg0 };");
    check("let x = arg0; return {[x]: 0};", "return { [arg0]: 0 };");
    check("let x = arg0; return {[y]: x};", "let x = arg0;\nreturn { [y]: x };");
    check("let x = arg0; return {[arg1]: x};", "let x = arg0;\nreturn { [arg1]: x };");
    check("let x = arg0; return {y() {}, x};", "return { y() {\n}, x: arg0 };");
    check("let x = arg0; return {[y]() {}, x};", "let x = arg0;\nreturn { [y]() {\n}, x };");
    check("let x = arg0; return {...x};", "return { ...arg0 };");
    check("let x = arg0; return {...x, y};", "return { ...arg0, y };");
    check("let x = arg0; return {x, ...y};", "return { x: arg0, ...y };");
    check("let x = arg0; return {...y, x};", "let x = arg0;\nreturn { ...y, x };");
    // Check substitutions into template literals
    check("let x = arg0; return `a${x}b${y}c`;", "return `a${arg0}b${y}c`;");
    check("let x = arg0; return `a${y}b${x}c`;", "let x = arg0;\nreturn `a${y}b${x}c`;");
    check("let x = arg0; return `a${arg1}b${x}c`;", "return `a${arg1}b${arg0}c`;");
    check("let x = arg0; return x`y`;", "return arg0`y`;");
    check("let x = arg0; return y`a${x}b`;", "let x = arg0;\nreturn y`a${x}b`;");
    check("let x = arg0; return arg1`a${x}b`;", "return arg1`a${arg0}b`;");
    check("let x = 'x'; return `a${x}b`;", "return `axb`;");
    // Check substitutions into import expressions
    check("let x = arg0; return import(x);", "return import(arg0);");
    check("let x = arg0; return [import(y), x];", "let x = arg0;\nreturn [import(y), x];");
    check("let x = arg0; return [import(arg1), x];", "return [import(arg1), arg0];");
    // Check substitutions into await expressions
    check("return async () => { let x = arg0; await x; };", "return async () => {\n  await arg0;\n};");
    check("return async () => { let x = arg0; await y; return x; };", "return async () => {\n  let x = arg0;\n  return await y, x;\n};");
    check("return async () => { let x = arg0; await arg1; return x; };", "return async () => {\n  let x = arg0;\n  return await arg1, x;\n};");
    // Check substitutions into yield expressions
    check("return function* () { let x = arg0; yield x; };", "return function* () {\n  yield arg0;\n};");
    check("return function* () { let x = arg0; yield; return x; };", "return function* () {\n  let x = arg0;\n  return yield, x;\n};");
    check("return function* () { let x = arg0; yield y; return x; };", "return function* () {\n  let x = arg0;\n  return yield y, x;\n};");
    check("return function* () { let x = arg0; yield arg1; return x; };", "return function* () {\n  let x = arg0;\n  return yield arg1, x;\n};");
    // Make sure that transforms which duplicate identifiers cause
    // them to no longer be considered single-use identifiers
    expectPrintedMangleTarget( 2015, "(x => { let y = x; throw y ?? z })()", "((x) => {\n  let y = x;\n  throw y != null ? y : z;\n})();\n");
    expectPrintedMangleTarget( 2015, "(x => { let y = x; y.z \?\?= z })()", "((x) => {\n  var _a;\n  let y = x;\n  (_a = y.z) != null || (y.z = z);\n})();\n");
    expectPrintedMangleTarget( 2015, "(x => { let y = x; y?.z })()", "((x) => {\n  let y = x;\n  y == null || y.z;\n})();\n");
    // Cannot substitute into call targets when it would change "this"
    check("let x = arg0; x()", "arg0();");
    check("let x = arg0; (0, x)()", "arg0();");
    check("let x = arg0.foo; x.bar()", "arg0.foo.bar();");
    check("let x = arg0.foo; x[bar]()", "arg0.foo[bar]();");
    check("let x = arg0.foo; x()", "let x = arg0.foo;\nx();");
    check("let x = arg0[foo]; x()", "let x = arg0[foo];\nx();");
    check("let x = arg0?.foo; x()", "let x = arg0?.foo;\nx();");
    check("let x = arg0?.[foo]; x()", "let x = arg0?.[foo];\nx();");
    check("let x = arg0.foo; (0, x)()", "let x = arg0.foo;\nx();");
    check("let x = arg0[foo]; (0, x)()", "let x = arg0[foo];\nx();");
    check("let x = arg0?.foo; (0, x)()", "let x = arg0?.foo;\nx();");
    check("let x = arg0?.[foo]; (0, x)()", "let x = arg0?.[foo];\nx();");
    // Explicitly allow reordering calls that are both marked as "/* @__PURE__ */".
    // This happens because only two expressions that are free from side-effects
    // can be freely reordered, and marking something as "/* @__PURE__ */" tells
    // us that it has no side effects.
    check("let x = arg0(); arg1() + x", "let x = arg0();\narg1() + x;");
    check("let x = arg0(); /* @__PURE__ */ arg1() + x", "let x = arg0();\n/* @__PURE__ */ arg1() + x;");
    check("let x = /* @__PURE__ */ arg0(); arg1() + x", "let x = /* @__PURE__ */ arg0();\narg1() + x;");
    check("let x = /* @__PURE__ */ arg0(); /* @__PURE__ */ arg1() + x", "/* @__PURE__ */ arg1() + /* @__PURE__ */ arg0();");
}

TEST(JsParser, TestTrimCodeInDeadControlFlow) {
    expectPrintedMangle( "if (1) a(); else { ; }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { b() }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { const b = c }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { let b }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { throw b }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { return b }", "a();\n");
    expectPrintedMangle( "b: { if (x) a(); else { break b } }", "b:\n  if (x) a();\n  else\n    break b;\n");
    expectPrintedMangle( "b: { if (1) a(); else { break b } }", "a();\n");
    expectPrintedMangle( "b: { if (0) a(); else { break b } }", "");
    expectPrintedMangle( "b: while (1) if (x) a(); else { continue b }", "b: for (; ; ) if (x) a();\nelse\n  continue b;\n");
    expectPrintedMangle( "b: while (1) if (1) a(); else { continue b }", "for (; ; ) a();\n");
    expectPrintedMangle( "b: while (1) if (0) a(); else { continue b }", "b: for (; ; ) continue b;\n");
    expectPrintedMangle( "if (1) a(); else { class b {} }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { debugger }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { switch (1) { case 1: b() } }", "a();\n");
    expectPrintedMangle( "if (0) {let a = 1} else a()", "a();\n");
    expectPrintedMangle( "if (1) {let a = 1} else a()", "{\n  let a = 1;\n}\n");
    expectPrintedMangle( "if (0) a(); else {let a = 1}", "{\n  let a = 1;\n}\n");
    expectPrintedMangle( "if (1) a(); else {let a = 1}", "a();\n");
    expectPrintedMangle( "if (1) a(); else { var a = b }", "if (1) a();\nelse\n  var a;\n");
    expectPrintedMangle( "if (1) a(); else { var [a] = b }", "if (1) a();\nelse\n  var a;\n");
    expectPrintedMangle( "if (1) a(); else { var {x: a} = b }", "if (1) a();\nelse\n  var a;\n");
    expectPrintedMangle( "if (1) a(); else { var [] = b }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { var {} = b }", "a();\n");
    expectPrintedMangle( "if (1) a(); else { function a() {} }", "if (1) a();\nelse\n  var a;\n");
    expectPrintedMangle( "if (1) a(); else { for(;;){var a} }", "if (1) a();\nelse\n  for (; ; )\n    var a;\n");
    expectPrintedMangle( "if (1) { a(); b() } else { var a; var b; }", "if (1)\n  a(), b();\nelse\n  var a, b;\n");
    expectPrintedMangle( "if (1) a(); else { switch (1) { case 1: case 2: var a } }", "if (1) a();\nelse\n  var a;\n");
    expectPrintedMangle( "return 'foo'; try { return 'bar' } catch {}", "return \"foo\";\n");
    expectPrintedMangle( "return foo = true; try { var foo } catch {}", "return foo = true;\ntry {\n  var foo;\n} catch {\n}\n");
    expectPrintedMangle( "return foo = true; try {} catch { var foo }", "return foo = true;\ntry {\n} catch {\n  var foo;\n}\n");
    expectPrintedMangle( "\n"
        "\t\tasync function test() {\n"
        "\t\t\tif (true) return { status: \"disabled_for_development\" };\n"
        "\t\t\ttry {\n"
        "\t\t\t\tconst response = await httpClients.releasesApi.get();\n"
        "\t\t\t\tif (!response.ok) return { status: \"no_release_found\" };\n"
        "\t\t\t\tif (response.statusCode === 204) return { status: \"up_to_date\" };\n"
        "\t\t\t} catch (error) {\n"
        "\t\t\t\treturn { status: \"no_release_found\" };\n"
        "\t\t\t}\n"
        "\t\t\treturn { status: \"downloading\" };\n"
        "\t\t}\n"
        "\t", "async function test() {\n  return { status: \"disabled_for_development\" };\n}\n");
}

TEST(JsParser, TestPreservedComments) {
    expectPrinted( "//", "");
    expectPrinted( "//preserve", "");
    expectPrinted( "//@__PURE__", "");
    expectPrinted( "//!", "//!\n");
    expectPrinted( "//@license", "//@license\n");
    expectPrinted( "//@preserve", "//@preserve\n");
    expectPrinted( "// @license", "// @license\n");
    expectPrinted( "// @preserve", "// @preserve\n");
    expectPrinted( "/**/", "");
    expectPrinted( "/*preserve*/", "");
    expectPrinted( "/*@__PURE__*/", "");
    expectPrinted( "/*!*/", "/*!*/\n");
    expectPrinted( "/*@license*/", "/*@license*/\n");
    expectPrinted( "/*@preserve*/", "/*@preserve*/\n");
    expectPrinted( "/*\n * @license\n */", "/*\n * @license\n */\n");
    expectPrinted( "/*\n * @preserve\n */", "/*\n * @preserve\n */\n");
    expectPrinted( "foo() //! test", "foo();\n//! test\n");
    expectPrinted( "//! test\nfoo()", "//! test\nfoo();\n");
    expectPrinted( "if (1) //! test\nfoo()", "if (1)\n  foo();\n");
    expectPrinted( "if (1) {//! test\nfoo()}", "if (1) {\n  //! test\n  foo();\n}\n");
    expectPrinted( "if (1) {foo() //! test\n}", "if (1) {\n  foo();\n  //! test\n}\n");
    expectPrinted( "    /*!\r     * Re-indent test\r     */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "    /*!\n     * Re-indent test\n     */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "    /*!\r\n     * Re-indent test\r\n     */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "    /*!\u2028     * Re-indent test\u2028     */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "    /*!\u2029     * Re-indent test\u2029     */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "\t\t/*!\r\t\t * Re-indent test\r\t\t */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "\t\t/*!\n\t\t * Re-indent test\n\t\t */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "\t\t/*!\r\n\t\t * Re-indent test\r\n\t\t */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "\t\t/*!\u2028\t\t * Re-indent test\u2028\t\t */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "\t\t/*!\u2029\t\t * Re-indent test\u2029\t\t */", "/*!\n * Re-indent test\n */\n");
    expectPrinted( "x\r    /*!\r     * Re-indent test\r     */", "x;\n/*!\n * Re-indent test\n */\n");
    expectPrinted( "x\n    /*!\n     * Re-indent test\n     */", "x;\n/*!\n * Re-indent test\n */\n");
    expectPrinted( "x\r\n    /*!\r\n     * Re-indent test\r\n     */", "x;\n/*!\n * Re-indent test\n */\n");
    expectPrinted( "x\u2028    /*!\u2028     * Re-indent test\u2028     */", "x;\n/*!\n * Re-indent test\n */\n");
    expectPrinted( "x\u2029    /*!\u2029     * Re-indent test\u2029     */", "x;\n/*!\n * Re-indent test\n */\n");
}

TEST(JsParser, TestUnicodeWhitespace) {
    const std::vector<std::string> whitespace = {
        "\u0009", // character tabulation
        "\u000B", // line tabulation
        "\u000C", // form feed
        "\u0020", // space
        "\u00A0", // no-break space
        "\u1680", // ogham space mark
        "\u2000", // en quad
        "\u2001", // em quad
        "\u2002", // en space
        "\u2003", // em space
        "\u2004", // three-per-em space
        "\u2005", // four-per-em space
        "\u2006", // six-per-em space
        "\u2007", // figure space
        "\u2008", // punctuation space
        "\u2009", // thin space
        "\u200A", // hair space
        "\u202F", // narrow no-break space
        "\u205F", // medium mathematical space
        "\u3000", // ideographic space
        "\uFEFF", // zero width non-breaking space
        };
    // Test "js_lexer.Next()"
    expectParseError( "var\u0008x", "<stdin>: ERROR: Expected identifier but found \"\\b\"\n");
    for (const std::string& s : whitespace) {
        expectPrinted( "var"+s+"x", "var x;\n");
    }
    // Test "js_lexer.NextInsideJSXElement()"
    expectParseErrorJSX( "<x\u0008y/>", "<stdin>: ERROR: Expected \">\" but found \"\\b\"\n");
    for (const std::string& s : whitespace) {
        expectPrintedJSX( "<x"+s+"y/>", "<x y />;\n", "/* @__PURE__ */ React.createElement(\"x\", { y: true });\n");
    }
    // Test "js_lexer.NextJSXElementChild()"
    expectPrintedJSX( "<x>\n\u0008\n</x>", "<x>\n\u0008\n</x>;\n", "/* @__PURE__ */ React.createElement(\"x\", null, \"\\b\");\n");
    for (const std::string& s : whitespace) {
        expectPrintedJSX( "<x>\n"+s+"\n</x>", "<x>\n"+s+"\n</x>;\n", "/* @__PURE__ */ React.createElement(\"x\", null);\n");
    }
    // Test "fixWhitespaceAndDecodeJSXEntities()"
    expectPrintedJSX( "<x>\n\u0008&quot;\n</x>", "<x>\n\u0008&quot;\n</x>;\n", "/* @__PURE__ */ React.createElement(\"x\", null, '\\b\"');\n");
    for (const std::string& s : whitespace) {
        expectPrintedJSX( "<x>\n"+s+"&quot;\n</x>", "<x>\n"+s+"&quot;\n</x>;\n", "/* @__PURE__ */ React.createElement(\"x\", null, '\"');\n");
    }
    const std::vector<std::string> invalidWhitespaceInJS = {
        "\u0085", // next line (nel)
        };
    // Test "js_lexer.Next()"
    for (const std::string& s : invalidWhitespaceInJS) {
        const int r = DecodeWTF8Rune(s);
        expectParseError( "var"+s+"x", SprintfReplace("<stdin>: ERROR: Expected identifier but found \"\\u%04x\"\n", r));
    }
    // Test "js_lexer.NextInsideJSXElement()"
    for (const std::string& s : invalidWhitespaceInJS) {
        const int r = DecodeWTF8Rune(s);
        expectParseErrorJSX( "<x"+s+"y/>", SprintfReplace("<stdin>: ERROR: Expected \">\" but found \"\\u%04x\"\n", r));
    }
    // Test "js_lexer.NextJSXElementChild()"
    for (const std::string& s : invalidWhitespaceInJS) {
        expectPrintedJSX( "<x>\n"+s+"\n</x>", "<x>\n"+s+"\n</x>;\n", "/* @__PURE__ */ React.createElement(\"x\", null, \""+s+"\");\n");
    }
    // Test "fixWhitespaceAndDecodeJSXEntities()"
    for (const std::string& s : invalidWhitespaceInJS) {
        expectPrintedJSX( "<x>\n"+s+"&quot;\n</x>", "<x>\n"+s+"&quot;\n</x>;\n", "/* @__PURE__ */ React.createElement(\"x\", null, '"+s+"\"');\n");
    }
}

// Make sure we can handle the unicode replacement character "�" in various places
TEST(JsParser, TestReplacementCharacter) {
    expectPrinted( "//\uFFFD\n123", "123;\n");
    expectPrinted( "/*\uFFFD*/123", "123;\n");
    expectPrinted( "'\uFFFD'", "\"\uFFFD\";\n");
    expectPrinted( "\"\uFFFD\"", "\"\uFFFD\";\n");
    expectPrinted( "`\uFFFD`", "`\uFFFD`;\n");
    expectPrinted( "/\uFFFD/", "/\uFFFD/;\n");
    expectPrintedJSX( "<a>\uFFFD</a>", "<a>\uFFFD</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"\uFFFD\");\n");
}

TEST(JsParser, TestNewTarget) {
    expectPrinted( "function f() { new.target }", "function f() {\n  new.target;\n}\n");
    expectPrinted( "function f() { (new.target) }", "function f() {\n  new.target;\n}\n");
    expectPrinted( "function f() { () => new.target }", "function f() {\n  () => new.target;\n}\n");
    expectPrinted( "class Foo { x = new.target }", "class Foo {\n  x = new.target;\n}\n");
    expectParseError( "new.t\\u0061rget", "<stdin>: ERROR: Unexpected \"t\\\\u0061rget\"\n");
    expectParseError( "new.target", "<stdin>: ERROR: Cannot use \"new.target\" here:\n");
    expectParseError( "() => new.target", "<stdin>: ERROR: Cannot use \"new.target\" here:\n");
    expectParseError( "class Foo { [new.target] }", "<stdin>: ERROR: Cannot use \"new.target\" here:\n");
}

TEST(JsParser, TestJSX) {
    expectParseErrorJSX( "<div>></div>",
        "<stdin>: WARNING: The character \">\" is not valid inside a JSX element\n" "NOTE: Did you mean to escape it as \"{'>'}\" instead?\n");
    expectParseErrorJSX( "<div>{1}}</div>",
        "<stdin>: WARNING: The character \"}\" is not valid inside a JSX element\n" "NOTE: Did you mean to escape it as \"{'}'}\" instead?\n");
    expectPrintedJSX( "<div>></div>", "<div>></div>;\n", "/* @__PURE__ */ React.createElement(\"div\", null, \">\");\n");
    expectPrintedJSX( "<div>{1}}</div>", "<div>{1}}</div>;\n", "/* @__PURE__ */ React.createElement(\"div\", null, 1, \"}\");\n");
    expectParseError( "<a/>", "<stdin>: ERROR: The JSX syntax extension is not currently enabled\n" "NOTE: The guchho loader for this file is currently set to \"js\" but it must be set to \"jsx\" to be able to parse JSX syntax. " "You can use '--loader:.js=jsx' to do that.\n");
    expectPrintedJSX( "<a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a></a>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<A/>", "<A />;\n", "/* @__PURE__ */ React.createElement(A, null);\n");
    expectPrintedJSX( "<a.b/>", "<a.b />;\n", "/* @__PURE__ */ React.createElement(a.b, null);\n");
    expectPrintedJSX( "<_a/>", "<_a />;\n", "/* @__PURE__ */ React.createElement(_a, null);\n");
    expectPrintedJSX( "<a-b/>", "<a-b />;\n", "/* @__PURE__ */ React.createElement(\"a-b\", null);\n");
    expectPrintedJSX( "<a0/>", "<a0 />;\n", "/* @__PURE__ */ React.createElement(\"a0\", null);\n");
    expectParseErrorJSX( "<0a/>", "<stdin>: ERROR: Expected identifier but found \"0\"\n");
    expectPrintedJSX( "<a b/>", "<a b />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: true });\n");
    expectPrintedJSX( "<a b=\"\\\"/>", "<a b=\"\\\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"\\\\\" });\n");
    expectPrintedJSX( "<a b=\"<>\"/>", "<a b=\"<>\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"<>\" });\n");
    expectPrintedJSX( "<a b=\"&lt;&gt;\"/>", "<a b=\"&lt;&gt;\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"<>\" });\n");
    expectPrintedJSX( "<a b=\"&wrong;\"/>", "<a b=\"&wrong;\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"&wrong;\" });\n");
    expectPrintedJSX( "<a b={1, 2}/>", "<a b={(1, 2)} />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: (1, 2) });\n");
    expectPrintedJSX( "<a b={<c/>}/>", "<a b={<c />} />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: /* @__PURE__ */ React.createElement(\"c\", null) });\n");
    expectPrintedJSX( "<a {...props}/>", "<a {...props} />;\n", "/* @__PURE__ */ React.createElement(\"a\", { ...props });\n");
    expectPrintedJSX( "<a b=\"🙂\"/>", "<a b=\"🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂\" });\n");
    expectPrintedJSX( "<a>\n</a>", "<a>\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a>123</a>", "<a>123</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"123\");\n");
    expectPrintedJSX( "<a>}</a>", "<a>}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"}\");\n");
    expectPrintedJSX( "<a>=</a>", "<a>=</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"=\");\n");
    expectPrintedJSX( "<a>></a>", "<a>></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \">\");\n");
    expectPrintedJSX( "<a>>=</a>", "<a>>=</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \">=\");\n");
    expectPrintedJSX( "<a>>></a>", "<a>>></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \">>\");\n");
    expectPrintedJSX( "<a>{}</a>", "<a>{}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a>{/* comment */}</a>", "<a>{\n  /* comment */\n}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a>b{}</a>", "<a>b{}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b{/* comment */}</a>", "<a>b{\n  /* comment */\n}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>{}c</a>", "<a>{}c</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"c\");\n");
    expectPrintedJSX( "<a>{/* comment */}c</a>", "<a>{\n  /* comment */\n}c</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"c\");\n");
    expectPrintedJSX( "<a>b{}c</a>", "<a>b{}c</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\", \"c\");\n");
    expectPrintedJSX( "<a>b{/* comment */}c</a>", "<a>b{\n  /* comment */\n}c</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\", \"c\");\n");
    expectPrintedJSX( "<a>{1, 2}</a>", "<a>{(1, 2)}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, (1, 2));\n");
    expectPrintedJSX( "<a>&lt;&gt;</a>", "<a>&lt;&gt;</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"<>\");\n");
    expectPrintedJSX( "<a>&wrong;</a>", "<a>&wrong;</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"&wrong;\");\n");
    expectPrintedJSX( "<a>🙂</a>", "<a>🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    expectPrintedJSX( "<a>{...children}</a>", "<a>{...children}</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, ...children);\n");
    // Note: The TypeScript compiler and Babel disagree. This matches TypeScript.
    expectPrintedJSX( "<a b=\"   c\"/>", "<a b=\"   c\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   c\" });\n");
    expectPrintedJSX( "<a b=\"   \nc\"/>", "<a b=\"   \nc\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   \\nc\" });\n");
    expectPrintedJSX( "<a b=\"\n   c\"/>", "<a b=\"\n   c\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"\\n   c\" });\n");
    expectPrintedJSX( "<a b=\"c   \"/>", "<a b=\"c   \" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"c   \" });\n");
    expectPrintedJSX( "<a b=\"c   \n\"/>", "<a b=\"c   \n\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"c   \\n\" });\n");
    expectPrintedJSX( "<a b=\"c\n   \"/>", "<a b=\"c\n   \" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"c\\n   \" });\n");
    expectPrintedJSX( "<a b=\"c   d\"/>", "<a b=\"c   d\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"c   d\" });\n");
    expectPrintedJSX( "<a b=\"c   \nd\"/>", "<a b=\"c   \nd\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"c   \\nd\" });\n");
    expectPrintedJSX( "<a b=\"c\n   d\"/>", "<a b=\"c\n   d\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"c\\n   d\" });\n");
    expectPrintedJSX( "<a b=\"   c\"/>", "<a b=\"   c\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   c\" });\n");
    expectPrintedJSX( "<a b=\"   \nc\"/>", "<a b=\"   \nc\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   \\nc\" });\n");
    expectPrintedJSX( "<a b=\"\n   c\"/>", "<a b=\"\n   c\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"\\n   c\" });\n");
    // Same test as above except with multi-byte Unicode characters
    expectPrintedJSX( "<a b=\"   🙂\"/>", "<a b=\"   🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   🙂\" });\n");
    expectPrintedJSX( "<a b=\"   \n🙂\"/>", "<a b=\"   \n🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   \\n🙂\" });\n");
    expectPrintedJSX( "<a b=\"\n   🙂\"/>", "<a b=\"\n   🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"\\n   🙂\" });\n");
    expectPrintedJSX( "<a b=\"🙂   \"/>", "<a b=\"🙂   \" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂   \" });\n");
    expectPrintedJSX( "<a b=\"🙂   \n\"/>", "<a b=\"🙂   \n\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂   \\n\" });\n");
    expectPrintedJSX( "<a b=\"🙂\n   \"/>", "<a b=\"🙂\n   \" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂\\n   \" });\n");
    expectPrintedJSX( "<a b=\"🙂   🍕\"/>", "<a b=\"🙂   🍕\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂   🍕\" });\n");
    expectPrintedJSX( "<a b=\"🙂   \n🍕\"/>", "<a b=\"🙂   \n🍕\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂   \\n🍕\" });\n");
    expectPrintedJSX( "<a b=\"🙂\n   🍕\"/>", "<a b=\"🙂\n   🍕\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"🙂\\n   🍕\" });\n");
    expectPrintedJSX( "<a b=\"   🙂\"/>", "<a b=\"   🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   🙂\" });\n");
    expectPrintedJSX( "<a b=\"   \n🙂\"/>", "<a b=\"   \n🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"   \\n🙂\" });\n");
    expectPrintedJSX( "<a b=\"\n   🙂\"/>", "<a b=\"\n   🙂\" />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: \"\\n   🙂\" });\n");
    expectPrintedJSX( "<a>   b</a>", "<a>   b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"   b\");\n");
    expectPrintedJSX( "<a>   \nb</a>", "<a>   \nb</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>\n   b</a>", "<a>\n   b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b   </a>", "<a>b   </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b   \");\n");
    expectPrintedJSX( "<a>b   \n</a>", "<a>b   \n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b\n   </a>", "<a>b\n   </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b   c</a>", "<a>b   c</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b   c\");\n");
    expectPrintedJSX( "<a>b   \nc</a>", "<a>b   \nc</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b c\");\n");
    expectPrintedJSX( "<a>b\n   c</a>", "<a>b\n   c</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b c\");\n");
    expectPrintedJSX( "<a>   b</a>", "<a>   b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"   b\");\n");
    expectPrintedJSX( "<a>   \nb</a>", "<a>   \nb</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>\n   b</a>", "<a>\n   b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    // Same test as above except with multi-byte Unicode characters
    expectPrintedJSX( "<a>   🙂</a>", "<a>   🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"   🙂\");\n");
    expectPrintedJSX( "<a>   \n🙂</a>", "<a>   \n🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    expectPrintedJSX( "<a>\n   🙂</a>", "<a>\n   🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    expectPrintedJSX( "<a>🙂   </a>", "<a>🙂   </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂   \");\n");
    expectPrintedJSX( "<a>🙂   \n</a>", "<a>🙂   \n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    expectPrintedJSX( "<a>🙂\n   </a>", "<a>🙂\n   </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    expectPrintedJSX( "<a>🙂   🍕</a>", "<a>🙂   🍕</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂   🍕\");\n");
    expectPrintedJSX( "<a>🙂   \n🍕</a>", "<a>🙂   \n🍕</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂 🍕\");\n");
    expectPrintedJSX( "<a>🙂\n   🍕</a>", "<a>🙂\n   🍕</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂 🍕\");\n");
    expectPrintedJSX( "<a>   🙂</a>", "<a>   🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"   🙂\");\n");
    expectPrintedJSX( "<a>   \n🙂</a>", "<a>   \n🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    expectPrintedJSX( "<a>\n   🙂</a>", "<a>\n   🙂</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"🙂\");\n");
    // "<a>{x}</b></a>" with all combinations of "", " ", and "\n" inserted in between
    expectPrintedJSX( "<a>{x}<b/></a>;", "<a>{x}<b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>\n{x}<b/></a>;", "<a>\n{x}<b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>{x}\n<b/></a>;", "<a>{x}\n<b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>\n{x}\n<b/></a>;", "<a>\n{x}\n<b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>{x}<b/>\n</a>;", "<a>{x}<b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>\n{x}<b/>\n</a>;", "<a>\n{x}<b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>{x}\n<b/>\n</a>;", "<a>{x}\n<b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>\n{x}\n<b/>\n</a>;", "<a>\n{x}\n<b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a> {x}<b/></a>;", "<a> {x}<b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a> {x}\n<b/></a>;", "<a> {x}\n<b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a> {x}<b/>\n</a>;", "<a> {x}<b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a> {x}\n<b/>\n</a>;", "<a> {x}\n<b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>{x} <b/></a>;", "<a>{x} <b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, \" \", /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>\n{x} <b/></a>;", "<a>\n{x} <b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, \" \", /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>{x} <b/>\n</a>;", "<a>{x} <b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, \" \", /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>\n{x} <b/>\n</a>;", "<a>\n{x} <b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, \" \", /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a> {x} <b/></a>;", "<a> {x} <b /></a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, \" \", /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a> {x} <b/>\n</a>;", "<a> {x} <b />\n</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, \" \", /* @__PURE__ */ React.createElement(\"b\", null));\n");
    expectPrintedJSX( "<a>{x}<b/> </a>;", "<a>{x}<b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a>\n{x}<b/> </a>;", "<a>\n{x}<b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a>{x}\n<b/> </a>;", "<a>{x}\n<b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a>\n{x}\n<b/> </a>;", "<a>\n{x}\n<b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a> {x}<b/> </a>;", "<a> {x}<b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a> {x}\n<b/> </a>;", "<a> {x}\n<b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a>{x} <b/> </a>;", "<a>{x} <b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, \" \", /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a>\n{x} <b/> </a>;", "<a>\n{x} <b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, x, \" \", /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectPrintedJSX( "<a> {x} <b/> </a>;", "<a> {x} <b /> </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" \", x, \" \", /* @__PURE__ */ React.createElement(\"b\", null), \" \");\n");
    expectParseErrorJSX( "<a b=true/>", "<stdin>: ERROR: Expected \"{\" but found \"true\"\n");
    expectParseErrorJSX( "</a>", "<stdin>: ERROR: Expected identifier but found \"/\"\n");
    expectParseErrorJSX( "<></b>",
        "<stdin>: ERROR: Unexpected closing \"b\" tag does not match opening fragment tag\n<stdin>: NOTE: The opening fragment tag is here:\n");
    expectParseErrorJSX( "<a></>",
        "<stdin>: ERROR: Unexpected closing fragment tag does not match opening \"a\" tag\n<stdin>: NOTE: The opening \"a\" tag is here:\n");
    expectParseErrorJSX( "<a></b>",
        "<stdin>: ERROR: Unexpected closing \"b\" tag does not match opening \"a\" tag\n<stdin>: NOTE: The opening \"a\" tag is here:\n");
    expectParseErrorJSX( "<\na\n.\nb\n>\n<\n/\nc\n.\nd\n>",
        "<stdin>: ERROR: Unexpected closing \"c.d\" tag does not match opening \"a.b\" tag\n<stdin>: NOTE: The opening \"a.b\" tag is here:\n");
    expectParseErrorJSX( "<a-b.c>", "<stdin>: ERROR: Expected \">\" but found \".\"\n");
    expectParseErrorJSX( "<a.b-c>", "<stdin>: ERROR: Unexpected \"-\"\n");
    expectPrintedJSX( "< /**/ a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "< //\n a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a /**/ />", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a //\n />", "<a />;\n", "/* @__PURE__ */ React.createElement(\n  \"a\",\n  null\n);\n");
    expectPrintedJSX( "<a/ /**/ >", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a/ //\n >", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a>b< /**/ /a>", "<a>b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b< //\n /a>", "<a>b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b</ /**/ a>", "<a>b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b</ //\n a>", "<a>b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b</a /**/ >", "<a>b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a>b</a //\n >", "<a>b</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"b\");\n");
    expectPrintedJSX( "<a> /**/ </a>", "<a> /**/ </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" /**/ \");\n");
    expectPrintedJSX( "<a> //\n </a>", "<a> //\n </a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \" //\");\n");
    // Unicode tests
    expectPrintedJSX( "<\U00020000/>", "<\U00020000 />;\n", "/* @__PURE__ */ React.createElement(\U00020000, null);\n");
    expectPrintedJSX( "<a>\U00020000</a>", "<a>\U00020000</a>;\n", "/* @__PURE__ */ React.createElement(\"a\", null, \"\U00020000\");\n");
    expectPrintedJSX( "<a \U00020000={0}/>", "<a \U00020000={0} />;\n", "/* @__PURE__ */ React.createElement(\"a\", { \"\U00020000\": 0 });\n");
    // Comment tests
    expectParseErrorJSX( "<a /* />", "<stdin>: ERROR: Expected \"*/\" to terminate multi-line comment\n<stdin>: NOTE: The multi-line comment starts here:\n");
    expectParseErrorJSX( "<a /*/ />", "<stdin>: ERROR: Expected \"*/\" to terminate multi-line comment\n<stdin>: NOTE: The multi-line comment starts here:\n");
    expectParseErrorJSX( "<a // />", "<stdin>: ERROR: Expected \">\" but found end of file\n");
    expectParseErrorJSX( "<a /**/>", "<stdin>: ERROR: Unexpected end of file before a closing \"a\" tag\n<stdin>: NOTE: The opening \"a\" tag is here:\n");
    expectParseErrorJSX( "<a /**/ />", "");
    expectParseErrorJSX( "<a // \n />", "");
    expectParseErrorJSX( "<a b/* />", "<stdin>: ERROR: Expected \"*/\" to terminate multi-line comment\n<stdin>: NOTE: The multi-line comment starts here:\n");
    expectParseErrorJSX( "<a b/*/ />", "<stdin>: ERROR: Expected \"*/\" to terminate multi-line comment\n<stdin>: NOTE: The multi-line comment starts here:\n");
    expectParseErrorJSX( "<a b// />", "<stdin>: ERROR: Expected \">\" but found end of file\n");
    expectParseErrorJSX( "<a b/**/>", "<stdin>: ERROR: Unexpected end of file before a closing \"a\" tag\n<stdin>: NOTE: The opening \"a\" tag is here:\n");
    expectParseErrorJSX( "<a b/**/ />", "");
    expectParseErrorJSX( "<a b// \n />", "");
    // JSX namespaced names
    for (const std::string& colon : std::vector<std::string>{":", " :", ": ", " : "}) {
        expectPrintedJSX( "<a"+colon+"b/>", "<a:b />;\n", "/* @__PURE__ */ React.createElement(\"a:b\", null);\n");
        expectPrintedJSX( "<a-b"+colon+"c-d/>", "<a-b:c-d />;\n", "/* @__PURE__ */ React.createElement(\"a-b:c-d\", null);\n");
        expectPrintedJSX( "<a-"+colon+"b-/>", "<a-:b- />;\n", "/* @__PURE__ */ React.createElement(\"a-:b-\", null);\n");
        expectPrintedJSX( "<Te"+colon+"st/>", "<Te:st />;\n", "/* @__PURE__ */ React.createElement(\"Te:st\", null);\n");
        expectPrintedJSX( "<x a"+colon+"b/>", "<x a:b />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"a:b\": true });\n");
        expectPrintedJSX( "<x a-b"+colon+"c-d/>", "<x a-b:c-d />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"a-b:c-d\": true });\n");
        expectPrintedJSX( "<x a-"+colon+"b-/>", "<x a-:b- />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"a-:b-\": true });\n");
        expectPrintedJSX( "<x Te"+colon+"st/>", "<x Te:st />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"Te:st\": true });\n");
        expectPrintedJSX( "<x a"+colon+"b={0}/>", "<x a:b={0} />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"a:b\": 0 });\n");
        expectPrintedJSX( "<x a-b"+colon+"c-d={0}/>", "<x a-b:c-d={0} />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"a-b:c-d\": 0 });\n");
        expectPrintedJSX( "<x a-"+colon+"b-={0}/>", "<x a-:b-={0} />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"a-:b-\": 0 });\n");
        expectPrintedJSX( "<x Te"+colon+"st={0}/>", "<x Te:st={0} />;\n", "/* @__PURE__ */ React.createElement(\"x\", { \"Te:st\": 0 });\n");
        expectPrintedJSX( "<a-b a-b={a-b}/>", "<a-b a-b={a - b} />;\n", "/* @__PURE__ */ React.createElement(\"a-b\", { \"a-b\": a - b });\n");
        expectParseErrorJSX( "<x"+colon+"/>", "<stdin>: ERROR: Expected identifier after \"x:\" in namespaced JSX name\n");
        expectParseErrorJSX( "<x"+colon+"y"+colon+"/>", "<stdin>: ERROR: Expected \">\" but found \":\"\n");
        expectParseErrorJSX( "<x"+colon+"0y/>", "<stdin>: ERROR: Expected identifier after \"x:\" in namespaced JSX name\n");
    }
    // JSX elements as JSX attribute values
    expectPrintedJSX( "<a b=<c/>/>", "<a b=<c /> />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: /* @__PURE__ */ React.createElement(\"c\", null) });\n");
    expectPrintedJSX( "<a b=<></>/>", "<a b=<></> />;\n", "/* @__PURE__ */ React.createElement(\"a\", { b: /* @__PURE__ */ React.createElement(React.Fragment, null) });\n");
    expectParseErrorJSX( "<a b=</a>/>", "<stdin>: ERROR: Expected identifier but found \"/\"\n");
    expectParseErrorJSX( "<a b=<>/>",
        "<stdin>: WARNING: The character \">\" is not valid inside a JSX element\nNOTE: Did you mean to escape it as \"{'>'}\" instead?\n" "<stdin>: ERROR: Unexpected end of file before a closing fragment tag\n<stdin>: NOTE: The opening fragment tag is here:\n");
    expectParseErrorJSX( "<a b=<c>></a>",
        "<stdin>: WARNING: The character \">\" is not valid inside a JSX element\nNOTE: Did you mean to escape it as \"{'>'}\" instead?\n" "<stdin>: ERROR: Unexpected closing \"a\" tag does not match opening \"c\" tag\n<stdin>: NOTE: The opening \"c\" tag is here:\n" "<stdin>: ERROR: Expected \">\" but found end of file\n");
    expectParseErrorJSX( "<a b=<c>/>",
        "<stdin>: WARNING: The character \">\" is not valid inside a JSX element\nNOTE: Did you mean to escape it as \"{'>'}\" instead?\n" "<stdin>: ERROR: Unexpected end of file before a closing \"c\" tag\n<stdin>: NOTE: The opening \"c\" tag is here:\n");
}

TEST(JsParser, TestJSXSingleLine) {
    expectPrintedJSX( "<x/>", "<x />;\n", "/* @__PURE__ */ React.createElement(\"x\", null);\n");
    expectPrintedJSX( "<x y/>", "<x y />;\n", "/* @__PURE__ */ React.createElement(\"x\", { y: true });\n");
    expectPrintedJSX( "<x\n/>", "<x />;\n", "/* @__PURE__ */ React.createElement(\n  \"x\",\n  null\n);\n");
    expectPrintedJSX( "<x\ny/>", "<x\n  y\n/>;\n", "/* @__PURE__ */ React.createElement(\n  \"x\",\n  {\n    y: true\n  }\n);\n");
    expectPrintedJSX( "<x y\n/>", "<x\n  y\n/>;\n", "/* @__PURE__ */ React.createElement(\n  \"x\",\n  {\n    y: true\n  }\n);\n");
    expectPrintedJSX( "<x\n{...y}/>", "<x\n  {...y}\n/>;\n", "/* @__PURE__ */ React.createElement(\n  \"x\",\n  {\n    ...y\n  }\n);\n");
}

TEST(JsParser, TestJSXPragmas) {
    expectPrintedJSX( "// @jsx h\n<a/>", "<a />;\n", "/* @__PURE__ */ h(\"a\", null);\n");
    expectPrintedJSX( "/*@jsx h*/\n<a/>", "<a />;\n", "/* @__PURE__ */ h(\"a\", null);\n");
    expectPrintedJSX( "/* @jsx h */\n<a/>", "<a />;\n", "/* @__PURE__ */ h(\"a\", null);\n");
    expectPrintedJSX( "<a/>\n// @jsx h", "<a />;\n", "/* @__PURE__ */ h(\"a\", null);\n");
    expectPrintedJSX( "<a/>\n/*@jsx h*/", "<a />;\n", "/* @__PURE__ */ h(\"a\", null);\n");
    expectPrintedJSX( "<a/>\n/* @jsx h */", "<a />;\n", "/* @__PURE__ */ h(\"a\", null);\n");
    expectPrintedJSX( "// @jsx a.b.c\n<a/>", "<a />;\n", "/* @__PURE__ */ a.b.c(\"a\", null);\n");
    expectPrintedJSX( "/*@jsx a.b.c*/\n<a/>", "<a />;\n", "/* @__PURE__ */ a.b.c(\"a\", null);\n");
    expectPrintedJSX( "/* @jsx a.b.c */\n<a/>", "<a />;\n", "/* @__PURE__ */ a.b.c(\"a\", null);\n");
    expectPrintedJSX( "// @jsxFrag f\n<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(f, null);\n");
    expectPrintedJSX( "/*@jsxFrag f*/\n<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(f, null);\n");
    expectPrintedJSX( "/* @jsxFrag f */\n<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(f, null);\n");
    expectPrintedJSX( "<></>\n// @jsxFrag f", "<></>;\n", "/* @__PURE__ */ React.createElement(f, null);\n");
    expectPrintedJSX( "<></>\n/*@jsxFrag f*/", "<></>;\n", "/* @__PURE__ */ React.createElement(f, null);\n");
    expectPrintedJSX( "<></>\n/* @jsxFrag f */", "<></>;\n", "/* @__PURE__ */ React.createElement(f, null);\n");
    expectPrintedJSX( "// @jsxFrag a.b.c\n<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(a.b.c, null);\n");
    expectPrintedJSX( "/*@jsxFrag a.b.c*/\n<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(a.b.c, null);\n");
    expectPrintedJSX( "/* @jsxFrag a.b.c */\n<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(a.b.c, null);\n");
}

TEST(JsParser, TestJSXAutomatic) {
    // Prod, without runtime imports
    JSXAutomaticTestOptions p{false, "", true, false};
    expectPrintedJSXAutomatic( p, "<div>></div>", "/* @__PURE__ */ jsx(\"div\", { children: \">\" });\n");
    expectPrintedJSXAutomatic( p, "<div>{1}}</div>", "/* @__PURE__ */ jsxs(\"div\", { children: [\n  1,\n  \"}\"\n] });\n");
    expectPrintedJSXAutomatic( p, "<div key={true} />", "/* @__PURE__ */ jsx(\"div\", {}, true);\n");
    expectPrintedJSXAutomatic( p, "<div key=\"key\" />", "/* @__PURE__ */ jsx(\"div\", {}, \"key\");\n");
    expectPrintedJSXAutomatic( p, "<div key=\"key\" {...props} />", "/* @__PURE__ */ jsx(\"div\", { ...props }, \"key\");\n");
    expectPrintedJSXAutomatic( p, "<div {...props} key=\"key\" />", "/* @__PURE__ */ createElement(\"div\", { ...props, key: \"key\" });\n");// Falls back to createElement
    expectPrintedJSXAutomatic( p, "<div>{...children}</div>", "/* @__PURE__ */ jsxs(\"div\", { children: [\n  ...children\n] });\n");
    expectPrintedJSXAutomatic( p, "<div>{...children}<a/></div>", "/* @__PURE__ */ jsxs(\"div\", { children: [\n  ...children,\n  /* @__PURE__ */ jsx(\"a\", {})\n] });\n");
    expectPrintedJSXAutomatic( p, "<>></>", "/* @__PURE__ */ jsx(Fragment, { children: \">\" });\n");
    expectParseErrorJSXAutomatic( p, "<a key/>",
        "<stdin>: ERROR: Please provide an explicit value for \"key\":\n"
        "NOTE: Using \"key\" as a shorthand for \"key={true}\" is not allowed when using React's \"automatic\" JSX transform.\n"
        "");
    expectParseErrorJSXAutomatic( p, "<div __self={self} />",
        "<stdin>: ERROR: Duplicate \"__self\" prop found:\n"
        "NOTE: Both \"__source\" and \"__self\" are set automatically by Guchho when using React's \"automatic\" JSX transform. This duplicate prop may have come from a plugin.\n"
        "");
    expectParseErrorJSXAutomatic( p, "<div __source=\"/path/to/source.jsx\" />",
        "<stdin>: ERROR: Duplicate \"__source\" prop found:\n"
        "NOTE: Both \"__source\" and \"__self\" are set automatically by Guchho when using React's \"automatic\" JSX transform. This duplicate prop may have come from a plugin.\n"
        "");
    // Prod, with runtime imports
    JSXAutomaticTestOptions pr{false, "", false, false};
    expectPrintedJSXAutomatic( pr, "<div/>", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"div\", {});\n");
    expectPrintedJSXAutomatic( pr, "<><a/><b/></>", "import { Fragment, jsx, jsxs } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsxs(Fragment, { children: [\n  /* @__PURE__ */ jsx(\"a\", {}),\n  /* @__PURE__ */ jsx(\"b\", {})\n] });\n");
    expectPrintedJSXAutomatic( pr, "<div {...props} key=\"key\" />", "import { createElement } from \"react\";\n/* @__PURE__ */ createElement(\"div\", { ...props, key: \"key\" });\n");
    expectPrintedJSXAutomatic( pr, "<><div {...props} key=\"key\" /></>", "import { Fragment, jsx } from \"react/jsx-runtime\";\nimport { createElement } from \"react\";\n/* @__PURE__ */ jsx(Fragment, { children: /* @__PURE__ */ createElement(\"div\", { ...props, key: \"key\" }) });\n");
    JSXAutomaticTestOptions pri{false, "my-jsx-lib", false, false};
    expectPrintedJSXAutomatic( pri, "<div/>", "import { jsx } from \"my-jsx-lib/jsx-runtime\";\n/* @__PURE__ */ jsx(\"div\", {});\n");
    expectPrintedJSXAutomatic( pri, "<div {...props} key=\"key\" />", "import { createElement } from \"my-jsx-lib\";\n/* @__PURE__ */ createElement(\"div\", { ...props, key: \"key\" });\n");
    // Impure JSX call expressions
    JSXAutomaticTestOptions pi{false, "my-jsx-lib", false, true};
    expectPrintedJSXAutomatic( pi, "<a/>", "import { jsx } from \"my-jsx-lib/jsx-runtime\";\njsx(\"a\", {});\n");
    expectPrintedJSXAutomatic( pi, "<></>", "import { Fragment, jsx } from \"my-jsx-lib/jsx-runtime\";\njsx(Fragment, {});\n");
    // Dev, without runtime imports
    JSXAutomaticTestOptions d{true, "", true, false};
    expectPrintedJSXAutomatic( d, "<div>></div>", "/* @__PURE__ */ jsxDEV(\"div\", { children: \">\" }, void 0, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<div>{1}}</div>", "/* @__PURE__ */ jsxDEV(\"div\", { children: [\n  1,\n  \"}\"\n] }, void 0, true, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<div key={true} />", "/* @__PURE__ */ jsxDEV(\"div\", {}, true, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<div key=\"key\" />", "/* @__PURE__ */ jsxDEV(\"div\", {}, \"key\", false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<div key=\"key\" {...props} />", "/* @__PURE__ */ jsxDEV(\"div\", { ...props }, \"key\", false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<div {...props} key=\"key\" />", "/* @__PURE__ */ createElement(\"div\", { ...props, key: \"key\" });\n");// Falls back to createElement
    expectPrintedJSXAutomatic( d, "<div>{...children}</div>", "/* @__PURE__ */ jsxDEV(\"div\", { children: [\n  ...children\n] }, void 0, true, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<div>\n  {...children}\n  <a/></div>", "/* @__PURE__ */ jsxDEV(\"div\", { children: [\n  ...children,\n  /* @__PURE__ */ jsxDEV(\"a\", {}, void 0, false, {\n    fileName: \"<stdin>\",\n    lineNumber: 3,\n    columnNumber: 3\n  }, this)\n] }, void 0, true, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "<>></>", "/* @__PURE__ */ jsxDEV(Fragment, { children: \">\" }, void 0, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectParseErrorJSXAutomatic( d, "<a key/>",
        "<stdin>: ERROR: Please provide an explicit value for \"key\":\n"
        "NOTE: Using \"key\" as a shorthand for \"key={true}\" is not allowed when using React's \"automatic\" JSX transform.\n"
        "");
    expectParseErrorJSXAutomatic( d, "<div __self={self} />",
        "<stdin>: ERROR: Duplicate \"__self\" prop found:\n"
        "NOTE: Both \"__source\" and \"__self\" are set automatically by Guchho when using React's \"automatic\" JSX transform. This duplicate prop may have come from a plugin.\n"
        "");
    expectParseErrorJSXAutomatic( d, "<div __source=\"/path/to/source.jsx\" />",
        "<stdin>: ERROR: Duplicate \"__source\" prop found:\n"
        "NOTE: Both \"__source\" and \"__self\" are set automatically by Guchho when using React's \"automatic\" JSX transform. This duplicate prop may have come from a plugin.\n"
        "");

    expectPrintedJSXAutomatic( d, "\r\n<x/>", "/* @__PURE__ */ jsxDEV(\"x\", {}, void 0, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 2,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "\n\r<x/>", "/* @__PURE__ */ jsxDEV(\"x\", {}, void 0, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 3,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( d, "let 𐀀 = <x>🍕🍕🍕<y/></x>", "let 𐀀 = /* @__PURE__ */ jsxDEV(\"x\", { children: [\n  \"🍕🍕🍕\",\n  /* @__PURE__ */ jsxDEV(\"y\", {}, void 0, false, {\n    fileName: \"<stdin>\",\n    lineNumber: 1,\n    columnNumber: 19\n  }, this)\n] }, void 0, true, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 10\n}, this);\n");
    // Dev, with runtime imports
    JSXAutomaticTestOptions dr{true, "", false, false};
    expectPrintedJSXAutomatic( dr, "<div/>", "import { jsxDEV } from \"react/jsx-dev-runtime\";\n/* @__PURE__ */ jsxDEV(\"div\", {}, void 0, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( dr, "<>\n  <a/>\n  <b/>\n</>", "import { Fragment, jsxDEV } from \"react/jsx-dev-runtime\";\n/* @__PURE__ */ jsxDEV(Fragment, { children: [\n  /* @__PURE__ */ jsxDEV(\"a\", {}, void 0, false, {\n    fileName: \"<stdin>\",\n    lineNumber: 2,\n    columnNumber: 3\n  }, this),\n  /* @__PURE__ */ jsxDEV(\"b\", {}, void 0, false, {\n    fileName: \"<stdin>\",\n    lineNumber: 3,\n    columnNumber: 3\n  }, this)\n] }, void 0, true, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    JSXAutomaticTestOptions dri{true, "preact", false, false};
    expectPrintedJSXAutomatic( dri, "<div/>", "import { jsxDEV } from \"preact/jsx-dev-runtime\";\n/* @__PURE__ */ jsxDEV(\"div\", {}, void 0, false, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    expectPrintedJSXAutomatic( dri, "<>\n  <a/>\n  <b/>\n</>", "import { Fragment, jsxDEV } from \"preact/jsx-dev-runtime\";\n/* @__PURE__ */ jsxDEV(Fragment, { children: [\n  /* @__PURE__ */ jsxDEV(\"a\", {}, void 0, false, {\n    fileName: \"<stdin>\",\n    lineNumber: 2,\n    columnNumber: 3\n  }, this),\n  /* @__PURE__ */ jsxDEV(\"b\", {}, void 0, false, {\n    fileName: \"<stdin>\",\n    lineNumber: 3,\n    columnNumber: 3\n  }, this)\n] }, void 0, true, {\n  fileName: \"<stdin>\",\n  lineNumber: 1,\n  columnNumber: 1\n}, this);\n");
    // JSX namespaced names
    for (const std::string& colon : std::vector<std::string>{":", " :", ": ", " : "}) {
        expectPrintedJSXAutomatic( p, "<a"+colon+"b/>", "/* @__PURE__ */ jsx(\"a:b\", {});\n");
        expectPrintedJSXAutomatic( p, "<a-b"+colon+"c-d/>", "/* @__PURE__ */ jsx(\"a-b:c-d\", {});\n");
        expectPrintedJSXAutomatic( p, "<a-"+colon+"b-/>", "/* @__PURE__ */ jsx(\"a-:b-\", {});\n");
        expectPrintedJSXAutomatic( p, "<Te"+colon+"st/>", "/* @__PURE__ */ jsx(\"Te:st\", {});\n");
        expectPrintedJSXAutomatic( p, "<x a"+colon+"b/>", "/* @__PURE__ */ jsx(\"x\", { \"a:b\": true });\n");
        expectPrintedJSXAutomatic( p, "<x a-b"+colon+"c-d/>", "/* @__PURE__ */ jsx(\"x\", { \"a-b:c-d\": true });\n");
        expectPrintedJSXAutomatic( p, "<x a-"+colon+"b-/>", "/* @__PURE__ */ jsx(\"x\", { \"a-:b-\": true });\n");
        expectPrintedJSXAutomatic( p, "<x Te"+colon+"st/>", "/* @__PURE__ */ jsx(\"x\", { \"Te:st\": true });\n");
        expectPrintedJSXAutomatic( p, "<x a"+colon+"b={0}/>", "/* @__PURE__ */ jsx(\"x\", { \"a:b\": 0 });\n");
        expectPrintedJSXAutomatic( p, "<x a-b"+colon+"c-d={0}/>", "/* @__PURE__ */ jsx(\"x\", { \"a-b:c-d\": 0 });\n");
        expectPrintedJSXAutomatic( p, "<x a-"+colon+"b-={0}/>", "/* @__PURE__ */ jsx(\"x\", { \"a-:b-\": 0 });\n");
        expectPrintedJSXAutomatic( p, "<x Te"+colon+"st={0}/>", "/* @__PURE__ */ jsx(\"x\", { \"Te:st\": 0 });\n");
        expectPrintedJSXAutomatic( p, "<a-b a-b={a-b}/>", "/* @__PURE__ */ jsx(\"a-b\", { \"a-b\": a - b });\n");
        expectParseErrorJSXAutomatic( p, "<x"+colon+"/>", "<stdin>: ERROR: Expected identifier after \"x:\" in namespaced JSX name\n");
        expectParseErrorJSXAutomatic( p, "<x"+colon+"y"+colon+"/>", "<stdin>: ERROR: Expected \">\" but found \":\"\n");
        expectParseErrorJSXAutomatic( p, "<x"+colon+"0y/>", "<stdin>: ERROR: Expected identifier after \"x:\" in namespaced JSX name\n");
    }
    // Enabling the "automatic" runtime means that any JSX element will cause the
    // file to be implicitly in strict mode due to the automatically-generated
    // import statement. This is the same behavior as the TypeScript compiler.
    const std::string strictModeError = "<stdin>: ERROR: With statements cannot be used in strict mode\n" "<stdin>: NOTE: This file is implicitly in strict mode due to the JSX element here:\n" "NOTE: When React's \"automatic\" JSX transform is enabled, using a JSX element automatically inserts an \"import\" statement at the top of the file " "for the corresponding the JSX helper function. This means the file is considered an ECMAScript module, and all ECMAScript modules use strict mode.\n";
    expectPrintedJSX( "with (x) y(<z/>)", "with (x) y(<z />);\n", "with (x) y(/* @__PURE__ */ React.createElement(\"z\", null));\n");
    expectPrintedJSXAutomatic( p, "with (x) y", "with (x) y;\n");
    expectParseErrorJSX( "with (x) y(<z/>) // @jsxRuntime automatic", strictModeError);
    expectParseErrorJSXAutomatic( p, "with (x) y(<z/>)", strictModeError);
}

TEST(JsParser, TestJSXAutomaticPragmas) {
    expectPrintedJSX( "// @jsxRuntime automatic\n<a/>", "<a />;\n", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "/*@jsxRuntime automatic*/\n<a/>", "<a />;\n", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "/* @jsxRuntime automatic */\n<a/>", "<a />;\n", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "<a/>\n/*@jsxRuntime automatic*/", "<a />;\n", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "<a/>\n/* @jsxRuntime automatic */", "<a />;\n", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "// @jsxRuntime classic\n<a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "/*@jsxRuntime classic*/\n<a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "/* @jsxRuntime classic */\n<a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a/>\n/*@jsxRuntime classic*/\n", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<a/>\n/* @jsxRuntime classic */\n", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectParseErrorJSX( "// @jsxRuntime foo\n<a/>",
        "<stdin>: WARNING: Invalid JSX runtime: \"foo\"\n"
        "NOTE: The JSX runtime can only be set to either \"classic\" or \"automatic\".\n"
        "");
    expectPrintedJSX( "// @jsxRuntime automatic @jsxImportSource src\n<a/>", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "/*@jsxRuntime automatic @jsxImportSource src*/\n<a/>", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "/*@jsxRuntime automatic*//*@jsxImportSource src*/\n<a/>", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "/* @jsxRuntime automatic */\n/* @jsxImportSource src */\n<a/>", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "<a/>\n/*@jsxRuntime automatic @jsxImportSource src*/", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "<a/>\n/*@jsxRuntime automatic*/\n/*@jsxImportSource src*/", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "<a/>\n/* @jsxRuntime automatic */\n/* @jsxImportSource src */", "<a />;\n", "import { jsx } from \"src/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectPrintedJSX( "// @jsxRuntime classic @jsxImportSource src\n<a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectParseErrorJSX( "// @jsxRuntime classic @jsxImportSource src\n<a/>",
        "<stdin>: WARNING: The JSX import source cannot be set without also enabling React's \"automatic\" JSX transform\n"
        "NOTE: You can enable React's \"automatic\" JSX transform for this file by using a \"@jsxRuntime automatic\" comment.\n"
        "");
    expectParseErrorJSX( "// @jsxImportSource src\n<a/>",
        "<stdin>: WARNING: The JSX import source cannot be set without also enabling React's \"automatic\" JSX transform\n"
        "NOTE: You can enable React's \"automatic\" JSX transform for this file by using a \"@jsxRuntime automatic\" comment.\n"
        "");
    expectPrintedJSX( "// @jsxRuntime automatic @jsx h\n<a/>", "<a />;\n", "import { jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(\"a\", {});\n");
    expectParseErrorJSX( "// @jsxRuntime automatic @jsx h\n<a/>", "<stdin>: WARNING: The JSX factory cannot be set when using React's \"automatic\" JSX transform\n");
    expectPrintedJSX( "// @jsxRuntime automatic @jsxFrag f\n<></>", "<></>;\n", "import { Fragment, jsx } from \"react/jsx-runtime\";\n/* @__PURE__ */ jsx(Fragment, {});\n");
    expectParseErrorJSX( "// @jsxRuntime automatic @jsxFrag f\n<></>", "<stdin>: WARNING: The JSX fragment cannot be set when using React's \"automatic\" JSX transform\n");
}

TEST(JsParser, TestJSXSideEffects) {
    expectPrintedJSX( "<a/>", "<a />;\n", "/* @__PURE__ */ React.createElement(\"a\", null);\n");
    expectPrintedJSX( "<></>", "<></>;\n", "/* @__PURE__ */ React.createElement(React.Fragment, null);\n");
    expectPrintedJSXSideEffects( "<a/>", "React.createElement(\"a\", null);\n");
    expectPrintedJSXSideEffects( "<></>", "React.createElement(React.Fragment, null);\n");
}

TEST(JsParser, TestPreserveOptionalChainParentheses) {
    expectPrinted( "a?.b.c", "a?.b.c;\n");
    expectPrinted( "(a?.b).c", "(a?.b).c;\n");
    expectPrinted( "a?.b.c.d", "a?.b.c.d;\n");
    expectPrinted( "(a?.b.c).d", "(a?.b.c).d;\n");
    expectPrinted( "a?.b[c]", "a?.b[c];\n");
    expectPrinted( "(a?.b)[c]", "(a?.b)[c];\n");
    expectPrinted( "a?.b(c)", "a?.b(c);\n");
    expectPrinted( "(a?.b)(c)", "(a?.b)(c);\n");
    expectPrinted( "new (a?.b)", "new (a?.b)();\n");
    expectPrinted( "a?.[b][c]", "a?.[b][c];\n");
    expectPrinted( "(a?.[b])[c]", "(a?.[b])[c];\n");
    expectPrinted( "a?.[b][c][d]", "a?.[b][c][d];\n");
    expectPrinted( "(a?.[b][c])[d]", "(a?.[b][c])[d];\n");
    expectPrinted( "a?.[b].c", "a?.[b].c;\n");
    expectPrinted( "(a?.[b]).c", "(a?.[b]).c;\n");
    expectPrinted( "a?.[b](c)", "a?.[b](c);\n");
    expectPrinted( "(a?.[b])(c)", "(a?.[b])(c);\n");
    expectPrinted( "new (a?.[b])", "new (a?.[b])();\n");
    expectPrinted( "a?.(b)(c)", "a?.(b)(c);\n");
    expectPrinted( "(a?.(b))(c)", "(a?.(b))(c);\n");
    expectPrinted( "a?.(b)(c)(d)", "a?.(b)(c)(d);\n");
    expectPrinted( "(a?.(b)(c))(d)", "(a?.(b)(c))(d);\n");
    expectPrinted( "a?.(b).c", "a?.(b).c;\n");
    expectPrinted( "(a?.(b)).c", "(a?.(b)).c;\n");
    expectPrinted( "a?.(b)[c]", "a?.(b)[c];\n");
    expectPrinted( "(a?.(b))[c]", "(a?.(b))[c];\n");
    expectPrinted( "new (a?.(b))", "new (a?.(b))();\n");
    expectPrinted( "new a()?.b", "new a()?.b;\n");
    expectPrinted( "new a()?.[b]", "new a()?.[b];\n");
    expectPrinted( "new a()?.(b)", "new a()?.(b);\n");
    expectPrinted( "new a.b()?.c", "new a.b()?.c;\n");
    expectPrinted( "new a.b()?.[c]", "new a.b()?.[c];\n");
    expectPrinted( "new a.b()?.(c)", "new a.b()?.(c);\n");
    const std::string err = "<stdin>: ERROR: Cannot use an unparenthesized optional chain inside the target of \"new\"\n";
    expectParseError( "new a?.b", err);
    expectParseError( "new a?.[b]", err);
    expectParseError( "new a?.(b)", err);
    expectParseError( "new a.b?.c", err);
    expectParseError( "new a.b?.[c]", err);
    expectParseError( "new a.b?.(c)", err);
}

TEST(JsParser, TestPrivateIdentifiers) {
    expectParseError( "#foo", "<stdin>: ERROR: Expected \"in\" but found end of file\n");
    expectParseError( "#foo in this", "<stdin>: ERROR: Private name \"#foo\" must be declared in an enclosing class\n");
    expectParseError( "this.#foo", "<stdin>: ERROR: Private name \"#foo\" must be declared in an enclosing class\n");
    expectParseError( "this?.#foo", "<stdin>: ERROR: Private name \"#foo\" must be declared in an enclosing class\n");
    expectParseError( "({ #foo: 1 })", "<stdin>: ERROR: Expected identifier but found \"#foo\"\n");
    expectParseError( "class Foo { x = { #foo: 1 } }", "<stdin>: ERROR: Expected identifier but found \"#foo\"\n");
    expectParseError( "class Foo { x = #foo }", "<stdin>: ERROR: Expected \"in\" but found \"}\"\n");
    expectParseError( "class Foo { #foo; foo() { delete this.#foo } }",
        "<stdin>: ERROR: Deleting the private name \"#foo\" is forbidden\n");
    expectParseError( "class Foo { #foo; foo() { delete this?.#foo } }",
        "<stdin>: ERROR: Deleting the private name \"#foo\" is forbidden\n");
    expectParseError( "class Foo extends Bar { #foo; foo() { super.#foo } }",
        "<stdin>: ERROR: Expected identifier but found \"#foo\"\n");
    expectParseError( "class Foo { #foo = () => { for (#foo in this) ; } }",
        "<stdin>: ERROR: Unexpected \"#foo\"\n");
    expectParseError( "class Foo { #foo = () => { for (x = #foo in this) ; } }",
        "<stdin>: ERROR: Unexpected \"#foo\"\n");
    expectPrinted( "class Foo { #foo }", "class Foo {\n  #foo;\n}\n");
    expectPrinted( "class Foo { #foo = 1 }", "class Foo {\n  #foo = 1;\n}\n");
    expectPrinted( "class Foo { #foo = #foo in this }", "class Foo {\n  #foo = #foo in this;\n}\n");
    expectPrinted( "class Foo { #foo = #foo in (#bar in this); #bar }", "class Foo {\n  #foo = #foo in (#bar in this);\n  #bar;\n}\n");
    expectPrinted( "class Foo { #foo() {} }", "class Foo {\n  #foo() {\n  }\n}\n");
    expectPrinted( "class Foo { get #foo() {} }", "class Foo {\n  get #foo() {\n  }\n}\n");
    expectPrinted( "class Foo { set #foo(x) {} }", "class Foo {\n  set #foo(x) {\n  }\n}\n");
    expectPrinted( "class Foo { static #foo }", "class Foo {\n  static #foo;\n}\n");
    expectPrinted( "class Foo { static #foo = 1 }", "class Foo {\n  static #foo = 1;\n}\n");
    expectPrinted( "class Foo { static #foo() {} }", "class Foo {\n  static #foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static get #foo() {} }", "class Foo {\n  static get #foo() {\n  }\n}\n");
    expectPrinted( "class Foo { static set #foo(x) {} }", "class Foo {\n  static set #foo(x) {\n  }\n}\n");
    expectParseError( "class Foo { #foo = #foo in #bar in this; #bar }", "<stdin>: ERROR: Unexpected \"#bar\"\n");
    // The name "#constructor" is forbidden
    expectParseError( "class Foo { #constructor }", "<stdin>: ERROR: Invalid field name \"#constructor\"\n");
    expectParseError( "class Foo { #constructor() {} }", "<stdin>: ERROR: Invalid method name \"#constructor\"\n");
    expectParseError( "class Foo { static #constructor }", "<stdin>: ERROR: Invalid field name \"#constructor\"\n");
    expectParseError( "class Foo { static #constructor() {} }", "<stdin>: ERROR: Invalid method name \"#constructor\"\n");
    expectParseError( "class Foo { #\\u0063onstructor }", "<stdin>: ERROR: Invalid field name \"#constructor\"\n");
    expectParseError( "class Foo { #\\u0063onstructor() {} }", "<stdin>: ERROR: Invalid method name \"#constructor\"\n");
    expectParseError( "class Foo { static #\\u0063onstructor }", "<stdin>: ERROR: Invalid field name \"#constructor\"\n");
    expectParseError( "class Foo { static #\\u0063onstructor() {} }", "<stdin>: ERROR: Invalid method name \"#constructor\"\n");
    // Test escape sequences
    expectPrinted( "class Foo { #\\u0066oo; foo = this.#foo }", "class Foo {\n  #foo;\n  foo = this.#foo;\n}\n");
    expectPrinted( "class Foo { #fo\\u006f; foo = this.#foo }", "class Foo {\n  #foo;\n  foo = this.#foo;\n}\n");
    expectParseError( "class Foo { #\\u0020oo }", "<stdin>: ERROR: Invalid identifier: \"# oo\"\n");
    expectParseError( "class Foo { #fo\\u0020 }", "<stdin>: ERROR: Invalid identifier: \"#fo \"\n");
    const std::string errorText = "<stdin>: ERROR: The symbol \"#foo\" has already been declared\n"
        "<stdin>: NOTE: The symbol \"#foo\" was originally declared here:\n"
        "";
    // Scope tests
    expectParseError( "class Foo { #foo; #foo }", errorText);
    expectParseError( "class Foo { #foo; static #foo }", errorText);
    expectParseError( "class Foo { static #foo; #foo }", errorText);
    expectParseError( "class Foo { #foo; #foo() {} }", errorText);
    expectParseError( "class Foo { #foo; get #foo() {} }", errorText);
    expectParseError( "class Foo { #foo; set #foo(x) {} }", errorText);
    expectParseError( "class Foo { #foo() {} #foo }", errorText);
    expectParseError( "class Foo { get #foo() {} #foo }", errorText);
    expectParseError( "class Foo { set #foo(x) {} #foo }", errorText);
    expectParseError( "class Foo { get #foo() {} get #foo() {} }", errorText);
    expectParseError( "class Foo { set #foo(x) {} set #foo(x) {} }", errorText);
    expectParseError( "class Foo { get #foo() {} set #foo(x) {} #foo }", errorText);
    expectParseError( "class Foo { set #foo(x) {} get #foo() {} #foo }", errorText);
    expectPrinted( "class Foo { get #foo() {} set #foo(x) { this.#foo } }",
        "class Foo {\n  get #foo() {\n  }\n  set #foo(x) {\n    this.#foo;\n  }\n}\n");
    expectPrinted( "class Foo { set #foo(x) { this.#foo } get #foo() {} }",
        "class Foo {\n  set #foo(x) {\n    this.#foo;\n  }\n  get #foo() {\n  }\n}\n");
    expectPrinted( "class Foo { #foo } class Bar { #foo }", "class Foo {\n  #foo;\n}\nclass Bar {\n  #foo;\n}\n");
    expectPrinted( "class Foo { foo = this.#foo; #foo }", "class Foo {\n  foo = this.#foo;\n  #foo;\n}\n");
    expectPrinted( "class Foo { foo = this?.#foo; #foo }", "class Foo {\n  foo = this?.#foo;\n  #foo;\n}\n");
    expectParseError( "class Foo { #foo } class Bar { foo = this.#foo }",
        "<stdin>: ERROR: Private name \"#foo\" must be declared in an enclosing class\n");
    expectParseError( "class Foo { #foo } class Bar { foo = this?.#foo }",
        "<stdin>: ERROR: Private name \"#foo\" must be declared in an enclosing class\n");
    expectParseError( "class Foo { #foo } class Bar { foo = #foo in this }",
        "<stdin>: ERROR: Private name \"#foo\" must be declared in an enclosing class\n");
    // Getter and setter warnings
    expectParseError( "class Foo { get #x() { this.#x = 1 } }",
        "<stdin>: WARNING: Writing to getter-only property \"#x\" will throw\n");
    expectParseError( "class Foo { get #x() { this.#x += 1 } }",
        "<stdin>: WARNING: Writing to getter-only property \"#x\" will throw\n");
    expectParseError( "class Foo { set #x(x) { this.#x } }",
        "<stdin>: WARNING: Reading from setter-only property \"#x\" will throw\n");
    expectParseError( "class Foo { set #x(x) { this.#x += 1 } }",
        "<stdin>: WARNING: Reading from setter-only property \"#x\" will throw\n");
    // Writing to method warnings
    expectParseError( "class Foo { #x() { this.#x = 1 } }",
        "<stdin>: WARNING: Writing to read-only method \"#x\" will throw\n");
    expectParseError( "class Foo { #x() { this.#x += 1 } }",
        "<stdin>: WARNING: Writing to read-only method \"#x\" will throw\n");
    expectPrinted( "class Foo {\n"
        "\t#if\n"
        "\t#im() { return this.#im(this.#if) }\n"
        "\tstatic #sf\n"
        "\tstatic #sm() { return this.#sm(this.#sf) }\n"
        "\tfoo() {\n"
        "\t\treturn class {\n"
        "\t\t\t#inner() {\n"
        "\t\t\t\treturn [this.#im, this?.#inner, this?.x.#if]\n"
        "\t\t\t}\n"
        "\t\t}\n"
        "\t}\n"
        "}\n"
        "", "class Foo {\n"
        "  #if;\n"
        "  #im() {\n"
        "    return this.#im(this.#if);\n"
        "  }\n"
        "  static #sf;\n"
        "  static #sm() {\n"
        "    return this.#sm(this.#sf);\n"
        "  }\n"
        "  foo() {\n"
        "    return class {\n"
        "      #inner() {\n"
        "        return [this.#im, this?.#inner, this?.x.#if];\n"
        "      }\n"
        "    };\n"
        "  }\n"
        "}\n"
        "");
}

TEST(JsParser, TestImportAssertions) {
    expectPrinted( "import 'x' assert {}", "import \"x\" assert {};\n");
    expectPrinted( "import 'x' assert {\n}", "import \"x\" assert {};\n");
    expectPrinted( "import 'x' assert\n{}", "import \"x\" assert {};\n");
    expectPrinted( "import 'x'\nassert\n{}", "import \"x\";\nassert;\n{\n}\n");
    expectPrinted( "import 'x' assert {type: 'json'}", "import \"x\" assert { type: \"json\" };\n");
    expectPrinted( "import 'x' assert {type: 'json',}", "import \"x\" assert { type: \"json\" };\n");
    expectPrinted( "import 'x' assert {'type': 'json'}", "import \"x\" assert { \"type\": \"json\" };\n");
    expectPrinted( "import 'x' assert {a: 'b', c: 'd'}", "import \"x\" assert { a: \"b\", c: \"d\" };\n");
    expectPrinted( "import 'x' assert {a: 'b', c: 'd',}", "import \"x\" assert { a: \"b\", c: \"d\" };\n");
    expectPrinted( "import 'x' assert {if: 'keyword'}", "import \"x\" assert { if: \"keyword\" };\n");
    expectPrintedMangle( "import 'x' assert {'type': 'json'}", "import \"x\" assert { type: \"json\" };\n");
    expectPrintedMangle( "import 'x' assert {'ty pe': 'json'}", "import \"x\" assert { \"ty pe\": \"json\" };\n");
    expectParseError( "import 'x' assert {,}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseError( "import 'x' assert {x}", "<stdin>: ERROR: Expected \":\" but found \"}\"\n");
    expectParseError( "import 'x' assert {x 'y'}", "<stdin>: ERROR: Expected \":\" but found \"'y'\"\n");
    expectParseError( "import 'x' assert {x: y}", "<stdin>: ERROR: Expected string but found \"y\"\n");
    expectParseError( "import 'x' assert {x: 'y',,}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseError( "import 'x' assert {`x`: 'y'}", "<stdin>: ERROR: Expected identifier but found \"`x`\"\n");
    expectParseError( "import 'x' assert {x: `y`}", "<stdin>: ERROR: Expected string but found \"`y`\"\n");
    expectParseError( "import 'x' assert: {x: 'y'}", "<stdin>: ERROR: Expected \"{\" but found \":\"\n");
    expectParseError( "import 'x' assert {x: 'y', x: 'y'}",
        "<stdin>: ERROR: Duplicate import assertion \"x\"\n<stdin>: NOTE: The first \"x\" was here:\n");
    expectParseError( "import 'x' assert {x: 'y', \\u0078: 'y'}",
        "<stdin>: ERROR: Duplicate import assertion \"x\"\n<stdin>: NOTE: The first \"x\" was here:\n");
    expectPrinted( "import x from 'x' assert {x: 'y'}", "import x from \"x\" assert { x: \"y\" };\n");
    expectPrinted( "import * as x from 'x' assert {x: 'y'}", "import * as x from \"x\" assert { x: \"y\" };\n");
    expectPrinted( "import {} from 'x' assert {x: 'y'}", "import {} from \"x\" assert { x: \"y\" };\n");
    expectPrinted( "export {} from 'x' assert {x: 'y'}", "export {} from \"x\" assert { x: \"y\" };\n");
    expectPrinted( "export * from 'x' assert {x: 'y'}", "export * from \"x\" assert { x: \"y\" };\n");
    expectPrinted( "import(x ? 'y' : 'z')", "x ? import(\"y\") : import(\"z\");\n");
    expectPrinted( "import(x ? 'y' : 'z', {assert: {}})",
        "x ? import(\"y\", { assert: {} }) : import(\"z\", { assert: {} });\n");
    expectPrinted( "import(x ? 'y' : 'z', {assert: {a: 'b'}})",
        "x ? import(\"y\", { assert: { a: \"b\" } }) : import(\"z\", { assert: { a: \"b\" } });\n");
    expectPrinted( "import(x ? 'y' : 'z', {assert: {'a': 'b'}})",
        "x ? import(\"y\", { assert: { \"a\": \"b\" } }) : import(\"z\", { assert: { \"a\": \"b\" } });\n");
    expectPrintedMangle( "import(x ? 'y' : 'z', {assert: {'a': 'b'}})",
        "x ? import(\"y\", { assert: { a: \"b\" } }) : import(\"z\", { assert: { a: \"b\" } });\n");
    expectPrintedMangle( "import(x ? 'y' : 'z', {assert: {'a a': 'b'}})",
        "x ? import(\"y\", { assert: { \"a a\": \"b\" } }) : import(\"z\", { assert: { \"a a\": \"b\" } });\n");
    expectPrinted( "import(x ? 'y' : 'z', {})", "import(x ? \"y\" : \"z\", {});\n");
    expectPrinted( "import(x ? 'y' : 'z', {assert: []})", "import(x ? \"y\" : \"z\", { assert: [] });\n");
    expectPrinted( "import(x ? 'y' : 'z', {asserts: {}})", "import(x ? \"y\" : \"z\", { asserts: {} });\n");
    expectPrinted( "import(x ? 'y' : 'z', {assert: {x: 1}})", "import(x ? \"y\" : \"z\", { assert: { x: 1 } });\n");
    expectPrintedTarget( 2015, "import 'x' assert {x: 'y'}", "import \"x\";\n");
    expectPrintedTarget( 2015, "import(x, {assert: {x: 'y'}})", "import(x);\n");
    expectPrintedTarget( 2015, "import(x, {assert: {x: 1}})", "import(x);\n");
    expectPrintedTarget( 2015, "import(x ? 'y' : 'z', {assert: {x: 'y'}})", "x ? import(\"y\") : import(\"z\");\n");
    expectPrintedTarget( 2015, "import(x ? 'y' : 'z', {assert: {x: 1}})", "import(x ? \"y\" : \"z\");\n");
    expectParseErrorTarget( 2015, "import(x ? 'y' : 'z', {assert: {x: foo()}})",
        "<stdin>: ERROR: Using an arbitrary value as the second argument to \"import()\" is not possible in the configured target environment\n");
    // Make sure there are no errors when bundling is disabled
    expectParseError( "import { foo } from 'x' assert {type: 'json'}", "");
    expectParseError( "export { foo } from 'x' assert {type: 'json'}", "");
    // Only omit the second argument to "import()" if both assertions and attributes aren't supported
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kImportAssertions,
        "import 'x' assert {y: 'z'}; import('x', {assert: {y: 'z'}})",
        "import \"x\";\nimport(\"x\", { assert: { y: \"z\" } });\n");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kImportAttributes,
        "import 'x' assert {y: 'z'}; import('x', {assert: {y: 'z'}})",
        "import \"x\" assert { y: \"z\" };\nimport(\"x\", { assert: { y: \"z\" } });\n");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kImportAssertions|compat::JSFeature::kImportAttributes,
        "import 'x' assert {y: 'z'}; import('x', {assert: {y: 'z'}})",
        "import \"x\";\nimport(\"x\");\n");
}

TEST(JsParser, TestImportAttributes) {
    expectPrinted( "import 'x' with {}", "import \"x\" with {};\n");
    expectPrinted( "import 'x' with {\n}", "import \"x\" with {};\n");
    expectPrinted( "import 'x' with\n{}", "import \"x\" with {};\n");
    expectPrinted( "import 'x'\nwith\n{}", "import \"x\" with {};\n");
    expectPrinted( "import 'x' with {type: 'json'}", "import \"x\" with { type: \"json\" };\n");
    expectPrinted( "import 'x' with {type: 'json',}", "import \"x\" with { type: \"json\" };\n");
    expectPrinted( "import 'x' with {'type': 'json'}", "import \"x\" with { \"type\": \"json\" };\n");
    expectPrinted( "import 'x' with {a: 'b', c: 'd'}", "import \"x\" with { a: \"b\", c: \"d\" };\n");
    expectPrinted( "import 'x' with {a: 'b', c: 'd',}", "import \"x\" with { a: \"b\", c: \"d\" };\n");
    expectPrinted( "import 'x' with {if: 'keyword'}", "import \"x\" with { if: \"keyword\" };\n");
    expectPrintedMangle( "import 'x' with {'type': 'json'}", "import \"x\" with { type: \"json\" };\n");
    expectPrintedMangle( "import 'x' with {'ty pe': 'json'}", "import \"x\" with { \"ty pe\": \"json\" };\n");
    expectParseError( "import 'x' with {,}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseError( "import 'x' with {x}", "<stdin>: ERROR: Expected \":\" but found \"}\"\n");
    expectParseError( "import 'x' with {x 'y'}", "<stdin>: ERROR: Expected \":\" but found \"'y'\"\n");
    expectParseError( "import 'x' with {x: y}", "<stdin>: ERROR: Expected string but found \"y\"\n");
    expectParseError( "import 'x' with {x: 'y',,}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseError( "import 'x' with {`x`: 'y'}", "<stdin>: ERROR: Expected identifier but found \"`x`\"\n");
    expectParseError( "import 'x' with {x: `y`}", "<stdin>: ERROR: Expected string but found \"`y`\"\n");
    expectParseError( "import 'x' with: {x: 'y'}", "<stdin>: ERROR: Expected \"{\" but found \":\"\n");
    expectParseError( "import 'x' with {x: 'y', x: 'y'}",
        "<stdin>: ERROR: Duplicate import attribute \"x\"\n<stdin>: NOTE: The first \"x\" was here:\n");
    expectParseError( "import 'x' with {x: 'y', \\u0078: 'y'}",
        "<stdin>: ERROR: Duplicate import attribute \"x\"\n<stdin>: NOTE: The first \"x\" was here:\n");
    expectPrinted( "import x from 'x' with {x: 'y'}", "import x from \"x\" with { x: \"y\" };\n");
    expectPrinted( "import * as x from 'x' with {x: 'y'}", "import * as x from \"x\" with { x: \"y\" };\n");
    expectPrinted( "import {} from 'x' with {x: 'y'}", "import {} from \"x\" with { x: \"y\" };\n");
    expectPrinted( "export {} from 'x' with {x: 'y'}", "export {} from \"x\" with { x: \"y\" };\n");
    expectPrinted( "export * from 'x' with {x: 'y'}", "export * from \"x\" with { x: \"y\" };\n");
    expectPrinted( "import(x ? 'y' : 'z')", "x ? import(\"y\") : import(\"z\");\n");
    expectPrinted( "import(x ? 'y' : 'z', {with: {}})",
        "x ? import(\"y\", { with: {} }) : import(\"z\", { with: {} });\n");
    expectPrinted( "import(x ? 'y' : 'z', {with: {a: 'b'}})",
        "x ? import(\"y\", { with: { a: \"b\" } }) : import(\"z\", { with: { a: \"b\" } });\n");
    expectPrinted( "import(x ? 'y' : 'z', {with: {'a': 'b'}})",
        "x ? import(\"y\", { with: { \"a\": \"b\" } }) : import(\"z\", { with: { \"a\": \"b\" } });\n");
    expectPrintedMangle( "import(x ? 'y' : 'z', {with: {'a': 'b'}})",
        "x ? import(\"y\", { with: { a: \"b\" } }) : import(\"z\", { with: { a: \"b\" } });\n");
    expectPrintedMangle( "import(x ? 'y' : 'z', {with: {'a a': 'b'}})",
        "x ? import(\"y\", { with: { \"a a\": \"b\" } }) : import(\"z\", { with: { \"a a\": \"b\" } });\n");
    expectPrinted( "import(x ? 'y' : 'z', {})", "import(x ? \"y\" : \"z\", {});\n");
    expectPrinted( "import(x ? 'y' : 'z', {with: []})", "import(x ? \"y\" : \"z\", { with: [] });\n");
    expectPrinted( "import(x ? 'y' : 'z', {whithe: {}})", "import(x ? \"y\" : \"z\", { whithe: {} });\n");
    expectPrinted( "import(x ? 'y' : 'z', {with: {x: 1}})", "import(x ? \"y\" : \"z\", { with: { x: 1 } });\n");
    expectPrintedTarget( 2015, "import 'x' with {x: 'y'}", "import \"x\";\n");
    expectPrintedTarget( 2015, "import(x, {with: {x: 'y'}})", "import(x);\n");
    expectPrintedTarget( 2015, "import(x, {with: {x: 1}})", "import(x);\n");
    expectPrintedTarget( 2015, "import(x ? 'y' : 'z', {with: {x: 'y'}})", "x ? import(\"y\") : import(\"z\");\n");
    expectPrintedTarget( 2015, "import(x ? 'y' : 'z', {with: {x: 1}})", "import(x ? \"y\" : \"z\");\n");
    expectParseErrorTarget( 2015, "import(x ? 'y' : 'z', {with: {x: foo()}})",
        "<stdin>: ERROR: Using an arbitrary value as the second argument to \"import()\" is not possible in the configured target environment\n");
    // Make sure there are no errors when bundling is disabled
    expectParseError( "import { foo } from 'x' with {type: 'json'}", "");
    expectParseError( "export { foo } from 'x' with {type: 'json'}", "");
    // Only omit the second argument to "import()" if both assertions and attributes aren't supported
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kImportAssertions,
        "import 'x' with {y: 'z'}; import('x', {with: {y: 'z'}})",
        "import \"x\" with { y: \"z\" };\nimport(\"x\", { with: { y: \"z\" } });\n");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kImportAttributes,
        "import 'x' with {y: 'z'}; import('x', {with: {y: 'z'}})",
        "import \"x\";\nimport(\"x\", { with: { y: \"z\" } });\n");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kImportAssertions|compat::JSFeature::kImportAttributes,
        "import 'x' with {y: 'z'}; import('x', {with: {y: 'z'}})",
        "import \"x\";\nimport(\"x\");\n");
    // Test the migration warning
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kImportAssertions,
        "import x from 'y' assert {type: 'json'}",
        "<stdin>: WARNING: The \"assert\" keyword is not supported in the configured target environment\nNOTE: Did you mean to use \"with\" instead of \"assert\"?\n");
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kImportAssertions,
        "export {default} from 'y' assert {type: 'json'}",
        "<stdin>: WARNING: The \"assert\" keyword is not supported in the configured target environment\nNOTE: Did you mean to use \"with\" instead of \"assert\"?\n");
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kImportAssertions,
        "import('y', {assert: {type: 'json'}})",
        "<stdin>: WARNING: The \"assert\" keyword is not supported in the configured target environment\nNOTE: Did you mean to use \"with\" instead of \"assert\"?\n");
}

TEST(JsParser, TestES5) {
    // Do not generate "let" when emulating block-level function declarations and targeting ES5
    expectPrintedTarget( 2015, "if (1) function f() {}", "if (1) {\n  let f = function() {\n  };\n  var f = f;\n}\n");
    expectPrintedTarget( 5, "if (1) function f() {}", "if (1) {\n  var f = function() {\n  };\n  var f = f;\n}\n");
    expectParseErrorTarget( 5, "function foo(x = 0) {}",
        "<stdin>: ERROR: Transforming default arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(function(x = 0) {})",
        "<stdin>: ERROR: Transforming default arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(x = 0) => {}",
        "<stdin>: ERROR: Transforming default arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "function foo(...x) {}",
        "<stdin>: ERROR: Transforming rest arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(function(...x) {})",
        "<stdin>: ERROR: Transforming rest arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(...x) => {}",
        "<stdin>: ERROR: Transforming rest arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "foo(...x)",
        "<stdin>: ERROR: Transforming rest arguments to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "[...x]",
        "<stdin>: ERROR: Transforming array spread to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "for (var x of y) ;",
        "<stdin>: ERROR: Transforming for-of loops to the configured target environment is not supported yet\n");
    expectPrintedTarget( 5, "({ x })", "({ x: x });\n");
    expectParseErrorTarget( 5, "({ [x]: y })",
        "<stdin>: ERROR: Transforming object literal extensions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "({ x() {} });",
        "<stdin>: ERROR: Transforming object literal extensions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "({ get x() {} });", "");
    expectParseErrorTarget( 5, "({ set x(x) {} });", "");
    expectParseErrorTarget( 5, "({ get [x]() {} });",
        "<stdin>: ERROR: Transforming object literal extensions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "({ set [x](x) {} });",
        "<stdin>: ERROR: Transforming object literal extensions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "function foo([]) {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "function foo({}) {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(function([]) {})",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(function({}) {})",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "([]) => {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "({}) => {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "var [] = [];",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "var {} = {};",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "([] = []);",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "({} = {});",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "for ([] in []);",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "for ({} in []);",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "function foo([...x]) {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(function([...x]) {})",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "([...x]) => {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "function foo([...[x]]) {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n"
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n"
        "<stdin>: ERROR: Transforming non-identifier array rest patterns to the configured target environment is not supported yet\n"
        "");
    expectParseErrorTarget( 5, "(function([...[x]]) {})",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n"
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n"
        "<stdin>: ERROR: Transforming non-identifier array rest patterns to the configured target environment is not supported yet\n"
        "");
    expectParseErrorTarget( 5, "([...[x]]) => {}",
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n"
        "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n"
        "<stdin>: ERROR: Transforming non-identifier array rest patterns to the configured target environment is not supported yet\n"
        "");
    expectParseErrorTarget( 5, "([...[x]])",
        "<stdin>: ERROR: Transforming array spread to the configured target environment is not supported yet\n");
    expectPrintedTarget( 5, "`abc`;", "\"abc\";\n");
    expectPrintedTarget( 5, "`a${b}`;", "\"a\".concat(b);\n");
    expectPrintedTarget( 5, "`${a}b`;", "\"\".concat(a, \"b\");\n");
    expectPrintedTarget( 5, "`${a}${b}`;", "\"\".concat(a).concat(b);\n");
    expectPrintedTarget( 5, "`a${b}c`;", "\"a\".concat(b, \"c\");\n");
    expectPrintedTarget( 5, "`a${b}${c}`;", "\"a\".concat(b).concat(c);\n");
    expectPrintedTarget( 5, "`a${b}${c}d`;", "\"a\".concat(b).concat(c, \"d\");\n");
    expectPrintedTarget( 5, "`a${b}c${d}`;", "\"a\".concat(b, \"c\").concat(d);\n");
    expectPrintedTarget( 5, "`a${b}c${d}e`;", "\"a\".concat(b, \"c\").concat(d, \"e\");\n");
    expectPrintedTarget( 5, "tag``;", "var _a;\ntag(_a || (_a = __template([\"\"])));\n");
    expectPrintedTarget( 5, "tag`abc`;", "var _a;\ntag(_a || (_a = __template([\"abc\"])));\n");
    expectPrintedTarget( 5, "tag`\\utf`;", "var _a;\ntag(_a || (_a = __template([void 0], [\"\\\\utf\"])));\n");
    expectPrintedTarget( 5, "tag`${a}b`;", "var _a;\ntag(_a || (_a = __template([\"\", \"b\"])), a);\n");
    expectPrintedTarget( 5, "tag`a${b}`;", "var _a;\ntag(_a || (_a = __template([\"a\", \"\"])), b);\n");
    expectPrintedTarget( 5, "tag`a${b}c`;", "var _a;\ntag(_a || (_a = __template([\"a\", \"c\"])), b);\n");
    expectPrintedTarget( 5, "tag`a${b}\\u`;", "var _a;\ntag(_a || (_a = __template([\"a\", void 0], [\"a\", \"\\\\u\"])), b);\n");
    expectPrintedTarget( 5, "tag`\\u${b}c`;", "var _a;\ntag(_a || (_a = __template([void 0, \"c\"], [\"\\\\u\", \"c\"])), b);\n");
    expectParseErrorTarget( 5, "class Foo { constructor() { new.target } }",
        "<stdin>: ERROR: Transforming class syntax to the configured target environment is not supported yet\n" "<stdin>: ERROR: Transforming object literal extensions to the configured target environment is not supported yet\n" "<stdin>: ERROR: Transforming new.target to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "const x = 1;",
        "<stdin>: ERROR: Transforming const to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "let x = 2;",
        "<stdin>: ERROR: Transforming let to the configured target environment is not supported yet\n");
    expectPrintedTarget( 5, "async => foo;", "(function(async) {\n  return foo;\n});\n");
    expectPrintedTarget( 5, "x => x;", "(function(x) {\n  return x;\n});\n");
    expectParseErrorTarget( 5, "async () => foo;",
        "<stdin>: ERROR: Transforming async functions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "class Foo {}",
        "<stdin>: ERROR: Transforming class syntax to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(class {});",
        "<stdin>: ERROR: Transforming class syntax to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "function* gen() {}",
        "<stdin>: ERROR: Transforming generator functions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "(function* () {});",
        "<stdin>: ERROR: Transforming generator functions to the configured target environment is not supported yet\n");
    expectParseErrorTarget( 5, "({ *foo() {} });",
        "<stdin>: ERROR: Transforming generator functions to the configured target environment is not supported yet\n");
}

TEST(JsParser, TestASCIIOnly) {
    const std::string es5 = "<stdin>: ERROR: \"𐀀\" cannot be escaped in the configured target environment " "but you can set the charset to \"utf8\" to allow unescaped Unicode characters\n";
    // Some context: "π" is in the BMP (i.e. has a code point ≤0xFFFF) and "𐀀" is
    // not in the BMP (i.e. has a code point >0xFFFF). This distinction matters
    // because it's impossible to escape non-BMP characters before ES6.
    expectPrinted( "π", "π;\n");
    expectPrinted( "𐀀", "𐀀;\n");
    expectPrintedASCII( "π", "\\u03C0;\n");
    expectPrintedASCII( "𐀀", "\\u{10000};\n");
    expectPrintedTargetASCII( 5, "π", "\\u03C0;\n");
    expectParseErrorTargetASCII( 5, "𐀀", es5);
    expectPrinted( "var π", "var π;\n");
    expectPrinted( "var 𐀀", "var 𐀀;\n");
    expectPrintedASCII( "var π", "var \\u03C0;\n");
    expectPrintedASCII( "var 𐀀", "var \\u{10000};\n");
    expectPrintedTargetASCII( 5, "var π", "var \\u03C0;\n");
    expectParseErrorTargetASCII( 5, "var 𐀀", es5);
    expectPrinted( "'π'", "\"π\";\n");
    expectPrinted( "'𐀀'", "\"𐀀\";\n");
    expectPrintedASCII( "'π'", "\"\\u03C0\";\n");
    expectPrintedASCII( "'𐀀'", "\"\\u{10000}\";\n");
    expectPrintedTargetASCII( 5, "'π'", "\"\\u03C0\";\n");
    expectPrintedTargetASCII( 5, "'𐀀'", "\"\\uD800\\uDC00\";\n");
    expectPrinted( "x.π", "x.π;\n");
    expectPrinted( "x.𐀀", "x[\"𐀀\"];\n");
    expectPrintedASCII( "x.π", "x.\\u03C0;\n");
    expectPrintedASCII( "x.𐀀", "x[\"\\u{10000}\"];\n");
    expectPrintedTargetASCII( 5, "x.π", "x.\\u03C0;\n");
    expectPrintedTargetASCII( 5, "x.𐀀", "x[\"\\uD800\\uDC00\"];\n");
    expectPrinted( "x?.π", "x?.π;\n");
    expectPrinted( "x?.𐀀", "x?.[\"𐀀\"];\n");
    expectPrintedASCII( "x?.π", "x?.\\u03C0;\n");
    expectPrintedASCII( "x?.𐀀", "x?.[\"\\u{10000}\"];\n");
    expectPrintedTargetASCII( 5, "x?.π", "x == null ? void 0 : x.\\u03C0;\n");
    expectPrintedTargetASCII( 5, "x?.𐀀", "x == null ? void 0 : x[\"\\uD800\\uDC00\"];\n");
    expectPrinted( "0 .π", "0 .π;\n");
    expectPrinted( "0 .𐀀", "0[\"𐀀\"];\n");
    expectPrintedASCII( "0 .π", "0 .\\u03C0;\n");
    expectPrintedASCII( "0 .𐀀", "0[\"\\u{10000}\"];\n");
    expectPrintedTargetASCII( 5, "0 .π", "0 .\\u03C0;\n");
    expectPrintedTargetASCII( 5, "0 .𐀀", "0[\"\\uD800\\uDC00\"];\n");
    expectPrinted( "0?.π", "0?.π;\n");
    expectPrinted( "0?.𐀀", "0?.[\"𐀀\"];\n");
    expectPrintedASCII( "0?.π", "0?.\\u03C0;\n");
    expectPrintedASCII( "0?.𐀀", "0?.[\"\\u{10000}\"];\n");
    expectPrintedTargetASCII( 5, "0?.π", "0 == null ? void 0 : 0 .\\u03C0;\n");
    expectPrintedTargetASCII( 5, "0?.𐀀", "0 == null ? void 0 : 0[\"\\uD800\\uDC00\"];\n");
    expectPrinted( "import 'π'", "import \"π\";\n");
    expectPrinted( "import '𐀀'", "import \"𐀀\";\n");
    expectPrintedASCII( "import 'π'", "import \"\\u03C0\";\n");
    expectPrintedASCII( "import '𐀀'", "import \"\\u{10000}\";\n");
    expectPrintedTargetASCII( 5, "import 'π'", "import \"\\u03C0\";\n");
    expectPrintedTargetASCII( 5, "import '𐀀'", "import \"\\uD800\\uDC00\";\n");
    expectPrinted( "({π: 0})", "({ π: 0 });\n");
    expectPrinted( "({𐀀: 0})", "({ \"𐀀\": 0 });\n");
    expectPrintedASCII( "({π: 0})", "({ \\u03C0: 0 });\n");
    expectPrintedASCII( "({𐀀: 0})", "({ \"\\u{10000}\": 0 });\n");
    expectPrintedTargetASCII( 5, "({π: 0})", "({ \\u03C0: 0 });\n");
    expectPrintedTargetASCII( 5, "({𐀀: 0})", "({ \"\\uD800\\uDC00\": 0 });\n");
    expectPrinted( "({π})", "({ π });\n");
    expectPrinted( "({𐀀})", "({ \"𐀀\": 𐀀 });\n");
    expectPrintedASCII( "({π})", "({ \\u03C0 });\n");
    expectPrintedASCII( "({𐀀})", "({ \"\\u{10000}\": \\u{10000} });\n");
    expectPrintedTargetASCII( 5, "({π})", "({ \\u03C0: \\u03C0 });\n");
    expectParseErrorTargetASCII( 5, "({𐀀})", es5);
    expectPrinted( "import * as π from 'path'; π", "import * as π from \"path\";\nπ;\n");
    expectPrinted( "import * as 𐀀 from 'path'; 𐀀", "import * as 𐀀 from \"path\";\n𐀀;\n");
    expectPrintedASCII( "import * as π from 'path'; π", "import * as \\u03C0 from \"path\";\n\\u03C0;\n");
    expectPrintedASCII( "import * as 𐀀 from 'path'; 𐀀", "import * as \\u{10000} from \"path\";\n\\u{10000};\n");
    expectPrintedTargetASCII( 5, "import * as π from 'path'; π", "import * as \\u03C0 from \"path\";\n\\u03C0;\n");
    expectParseErrorTargetASCII( 5, "import * as 𐀀 from 'path'", es5);
    expectPrinted( "import {π} from 'path'; π", "import { π } from \"path\";\nπ;\n");
    expectPrinted( "import {𐀀} from 'path'; 𐀀", "import { 𐀀 } from \"path\";\n𐀀;\n");
    expectPrintedASCII( "import {π} from 'path'; π", "import { \\u03C0 } from \"path\";\n\\u03C0;\n");
    expectPrintedASCII( "import {𐀀} from 'path'; 𐀀", "import { \\u{10000} } from \"path\";\n\\u{10000};\n");
    expectPrintedTargetASCII( 5, "import {π} from 'path'; π", "import { \\u03C0 } from \"path\";\n\\u03C0;\n");
    expectParseErrorTargetASCII( 5, "import {𐀀} from 'path'", es5);
    expectPrinted( "import {π as x} from 'path'", "import { π as x } from \"path\";\n");
    expectPrinted( "import {𐀀 as x} from 'path'", "import { 𐀀 as x } from \"path\";\n");
    expectPrintedASCII( "import {π as x} from 'path'", "import { \\u03C0 as x } from \"path\";\n");
    expectPrintedASCII( "import {𐀀 as x} from 'path'", "import { \\u{10000} as x } from \"path\";\n");
    expectPrintedTargetASCII( 5, "import {π as x} from 'path'", "import { \\u03C0 as x } from \"path\";\n");
    expectParseErrorTargetASCII( 5, "import {𐀀 as x} from 'path'", es5);
    expectPrinted( "import {x as π} from 'path'", "import { x as π } from \"path\";\n");
    expectPrinted( "import {x as 𐀀} from 'path'", "import { x as 𐀀 } from \"path\";\n");
    expectPrintedASCII( "import {x as π} from 'path'", "import { x as \\u03C0 } from \"path\";\n");
    expectPrintedASCII( "import {x as 𐀀} from 'path'", "import { x as \\u{10000} } from \"path\";\n");
    expectPrintedTargetASCII( 5, "import {x as π} from 'path'", "import { x as \\u03C0 } from \"path\";\n");
    expectParseErrorTargetASCII( 5, "import {x as 𐀀} from 'path'", es5);
    expectPrinted( "export * as π from 'path'; π", "export * as π from \"path\";\nπ;\n");
    expectPrinted( "export * as 𐀀 from 'path'; 𐀀", "export * as 𐀀 from \"path\";\n𐀀;\n");
    expectPrintedASCII( "export * as π from 'path'; π", "export * as \\u03C0 from \"path\";\n\\u03C0;\n");
    expectPrintedASCII( "export * as 𐀀 from 'path'; 𐀀", "export * as \\u{10000} from \"path\";\n\\u{10000};\n");
    expectPrintedTargetASCII( 5, "export * as π from 'path'", "import * as \\u03C0 from \"path\";\nexport { \\u03C0 };\n");
    expectParseErrorTargetASCII( 5, "export * as 𐀀 from 'path'", es5);
    expectPrinted( "export {π} from 'path'; π", "export { π } from \"path\";\nπ;\n");
    expectPrinted( "export {𐀀} from 'path'; 𐀀", "export { 𐀀 } from \"path\";\n𐀀;\n");
    expectPrintedASCII( "export {π} from 'path'; π", "export { \\u03C0 } from \"path\";\n\\u03C0;\n");
    expectPrintedASCII( "export {𐀀} from 'path'; 𐀀", "export { \\u{10000} } from \"path\";\n\\u{10000};\n");
    expectPrintedTargetASCII( 5, "export {π} from 'path'; π", "export { \\u03C0 } from \"path\";\n\\u03C0;\n");
    expectParseErrorTargetASCII( 5, "export {𐀀} from 'path'", es5);
    expectPrinted( "export {π as x} from 'path'", "export { π as x } from \"path\";\n");
    expectPrinted( "export {𐀀 as x} from 'path'", "export { 𐀀 as x } from \"path\";\n");
    expectPrintedASCII( "export {π as x} from 'path'", "export { \\u03C0 as x } from \"path\";\n");
    expectPrintedASCII( "export {𐀀 as x} from 'path'", "export { \\u{10000} as x } from \"path\";\n");
    expectPrintedTargetASCII( 5, "export {π as x} from 'path'", "export { \\u03C0 as x } from \"path\";\n");
    expectParseErrorTargetASCII( 5, "export {𐀀 as x} from 'path'", es5);
    expectPrinted( "export {x as π} from 'path'", "export { x as π } from \"path\";\n");
    expectPrinted( "export {x as 𐀀} from 'path'", "export { x as 𐀀 } from \"path\";\n");
    expectPrintedASCII( "export {x as π} from 'path'", "export { x as \\u03C0 } from \"path\";\n");
    expectPrintedASCII( "export {x as 𐀀} from 'path'", "export { x as \\u{10000} } from \"path\";\n");
    expectPrintedTargetASCII( 5, "export {x as π} from 'path'", "export { x as \\u03C0 } from \"path\";\n");
    expectParseErrorTargetASCII( 5, "export {x as 𐀀} from 'path'", es5);
    expectPrinted( "export {π}; var π", "export { π };\nvar π;\n");
    expectPrinted( "export {𐀀}; var 𐀀", "export { 𐀀 };\nvar 𐀀;\n");
    expectPrintedASCII( "export {π}; var π", "export { \\u03C0 };\nvar \\u03C0;\n");
    expectPrintedASCII( "export {𐀀}; var 𐀀", "export { \\u{10000} };\nvar \\u{10000};\n");
    expectPrintedTargetASCII( 5, "export {π}; var π", "export { \\u03C0 };\nvar \\u03C0;\n");
    expectParseErrorTargetASCII( 5, "export {𐀀}; var 𐀀", es5);
    expectPrinted( "export var π", "export var π;\n");
    expectPrinted( "export var 𐀀", "export var 𐀀;\n");
    expectPrintedASCII( "export var π", "export var \\u03C0;\n");
    expectPrintedASCII( "export var 𐀀", "export var \\u{10000};\n");
    expectPrintedTargetASCII( 5, "export var π", "export var \\u03C0;\n");
    expectParseErrorTargetASCII( 5, "export var 𐀀", es5);
}

TEST(JsParser, TestMangleCatch) {
    expectPrintedMangle( "try { throw 0 } catch (e) { console.log(0) }", "try {\n  throw 0;\n} catch {\n  console.log(0);\n}\n");
    expectPrintedMangle( "try { throw 0 } catch (e) { console.log(0, e) }", "try {\n  throw 0;\n} catch (e) {\n  console.log(0, e);\n}\n");
    expectPrintedMangle( "try { throw 0 } catch (e) { 0 && console.log(0, e) }", "try {\n  throw 0;\n} catch {\n}\n");
    expectPrintedMangle( "try { thrower() } catch ([a]) { console.log(0) }", "try {\n  thrower();\n} catch ([a]) {\n  console.log(0);\n}\n");
    expectPrintedMangle( "try { thrower() } catch ({ a }) { console.log(0) }", "try {\n  thrower();\n} catch ({ a }) {\n  console.log(0);\n}\n");
    expectPrintedMangleTarget( 2018, "try { throw 0 } catch (e) { console.log(0) }", "try {\n  throw 0;\n} catch (e) {\n  console.log(0);\n}\n");
    expectPrintedMangle( "try { throw 1 } catch (x) { y(x); var x = 2; y(x) }", "try {\n  throw 1;\n} catch (x) {\n  y(x);\n  var x = 2;\n  y(x);\n}\n");
    expectPrintedMangle( "try { throw 1 } catch (x) { var x = 2; y(x) }", "try {\n  throw 1;\n} catch (x) {\n  var x = 2;\n  y(x);\n}\n");
    expectPrintedMangle( "try { throw 1 } catch (x) { var x = 2 }", "try {\n  throw 1;\n} catch (x) {\n  var x = 2;\n}\n");
    expectPrintedMangle( "try { throw 1 } catch (x) { eval('x') }", "try {\n  throw 1;\n} catch (x) {\n  eval(\"x\");\n}\n");
    expectPrintedMangle( "if (y) try { throw 1 } catch (x) {} else eval('x')", "if (y) try {\n  throw 1;\n} catch {\n}\nelse eval(\"x\");\n");
}

TEST(JsParser, TestMangleTry) {
    expectPrintedMangle( "try { throw 0 } catch (e) { foo() }", "try {\n  throw 0;\n} catch {\n  foo();\n}\n");
    expectPrintedMangle( "try {} catch (e) { var foo }", "try {\n} catch {\n  var foo;\n}\n");
    expectPrintedMangle( "try {} catch (e) { foo() }", "");
    expectPrintedMangle( "try {} catch (e) { foo() } finally {}", "");
    expectPrintedMangle( "try {} finally { foo() }", "foo();\n");
    expectPrintedMangle( "try {} catch (e) { foo() } finally { bar() }", "bar();\n");
    expectPrintedMangle( "try {} finally { var x = foo() }", "var x = foo();\n");
    expectPrintedMangle( "try {} catch (e) { foo() } finally { var x = bar() }", "var x = bar();\n");
    expectPrintedMangle( "try {} finally { let x = foo() }", "{\n  let x = foo();\n}\n");
    expectPrintedMangle( "try {} catch (e) { foo() } finally { let x = bar() }", "{\n  let x = bar();\n}\n");
    expectPrintedMangle( "try { foo() } catch {}", "try {\n  foo();\n} catch {\n}\n");
    expectPrintedMangle( "try { foo() } catch {} finally {}", "try {\n  foo();\n} catch {\n}\n");
    expectPrintedMangle( "try { foo() } finally {}", "foo();\n");
    expectPrintedMangle( "try { var x = foo() } catch {}", "try {\n  var x = foo();\n} catch {\n}\n");
    expectPrintedMangle( "try { var x = foo() } catch {} finally {}", "try {\n  var x = foo();\n} catch {\n}\n");
    expectPrintedMangle( "try { var x = foo() } finally {}", "var x = foo();\n");
    expectPrintedMangle( "try { let x = foo() } catch {}", "try {\n  let x = foo();\n} catch {\n}\n");
    expectPrintedMangle( "try { let x = foo() } catch {} finally {}", "try {\n  let x = foo();\n} catch {\n}\n");
    expectPrintedMangle( "try { let x = foo() } finally {}", "{\n  let x = foo();\n}\n");
    expectPrintedMangle( "x: try { while (true) ; break x } catch {}", "x: try {\n  for (; ; ) ;\n  break x;\n} catch {\n}\n");
    expectPrintedMangle( "d: { e: { try { while (1) { break d } } catch { break e } } }",
        "d:\n  e:\n    try {\n      for (; ; )\n        break d;\n    } catch {\n      break e;\n    }\n");
}

TEST(JsParser, TestAutoPureForObjectCreate) {
    expectPrinted( "Object.create(null)", "/* @__PURE__ */ Object.create(null);\n");
    expectPrinted( "Object.create({})", "/* @__PURE__ */ Object.create({});\n");
    expectPrinted( "Object.create()", "Object.create();\n");
    expectPrinted( "Object.create(x)", "Object.create(x);\n");
    expectPrinted( "Object.create(undefined)", "Object.create(void 0);\n");
}

TEST(JsParser, TestAutoPureForSet) {
    expectPrinted( "new Set", "/* @__PURE__ */ new Set();\n");
    expectPrinted( "new Set(null)", "/* @__PURE__ */ new Set(null);\n");
    expectPrinted( "new Set(undefined)", "/* @__PURE__ */ new Set(void 0);\n");
    expectPrinted( "new Set([])", "/* @__PURE__ */ new Set([]);\n");
    expectPrinted( "new Set([x])", "/* @__PURE__ */ new Set([x]);\n");
    expectPrinted( "new Set(x)", "new Set(x);\n");
    expectPrinted( "new Set(false)", "new Set(false);\n");
    expectPrinted( "new Set({})", "new Set({});\n");
    expectPrinted( "new Set({ x })", "new Set({ x });\n");
}

TEST(JsParser, TestAutoPureForMap) {
    expectPrinted( "new Map", "/* @__PURE__ */ new Map();\n");
    expectPrinted( "new Map(null)", "/* @__PURE__ */ new Map(null);\n");
    expectPrinted( "new Map(undefined)", "/* @__PURE__ */ new Map(void 0);\n");
    expectPrinted( "new Map([])", "/* @__PURE__ */ new Map([]);\n");
    expectPrinted( "new Map([[]])", "/* @__PURE__ */ new Map([[]]);\n");
    expectPrinted( "new Map([[], []])", "/* @__PURE__ */ new Map([[], []]);\n");
    expectPrinted( "new Map(x)", "new Map(x);\n");
    expectPrinted( "new Map(false)", "new Map(false);\n");
    expectPrinted( "new Map([x])", "new Map([x]);\n");
    expectPrinted( "new Map([x, []])", "new Map([x, []]);\n");
    expectPrinted( "new Map([[], x])", "new Map([[], x]);\n");
}

TEST(JsParser, TestAutoPureForWeakSet) {
    expectPrinted( "new WeakSet", "/* @__PURE__ */ new WeakSet();\n");
    expectPrinted( "new WeakSet(null)", "/* @__PURE__ */ new WeakSet(null);\n");
    expectPrinted( "new WeakSet(undefined)", "/* @__PURE__ */ new WeakSet(void 0);\n");
    expectPrinted( "new WeakSet([])", "/* @__PURE__ */ new WeakSet([]);\n");
    expectPrinted( "new WeakSet([x])", "new WeakSet([x]);\n");
    expectPrinted( "new WeakSet(x)", "new WeakSet(x);\n");
    expectPrinted( "new WeakSet(false)", "new WeakSet(false);\n");
    expectPrinted( "new WeakSet({})", "new WeakSet({});\n");
    expectPrinted( "new WeakSet({ x })", "new WeakSet({ x });\n");
}

TEST(JsParser, TestAutoPureForWeakMap) {
    expectPrinted( "new WeakMap", "/* @__PURE__ */ new WeakMap();\n");
    expectPrinted( "new WeakMap(null)", "/* @__PURE__ */ new WeakMap(null);\n");
    expectPrinted( "new WeakMap(undefined)", "/* @__PURE__ */ new WeakMap(void 0);\n");
    expectPrinted( "new WeakMap([])", "/* @__PURE__ */ new WeakMap([]);\n");
    expectPrinted( "new WeakMap([[]])", "new WeakMap([[]]);\n");
    expectPrinted( "new WeakMap([[], []])", "new WeakMap([[], []]);\n");
    expectPrinted( "new WeakMap(x)", "new WeakMap(x);\n");
    expectPrinted( "new WeakMap(false)", "new WeakMap(false);\n");
    expectPrinted( "new WeakMap([x])", "new WeakMap([x]);\n");
    expectPrinted( "new WeakMap([x, []])", "new WeakMap([x, []]);\n");
    expectPrinted( "new WeakMap([[], x])", "new WeakMap([[], x]);\n");
}

TEST(JsParser, TestAutoPureForDate) {
    expectPrinted( "new Date", "/* @__PURE__ */ new Date();\n");
    expectPrinted( "new Date(0)", "/* @__PURE__ */ new Date(0);\n");
    expectPrinted( "new Date('')", "/* @__PURE__ */ new Date(\"\");\n");
    expectPrinted( "new Date(null)", "/* @__PURE__ */ new Date(null);\n");
    expectPrinted( "new Date(true)", "/* @__PURE__ */ new Date(true);\n");
    expectPrinted( "new Date(false)", "/* @__PURE__ */ new Date(false);\n");
    expectPrinted( "new Date(undefined)", "/* @__PURE__ */ new Date(void 0);\n");
    expectPrinted( "new Date(`${foo}`)", "/* @__PURE__ */ new Date(`${foo}`);\n");
    expectPrinted( "new Date(foo ? 'x' : 'y')", "/* @__PURE__ */ new Date(foo ? \"x\" : \"y\");\n");
    expectPrinted( "new Date(foo)", "new Date(foo);\n");
    expectPrinted( "new Date(foo``)", "new Date(foo``);\n");
    expectPrinted( "new Date(foo ? x : y)", "new Date(foo ? x : y);\n");
}

TEST(JsParser, TestAutoPureForRegExpEscape) {
    expectPrinted( "RegExp.escape('x')", "/* @__PURE__ */ RegExp.escape(\"x\");\n");
    expectPrinted( "RegExp.escape(`${x}`)", "/* @__PURE__ */ RegExp.escape(`${x}`);\n");
    expectPrinted( "RegExp.escape(x ? 'y' : 'z')", "/* @__PURE__ */ RegExp.escape(x ? \"y\" : \"z\");\n");
    expectPrinted( "RegExp.escape()", "RegExp.escape();\n");
    expectPrinted( "RegExp.escape(x`y`)", "RegExp.escape(x`y`);\n");
    expectPrinted( "RegExp.escape('x', 'y')", "RegExp.escape(\"x\", \"y\");\n");
    expectPrinted( "RegExp.escape(x ? 'y' : z)", "RegExp.escape(x ? \"y\" : z);\n");
    expectPrinted( "RegExp.escape(x ? y : 'z')", "RegExp.escape(x ? y : \"z\");\n");
}

// See: https://github.com/tc39/proposal-explicit-resource-management
TEST(JsParser, TestUsing) {
    expectPrinted( "using x = y", "using x = y;\n");
    expectPrinted( "using x = y; z", "using x = y;\nz;\n");
    expectPrinted( "using x = y, z = _", "using x = y, z = _;\n");
    expectPrinted( "using x = y, \n z = _", "using x = y, z = _;\n");
    expectPrinted( "using \n x = y", "using;\nx = y;\n");
    expectPrinted( "using [x]", "using[x];\n");
    expectPrinted( "using [x] = y", "using[x] = y;\n");
    expectPrinted( "using \n [x] = y", "using[x] = y;\n");
    expectParseError( "using x", "<stdin>: ERROR: The declaration \"x\" must be initialized\n");
    expectParseError( "using {x}", "<stdin>: ERROR: Expected \";\" but found \"{\"\n");
    expectParseError( "using x = y, z", "<stdin>: ERROR: The declaration \"z\" must be initialized\n");
    expectParseError( "using x = y, [z] = _", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseError( "using x = y, {z} = _", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseError( "export using x = y", "<stdin>: ERROR: Unexpected \"using\"\n");
    expectPrinted( "for (using x = y;;) ;", "for (using x = y; ; ) ;\n");
    expectPrinted( "for (using x of y) ;", "for (using x of y) ;\n");
    expectPrinted( "for (using of x) ;", "for (using of x) ;\n");
    expectPrinted( "for (using of of) ;", "for (using of of) ;\n");
    expectPrinted( "for (await using of of x) ;", "for (await using of of x) ;\n");
    expectPrinted( "for (await using of of of) ;", "for (await using of of of) ;\n");
    expectPrinted( "for await (using x of y) ;", "for await (using x of y) ;\n");
    expectPrinted( "for await (using of x) ;", "for await (using of x) ;\n");
    expectParseError( "for (using of of x) ;", "<stdin>: ERROR: Expected \")\" but found \"x\"\n");
    expectParseError( "for (using of of of) ;", "<stdin>: ERROR: Expected \")\" but found \"of\"\n");
    expectParseError( "for (using x in y) ;", "<stdin>: ERROR: \"using\" declarations are not allowed here\n");
    expectParseError( "for (using x;;) ;", "<stdin>: ERROR: The declaration \"x\" must be initialized\n");
    expectParseError( "for (using x = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (using \n x of y) ;", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "for await (using x = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for await (using \n x of y) ;", "<stdin>: ERROR: Expected \"of\" but found \"x\"\n");
    expectPrinted( "await using \n x = y", "await using;\nx = y;\n");
    expectPrinted( "await \n using \n x \n = \n y", "await using;\nx = y;\n");
    expectPrinted( "await using [x]", "await using[x];\n");
    expectPrinted( "await using ([x] = y)", "await using([x] = y);\n");
    expectPrinted( "await (using [x] = y)", "await (using[x] = y);\n");
    expectParseError( "await using [x] = y", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseError( "for (await using x in y) ;", "<stdin>: ERROR: \"await using\" declarations are not allowed here\n");
    expectParseError( "for (await using x = y;;) ;", "<stdin>: ERROR: \"await using\" declarations are not allowed here\n");
    expectParseError( "for (await using of x) ;", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "for (await using x = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for (await using \n x of y) ;", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseError( "for await (await using of x) ;", "<stdin>: ERROR: Expected \"of\" but found \"x\"\n");
    expectParseError( "for await (await using x = y of z) ;", "<stdin>: ERROR: for-of loop variables cannot have an initializer\n");
    expectParseError( "for await (await using \n x of y) ;", "<stdin>: ERROR: Expected \"of\" but found \"x\"\n");
    expectPrinted( "await using x = y", "await using x = y;\n");
    expectPrinted( "await using x = y, z = _", "await using x = y, z = _;\n");
    expectPrinted( "for (await using x of y) ;", "for (await using x of y) ;\n");
    expectPrinted( "for await (await using x of y) ;", "for await (await using x of y) ;\n");
    expectPrinted( "function foo() { using x = y }", "function foo() {\n  using x = y;\n}\n");
    expectPrinted( "foo = function() { using x = y }", "foo = function() {\n  using x = y;\n};\n");
    expectPrinted( "foo = () => { using x = y }", "foo = () => {\n  using x = y;\n};\n");
    expectPrinted( "async function foo() { using x = y }", "async function foo() {\n  using x = y;\n}\n");
    expectPrinted( "foo = async function() { using x = y }", "foo = async function() {\n  using x = y;\n};\n");
    expectPrinted( "foo = async () => { using x = y }", "foo = async () => {\n  using x = y;\n};\n");
    expectPrinted( "async function foo() { await using x = y }", "async function foo() {\n  await using x = y;\n}\n");
    expectPrinted( "foo = async function() { await using x = y }", "foo = async function() {\n  await using x = y;\n};\n");
    expectPrinted( "foo = async () => { await using x = y }", "foo = async () => {\n  await using x = y;\n};\n");
    expectParseError( "export using x = y", "<stdin>: ERROR: Unexpected \"using\"\n");
    expectParseError( "export await using x = y", "<stdin>: ERROR: Unexpected \"await\"\n");
    const std::string needAsync = "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n<stdin>: NOTE: Consider adding the \"async\" keyword here:\n";
    expectParseError( "function foo() { await using x = y }", needAsync);
    expectParseError( "foo = function() { await using x = y }", needAsync);
    expectParseError( "foo = () => { await using x = y }", needAsync);
    // Can't use await at the top-level without top-level await
    const std::string err = "<stdin>: ERROR: Top-level await is not available in the configured target environment\n";
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "await using x = y;", err);
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "for (await using x of y) ;", err);
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "if (true) { await using x = y }", err);
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "if (true) for (await using x of y) ;", err);
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "if (false) { await using x = y }", "if (false) {\n  using x = y;\n}\n");
    expectPrintedWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "if (false) for (await using x of y) ;", "if (false) for (using x of y) ;\n");
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "with (x) y; if (false) { await using x = y }",
        "<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n" "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n");
    expectParseErrorWithUnsupportedFeatures( compat::JSFeature::kTopLevelAwait, "with (x) y; if (false) for (await using x of y) ;",
        "<stdin>: ERROR: With statements cannot be used in an ECMAScript module\n" "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the top-level \"await\" keyword here:\n");
    // Optimization: "using" declarations initialized to null or undefined can avoid the "using" machinery
    expectPrinted( "using x = {}", "using x = {};\n");
    expectPrinted( "using x = null", "using x = null;\n");
    expectPrinted( "using x = undefined", "using x = void 0;\n");
    expectPrinted( "using x = (foo, y)", "using x = (foo, y);\n");
    expectPrinted( "using x = (foo, null)", "using x = (foo, null);\n");
    expectPrinted( "using x = (foo, undefined)", "using x = (foo, void 0);\n");
    expectPrintedMangle( "using x = {}", "using x = {};\n");
    expectPrintedMangle( "using x = null", "const x = null;\n");
    expectPrintedMangle( "using x = undefined", "const x = void 0;\n");
    expectPrintedMangle( "using x = (foo, y)", "using x = (foo, y);\n");
    expectPrintedMangle( "using x = (foo, null)", "const x = (foo, null);\n");
    expectPrintedMangle( "using x = (foo, undefined)", "const x = (foo, void 0);\n");
    expectPrintedMangle( "using x = null, y = undefined", "const x = null, y = void 0;\n");
    expectPrintedMangle( "using x = null, y = z", "using x = null, y = z;\n");
    expectPrintedMangle( "using x = z, y = undefined", "using x = z, y = void 0;\n");
}

