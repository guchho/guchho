#include "test/helpers/javascript_test.hpp"

TEST(JsParser, TestTSTypes) {
    expectPrintedTS("let x: T extends number\n ? T\n : number", "let x;\n");
    expectPrintedTS("let x: {y: T extends number ? T : number}", "let x;\n");
    expectPrintedTS("let x: {y: T \n extends: number}", "let x;\n");
    expectPrintedTS("let x: {y: T \n extends?: number}", "let x;\n");
    expectPrintedTS("let x: (number | string)[]", "let x;\n");
    expectPrintedTS("let x: [string[]?]", "let x;\n");
    expectPrintedTS("let x: [number?, string?]", "let x;\n");
    expectPrintedTS("let x: [a: number, b?: string, ...c: number[]]", "let x;\n");
    expectPrintedTS("type x =\n A\n | B\n C", "C;\n");
    expectPrintedTS("type x =\n | A\n | B\n C", "C;\n");
    expectPrintedTS("type x =\n A\n & B\n C", "C;\n");
    expectPrintedTS("type x =\n & A\n & B\n C", "C;\n");
    expectPrintedTS("type x = [-1, 0, 1]\n[]", "[];\n");
    expectPrintedTS("type x = [-1n, 0n, 1n]\n[]", "[];\n");
    expectPrintedTS("type x = {0: number, readonly 1: boolean}\n[]", "[];\n");
    expectPrintedTS("type x = {'a': number, readonly 'b': boolean}\n[]", "[];\n");
    expectPrintedTS("type\nFoo = {}", "type;\nFoo = {};\n");
    expectPrintedTS("export type\n{ Foo } \n x", "x;\n");
    expectPrintedTS("export type\n* from 'foo' \n x", "x;\n");
    expectPrintedTS("export type\n* as ns from 'foo' \n x", "x;\n");
    expectParseErrorTS("export type\nFoo = {}", "<stdin>: ERROR: Unexpected newline after \"type\"\n");
    expectPrintedTS("let x: {x: 'a', y: false, z: null}", "let x;\n");
    expectPrintedTS("let x: {foo(): void}", "let x;\n");
    expectPrintedTS("let x: {['x']: number}", "let x;\n");
    expectPrintedTS("let x: {['x'](): void}", "let x;\n");
    expectPrintedTS("let x: {[key: string]: number}", "let x;\n");
    expectPrintedTS("let x: {[keyof: string]: number}", "let x;\n");
    expectPrintedTS("let x: {[readonly: string]: number}", "let x;\n");
    expectPrintedTS("let x: {[infer: string]: number}", "let x;\n");
    expectPrintedTS("let x: [keyof: string]", "let x;\n");
    expectPrintedTS("let x: [readonly: string]", "let x;\n");
    expectPrintedTS("let x: [infer: string]", "let x;\n");
    expectParseErrorTS("let x: A extends B ? keyof : string", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorTS("let x: A extends B ? readonly : string", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorTS("let x: A extends B ? infer : string", "<stdin>: ERROR: Expected identifier but found \":\"\n");
    expectParseErrorTS("let x: {[new: string]: number}", "<stdin>: ERROR: Expected \"(\" but found \":\"\n");
    expectParseErrorTS("let x: {[import: string]: number}", "<stdin>: ERROR: Expected \"(\" but found \":\"\n");
    expectParseErrorTS("let x: {[typeof: string]: number}", "<stdin>: ERROR: Expected identifier but found \":\"\n");
    expectPrintedTS("let x: () => void = Foo", "let x = Foo;\n");
    expectPrintedTS("let x: new () => void = Foo", "let x = Foo;\n");
    expectPrintedTS("let x = 'x' as keyof T", "let x = \"x\";\n");
    expectPrintedTS("let x = [1] as readonly [number]", "let x = [1];\n");
    expectPrintedTS("let x = 'x' as keyof typeof Foo", "let x = \"x\";\n");
    expectPrintedTS("let fs: typeof import('fs') = require('fs')", "let fs = require(\"fs\");\n");
    expectPrintedTS("let fs: typeof import('fs').exists = require('fs').exists", "let fs = require(\"fs\").exists;\n");
    expectPrintedTS("let fs: typeof import('fs', { assert: { type: 'json' } }) = require('fs')", "let fs = require(\"fs\");\n");
    expectPrintedTS("let fs: typeof import('fs', { assert: { 'resolution-mode': 'import' } }) = require('fs')", "let fs = require(\"fs\");\n");
    expectPrintedTS("let x: <T>() => Foo<T>", "let x;\n");
    expectPrintedTS("let x: new <T>() => Foo<T>", "let x;\n");
    expectPrintedTS("let x: <T extends object>() => Foo<T>", "let x;\n");
    expectPrintedTS("let x: new <T extends object>() => Foo<T>", "let x;\n");
    expectPrintedTS("type Foo<T> = {[P in keyof T]?: T[P]}", "");
    expectPrintedTS("type Foo<T> = {[P in keyof T]+?: T[P]}", "");
    expectPrintedTS("type Foo<T> = {[P in keyof T]-?: T[P]}", "");
    expectPrintedTS("type Foo<T> = {readonly [P in keyof T]: T[P]}", "");
    expectPrintedTS("type Foo<T> = {-readonly [P in keyof T]: T[P]}", "");
    expectPrintedTS("type Foo<T> = {+readonly [P in keyof T]: T[P]}", "");
    expectPrintedTS("type Foo<T> = {[infer in T]?: Foo}", "");
    expectPrintedTS("type Foo<T> = {[keyof in T]?: Foo}", "");
    expectPrintedTS("type Foo<T> = {[asserts in T]?: Foo}", "");
    expectPrintedTS("type Foo<T> = {[abstract in T]?: Foo}", "");
    expectPrintedTS("type Foo<T> = {[readonly in T]?: Foo}", "");
    expectPrintedTS("type Foo<T> = {[satisfies in T]?: Foo}", "");
    expectPrintedTS("let x: number! = y", "let x = y;\n");
    expectPrintedTS("let x: number \n !y", "let x;\n!y;\n");
    expectPrintedTS("const x: unique = y", "const x = y;\n");
    expectPrintedTS("const x: unique<T> = y", "const x = y;\n");
    expectPrintedTS("const x: unique\nsymbol = y", "const x = y;\n");
    expectPrintedTS("let x: typeof a = y", "let x = y;\n");
    expectPrintedTS("let x: typeof a.b = y", "let x = y;\n");
    expectPrintedTS("let x: typeof a.if = y", "let x = y;\n");
    expectPrintedTS("let x: typeof if.a = y", "let x = y;\n");
    expectPrintedTS("let x: typeof readonly = y", "let x = y;\n");
    expectParseErrorTS("let x: typeof readonly Array", "<stdin>: ERROR: Expected \";\" but found \"Array\"\n");
    expectPrintedTS("let x: `y`", "let x;\n");
    expectParseErrorTS("let x: tag`y`", "<stdin>: ERROR: Expected \";\" but found \"`y`\"\n");
    expectPrintedTS("let x: { <A extends B>(): c.d \n <E extends F>(): g.h }", "let x;\n");
    expectPrintedTSX("type x = a.b \n <c></c>", "/* @__PURE__ */ React.createElement(\"c\", null);\n");
    expectPrintedTS("type Foo = a.b \n | c.d", "");
    expectPrintedTS("type Foo = a.b \n & c.d", "");
    expectPrintedTS("type Foo = \n | a.b \n | c.d", "");
    expectPrintedTS("type Foo = \n & a.b \n & c.d", "");
    expectPrintedTS("type Foo = Bar extends [infer T] ? T : null", "");
    expectPrintedTS("type Foo = Bar extends [infer T extends string] ? T : null", "");
    expectPrintedTS("type Foo = {} extends infer T extends {} ? A<T> : never", "");
    expectPrintedTS("type Foo = {} extends (infer T extends {}) ? A<T> : never", "");
    expectPrintedTS("type Foo<T> = T extends { a: infer U extends number } | { b: infer U extends number } ? U : never", "");
    expectPrintedTS("type Foo<T> = T extends { a: infer U extends number } & { b: infer U extends number } ? U : never", "");
    expectPrintedTS("type Foo<T> = T extends { a: infer U extends number } | infer U extends number ? U : never", "");
    expectPrintedTS("type Foo<T> = T extends { a: infer U extends number } & infer U extends number ? U : never", "");
    expectPrintedTS("let x: A extends B<infer C extends D> ? D : never", "let x;\n");
    expectPrintedTS("let x: A extends B<infer C extends D ? infer C : never> ? D : never", "let x;\n");
    expectPrintedTS("let x: ([e1, e2, ...es]: any) => any", "let x;\n");
    expectPrintedTS("let x: (...[e1, e2, es]: any) => any", "let x;\n");
    expectPrintedTS("let x: (...[e1, e2, ...es]: any) => any", "let x;\n");
    expectPrintedTS("let x: (y, [e1, e2, ...es]: any) => any", "let x;\n");
    expectPrintedTS("let x: (y, ...[e1, e2, es]: any) => any", "let x;\n");
    expectPrintedTS("let x: (y, ...[e1, e2, ...es]: any) => any", "let x;\n");

    expectPrintedTS("let x: A.B<X.Y>", "let x;\n");
    expectPrintedTS("let x: A.B<X.Y>=2", "let x = 2;\n");
    expectPrintedTS("let x: A.B<X.Y<Z>>", "let x;\n");
    expectPrintedTS("let x: A.B<X.Y<Z>>=2", "let x = 2;\n");
    expectPrintedTS("let x: A.B<X.Y<Z<T>>>", "let x;\n");
    expectPrintedTS("let x: A.B<X.Y<Z<T>>>=2", "let x = 2;\n");

    expectPrintedTS("(): A<T>=> 0", "() => 0;\n");
    expectPrintedTS("(): A<B<T>>=> 0", "() => 0;\n");
    expectPrintedTS("(): A<B<C<T>>>=> 0", "() => 0;\n");

    expectPrintedTS("let foo: any\n<x>y", "let foo;\ny;\n");
    expectPrintedTSX("let foo: any\n<x>y</x>", "let foo;\n/* @__PURE__ */ React.createElement(\"x\", null, \"y\");\n");
    expectParseErrorTS("let foo: (any\n<x>y)", "<stdin>: ERROR: Expected \")\" but found \"<\"\n");

    expectPrintedTS("let foo = bar as (null)", "let foo = bar;\n");
    expectPrintedTS("let foo = bar\nas (null)", "let foo = bar;\nas(null);\n");
    expectParseErrorTS("let foo = (bar\nas (null))", "<stdin>: ERROR: Expected \")\" but found \"as\"\n");

    expectPrintedTS("a as any ? b : c;", "a ? b : c;\n");
    expectPrintedTS("a as any ? async () => b : c;", "a ? async () => b : c;\n");
    expectPrintedTS("foo as number extends Object ? any : any;", "foo;\n");
    expectPrintedTS("foo as number extends Object ? () => void : any;", "foo;\n");
    expectPrintedTS("let a = b ? c : d as T extends T ? T extends T ? T : never : never ? e : f;", "let a = b ? c : d ? e : f;\n");
    expectParseErrorTS("type a = b extends c", "<stdin>: ERROR: Expected \"?\" but found end of file\n");
    expectParseErrorTS("type a = b extends c extends d", "<stdin>: ERROR: Expected \"?\" but found \"extends\"\n");
    expectParseErrorTS("type a = b ? c : d", "<stdin>: ERROR: Expected \";\" but found \"?\"\n");

    expectPrintedTS("let foo: keyof Object = 'toString'", "let foo = \"toString\";\n");
    expectPrintedTS("let foo: keyof\nObject = 'toString'", "let foo = \"toString\";\n");
    expectPrintedTS("let foo: (keyof\nObject) = 'toString'", "let foo = \"toString\";\n");

    expectPrintedTS("type Foo = Array<<T>(x: T) => T>\n x", "x;\n");
    expectPrintedTSX("<Foo<<T>(x: T) => T>/>", "/* @__PURE__ */ React.createElement(Foo, null);\n");

    expectPrintedTS("interface Foo<> {}", "");
    expectPrintedTSX("interface Foo<> {}", "");
    expectPrintedTS("type Foo<> = {}", "");
    expectPrintedTSX("type Foo<> = {}", "");
    expectParseErrorTS("class Foo<> {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTSX("class Foo<> {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("class Foo { foo<>() {} }", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTSX("class Foo { foo<>() {} }", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("type Foo = { foo<>(): void }", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTSX("type Foo = { foo<>(): void }", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("type Foo = <>() => {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTSX("type Foo = <>() => {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("let Foo = <>() => {}", "<stdin>: ERROR: Unexpected \">\"\n");
    expectParseErrorTSX("let Foo = <>() => {}",
        "<stdin>: ERROR: The character \">\" is not valid inside a JSX element\nNOTE: Did you mean to escape it as \"{'>'}\" instead?\n"
        "<stdin>: ERROR: Unexpected end of file before a closing fragment tag\n<stdin>: NOTE: The opening fragment tag is here:\n");

    // Certain built-in types do not accept type parameters
    expectPrintedTS("x as 1 < 1", "x < 1;\n");
    expectPrintedTS("x as 1n < 1", "x < 1;\n");
    expectPrintedTS("x as -1 < 1", "x < 1;\n");
    expectPrintedTS("x as -1n < 1", "x < 1;\n");
    expectPrintedTS("x as '' < 1", "x < 1;\n");
    expectPrintedTS("x as `` < 1", "x < 1;\n");
    expectPrintedTS("x as any < 1", "x < 1;\n");
    expectPrintedTS("x as bigint < 1", "x < 1;\n");
    expectPrintedTS("x as false < 1", "x < 1;\n");
    expectPrintedTS("x as never < 1", "x < 1;\n");
    expectPrintedTS("x as null < 1", "x < 1;\n");
    expectPrintedTS("x as number < 1", "x < 1;\n");
    expectPrintedTS("x as object < 1", "x < 1;\n");
    expectPrintedTS("x as string < 1", "x < 1;\n");
    expectPrintedTS("x as symbol < 1", "x < 1;\n");
    expectPrintedTS("x as this < 1", "x < 1;\n");
    expectPrintedTS("x as true < 1", "x < 1;\n");
    expectPrintedTS("x as undefined < 1", "x < 1;\n");
    expectPrintedTS("x as unique symbol < 1", "x < 1;\n");
    expectPrintedTS("x as unknown < 1", "x < 1;\n");
    expectPrintedTS("x as void < 1", "x < 1;\n");
    expectParseErrorTS("x as Foo < 1", "<stdin>: ERROR: Expected \">\" but found end of file\n");

    // These keywords are valid tuple labels
    expectPrintedTS("type _any = [any: string]", "");
    expectPrintedTS("type _asserts = [asserts: string]", "");
    expectPrintedTS("type _bigint = [bigint: string]", "");
    expectPrintedTS("type _boolean = [boolean: string]", "");
    expectPrintedTS("type _false = [false: string]", "");
    expectPrintedTS("type _function = [function: string]", "");
    expectPrintedTS("type _import = [import: string]", "");
    expectPrintedTS("type _infer = [infer: string]", "");
    expectPrintedTS("type _never = [never: string]", "");
    expectPrintedTS("type _new = [new: string]", "");
    expectPrintedTS("type _null = [null: string]", "");
    expectPrintedTS("type _number = [number: string]", "");
    expectPrintedTS("type _object = [object: string]", "");
    expectPrintedTS("type _readonly = [readonly: string]", "");
    expectPrintedTS("type _string = [string: string]", "");
    expectPrintedTS("type _symbol = [symbol: string]", "");
    expectPrintedTS("type _this = [this: string]", "");
    expectPrintedTS("type _true = [true: string]", "");
    expectPrintedTS("type _typeof = [typeof: string]", "");
    expectPrintedTS("type _undefined = [undefined: string]", "");
    expectPrintedTS("type _unique = [unique: string]", "");
    expectPrintedTS("type _unknown = [unknown: string]", "");
    expectPrintedTS("type _void = [void: string]", "");

    // Also check tuple labels with a question mark
    expectPrintedTS("type _any = [any?: string]", "");
    expectPrintedTS("type _asserts = [asserts?: string]", "");
    expectPrintedTS("type _bigint = [bigint?: string]", "");
    expectPrintedTS("type _boolean = [boolean?: string]", "");
    expectPrintedTS("type _false = [false?: string]", "");
    expectPrintedTS("type _function = [function?: string]", "");
    expectPrintedTS("type _import = [import?: string]", "");
    expectPrintedTS("type _infer = [infer?: string]", "");
    expectPrintedTS("type _never = [never?: string]", "");
    expectPrintedTS("type _new = [new?: string]", "");
    expectPrintedTS("type _null = [null?: string]", "");
    expectPrintedTS("type _number = [number?: string]", "");
    expectPrintedTS("type _object = [object?: string]", "");
    expectPrintedTS("type _readonly = [readonly?: string]", "");
    expectPrintedTS("type _string = [string?: string]", "");
    expectPrintedTS("type _symbol = [symbol?: string]", "");
    expectPrintedTS("type _this = [this?: string]", "");
    expectPrintedTS("type _true = [true?: string]", "");
    expectPrintedTS("type _typeof = [typeof?: string]", "");
    expectPrintedTS("type _undefined = [undefined?: string]", "");
    expectPrintedTS("type _unique = [unique?: string]", "");
    expectPrintedTS("type _unknown = [unknown?: string]", "");
    expectPrintedTS("type _void = [void?: string]", "");

    // These keywords are invalid tuple labels
    expectParseErrorTS("type _break = [break: string]", "<stdin>: ERROR: Unexpected \"break\"\n");
    expectParseErrorTS("type _case = [case: string]", "<stdin>: ERROR: Unexpected \"case\"\n");
    expectParseErrorTS("type _catch = [catch: string]", "<stdin>: ERROR: Unexpected \"catch\"\n");
    expectParseErrorTS("type _class = [class: string]", "<stdin>: ERROR: Unexpected \"class\"\n");
    expectParseErrorTS("type _const = [const: string]", "<stdin>: ERROR: Unexpected \"const\"\n");
    expectParseErrorTS("type _continue = [continue: string]", "<stdin>: ERROR: Unexpected \"continue\"\n");
    expectParseErrorTS("type _debugger = [debugger: string]", "<stdin>: ERROR: Unexpected \"debugger\"\n");
    expectParseErrorTS("type _default = [default: string]", "<stdin>: ERROR: Unexpected \"default\"\n");
    expectParseErrorTS("type _delete = [delete: string]", "<stdin>: ERROR: Unexpected \"delete\"\n");
    expectParseErrorTS("type _do = [do: string]", "<stdin>: ERROR: Unexpected \"do\"\n");
    expectParseErrorTS("type _else = [else: string]", "<stdin>: ERROR: Unexpected \"else\"\n");
    expectParseErrorTS("type _enum = [enum: string]", "<stdin>: ERROR: Unexpected \"enum\"\n");
    expectParseErrorTS("type _export = [export: string]", "<stdin>: ERROR: Unexpected \"export\"\n");
    expectParseErrorTS("type _extends = [extends: string]", "<stdin>: ERROR: Unexpected \"extends\"\n");
    expectParseErrorTS("type _finally = [finally: string]", "<stdin>: ERROR: Unexpected \"finally\"\n");
    expectParseErrorTS("type _for = [for: string]", "<stdin>: ERROR: Unexpected \"for\"\n");
    expectParseErrorTS("type _if = [if: string]", "<stdin>: ERROR: Unexpected \"if\"\n");
    expectParseErrorTS("type _in = [in: string]", "<stdin>: ERROR: Unexpected \"in\"\n");
    expectParseErrorTS("type _instanceof = [instanceof: string]", "<stdin>: ERROR: Unexpected \"instanceof\"\n");
    expectParseErrorTS("type _return = [return: string]", "<stdin>: ERROR: Unexpected \"return\"\n");
    expectParseErrorTS("type _super = [super: string]", "<stdin>: ERROR: Unexpected \"super\"\n");
    expectParseErrorTS("type _switch = [switch: string]", "<stdin>: ERROR: Unexpected \"switch\"\n");
    expectParseErrorTS("type _throw = [throw: string]", "<stdin>: ERROR: Unexpected \"throw\"\n");
    expectParseErrorTS("type _try = [try: string]", "<stdin>: ERROR: Unexpected \"try\"\n");
    expectParseErrorTS("type _var = [var: string]", "<stdin>: ERROR: Unexpected \"var\"\n");
    expectParseErrorTS("type _while = [while: string]", "<stdin>: ERROR: Unexpected \"while\"\n");
    expectParseErrorTS("type _with = [with: string]", "<stdin>: ERROR: Unexpected \"with\"\n");

    // TypeScript 4.1
    expectPrintedTS("let foo: `${'a' | 'b'}-${'c' | 'd'}` = 'a-c'", "let foo = \"a-c\";\n");

    // TypeScript 4.2
    expectPrintedTS("let x: abstract new () => void = Foo", "let x = Foo;\n");
    expectPrintedTS("let x: abstract new <T>() => Foo<T>", "let x;\n");
    expectPrintedTS("let x: abstract new <T extends object>() => Foo<T>", "let x;\n");
    expectParseErrorTS("let x: abstract () => void = Foo", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseErrorTS("let x: abstract <T>() => Foo<T>", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseErrorTS("let x: abstract <T extends object>() => Foo<T>", "<stdin>: ERROR: Expected \"?\" but found \">\"\n");

    // TypeScript 4.7
    const std::string jsxErrorArrow = "<stdin>: ERROR: The character \">\" is not valid inside a JSX element\n"
        "NOTE: Did you mean to escape it as \"{'>'}\" instead?\n";
    expectPrintedTS("type Foo<in T> = T", "");
    expectPrintedTS("type Foo<out T> = T", "");
    expectPrintedTS("type Foo<in out> = T", "");
    expectPrintedTS("type Foo<out out> = T", "");
    expectPrintedTS("type Foo<in out out> = T", "");
    expectPrintedTS("type Foo<in X, out Y> = [X, Y]", "");
    expectPrintedTS("type Foo<out X, in Y> = [X, Y]", "");
    expectPrintedTS("type Foo<out X, out Y extends keyof X> = [X, Y]", "");
    expectParseErrorTS("type Foo<i\\u006E T> = T", "<stdin>: ERROR: Expected identifier but found \"i\\\\u006E\"\n");
    expectParseErrorTS("type Foo<ou\\u0074 T> = T", "<stdin>: ERROR: Expected \">\" but found \"T\"\n");
    expectParseErrorTS("type Foo<in in> = T", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("type Foo<out in> = T", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("type Foo<out in T> = T", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("type Foo<public T> = T", "<stdin>: ERROR: Expected \">\" but found \"T\"\n");
    expectParseErrorTS("type Foo<in out in T> = T", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("type Foo<in out out T> = T", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectPrintedTS("class Foo<in T> {}", "class Foo {\n}\n");
    expectPrintedTS("class Foo<out T> {}", "class Foo {\n}\n");
    expectPrintedTS("export default class Foo<in T> {}", "export default class Foo {\n}\n");
    expectPrintedTS("export default class Foo<out T> {}", "export default class Foo {\n}\n");
    expectPrintedTS("export default class <in T> {}", "export default class {\n}\n");
    expectPrintedTS("export default class <out T> {}", "export default class {\n}\n");
    expectPrintedTS("interface Foo<in T> {}", "");
    expectPrintedTS("interface Foo<out T> {}", "");
    expectPrintedTS("declare class Foo<in T> {}", "");
    expectPrintedTS("declare class Foo<out T> {}", "");
    expectPrintedTS("declare interface Foo<in T> {}", "");
    expectPrintedTS("declare interface Foo<out T> {}", "");
    expectParseErrorTS("function foo<in T>() {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("function foo<out T>() {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("export default function foo<in T>() {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("export default function foo<out T>() {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("export default function <in T>() {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("export default function <out T>() {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let foo: Foo<in T>", "<stdin>: ERROR: Unexpected \"in\"\n");
    expectParseErrorTS("let foo: Foo<out T>", "<stdin>: ERROR: Expected \">\" but found \"T\"\n");
    expectParseErrorTS("declare function foo<in T>()", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("declare function foo<out T>()", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("declare let foo: Foo<in T>", "<stdin>: ERROR: Unexpected \"in\"\n");
    expectParseErrorTS("declare let foo: Foo<out T>", "<stdin>: ERROR: Expected \">\" but found \"T\"\n");
    expectPrintedTS("Foo = class <in T> {}", "Foo = class {\n};\n");
    expectPrintedTS("Foo = class <out T> {}", "Foo = class {\n};\n");
    expectPrintedTS("Foo = class Bar<in T> {}", "Foo = class Bar {\n};\n");
    expectPrintedTS("Foo = class Bar<out T> {}", "Foo = class Bar {\n};\n");
    expectParseErrorTS("foo = function <in T>() {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("foo = function <out T>() {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("class Foo { foo<in T>(): T {} }", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("class Foo { foo<out T>(): T {} }", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("foo = { foo<in T>(): T {} }", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("foo = { foo<out T>(): T {} }", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("<in T>() => {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("<out T>() => {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("<in T, out T>() => {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let x: <in T>() => {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("let x: <out T>() => {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let x: <in T, out T>() => {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let x: new <in T>() => {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("let x: new <out T>() => {}", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let x: new <in T, out T>() => {}", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let x: { y<in T>(): any }", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n");
    expectParseErrorTS("let x: { y<out T>(): any }", "<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectParseErrorTS("let x: { y<in T, out T>(): any }", "<stdin>: ERROR: The modifier \"in\" is not valid here:\n<stdin>: ERROR: The modifier \"out\" is not valid here:\n");
    expectPrintedTSX("<in T></in>", "/* @__PURE__ */ React.createElement(\"in\", { T: true });\n");
    expectPrintedTSX("<out T></out>", "/* @__PURE__ */ React.createElement(\"out\", { T: true });\n");
    expectPrintedTSX("<in out T></in>", "/* @__PURE__ */ React.createElement(\"in\", { out: true, T: true });\n");
    expectPrintedTSX("<out in T></out>", "/* @__PURE__ */ React.createElement(\"out\", { in: true, T: true });\n");
    expectPrintedTSX("<in T extends={true}></in>", "/* @__PURE__ */ React.createElement(\"in\", { T: true, extends: true });\n");
    expectPrintedTSX("<out T extends={true}></out>", "/* @__PURE__ */ React.createElement(\"out\", { T: true, extends: true });\n");
    expectPrintedTSX("<in out T extends={true}></in>", "/* @__PURE__ */ React.createElement(\"in\", { out: true, T: true, extends: true });\n");
    expectParseErrorTSX("<in T,>() => {}", "<stdin>: ERROR: Expected \">\" but found \",\"\n");
    expectParseErrorTSX("<out T,>() => {}", "<stdin>: ERROR: Expected \">\" but found \",\"\n");
    expectParseErrorTSX("<in out T,>() => {}", "<stdin>: ERROR: Expected \">\" but found \",\"\n");
    expectParseErrorTSX("<in T extends any>() => {}", jsxErrorArrow + "<stdin>: ERROR: Unexpected end of file before a closing \"in\" tag\n<stdin>: NOTE: The opening \"in\" tag is here:\n");
    expectParseErrorTSX("<out T extends any>() => {}", jsxErrorArrow + "<stdin>: ERROR: Unexpected end of file before a closing \"out\" tag\n<stdin>: NOTE: The opening \"out\" tag is here:\n");
    expectParseErrorTSX("<in out T extends any>() => {}", jsxErrorArrow + "<stdin>: ERROR: Unexpected end of file before a closing \"in\" tag\n<stdin>: NOTE: The opening \"in\" tag is here:\n");
    expectPrintedTS("class Container { get data(): typeof this.#data {} }", "class Container {\n  get data() {\n  }\n}\n");
    expectPrintedTS("const a: typeof this.#a = 1;", "const a = 1;\n");
    expectParseErrorTS("const a: typeof #a = 1;", "<stdin>: ERROR: Expected identifier but found \"#a\"\n");

    // TypeScript 5.0
    expectPrintedTS("class Foo<const T> {}", "class Foo {\n}\n");
    expectPrintedTS("class Foo<const T extends X> {}", "class Foo {\n}\n");
    expectPrintedTS("Foo = class <const T> {}", "Foo = class {\n};\n");
    expectPrintedTS("Foo = class Bar<const T> {}", "Foo = class Bar {\n};\n");
    expectPrintedTS("function foo<const T>() {}", "function foo() {\n}\n");
    expectPrintedTS("foo = function <const T>() {}", "foo = function() {\n};\n");
    expectPrintedTS("foo = function bar<const T>() {}", "foo = function bar() {\n};\n");
    expectPrintedTS("class Foo { bar<const T>() {} }", "class Foo {\n  bar() {\n  }\n}\n");
    expectPrintedTS("interface Foo { bar<const T>(): T }", "");
    expectPrintedTS("interface Foo { new bar<const T>(): T }", "");
    expectPrintedTS("let x: { bar<const T>(): T }", "let x;\n");
    expectPrintedTS("let x: { new bar<const T>(): T }", "let x;\n");
    expectPrintedTS("foo = { bar<const T>() {} }", "foo = { bar() {\n} };\n");
    expectPrintedTS("x = <const>(y)", "x = y;\n");
    expectPrintedTS("<const T>() => {}", "() => {\n};\n");
    expectPrintedTS("<const const T>() => {}", "() => {\n};\n");
    expectPrintedTS("async <const T>() => {}", "async () => {\n};\n");
    expectPrintedTS("async <const const T>() => {}", "async () => {\n};\n");
    expectPrintedTS("let x: <const T>() => T = y", "let x = y;\n");
    expectPrintedTS("let x: <const const T>() => T = y", "let x = y;\n");
    expectPrintedTS("let x: new <const T>() => T = y", "let x = y;\n");
    expectPrintedTS("let x: new <const const T>() => T = y", "let x = y;\n");
    expectParseErrorTS("type Foo<const T> = T", "<stdin>: ERROR: The modifier \"const\" is not valid here:\n");
    expectParseErrorTS("interface Foo<const T> {}", "<stdin>: ERROR: The modifier \"const\" is not valid here:\n");
    expectParseErrorTS("let x: <const>() => {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("let x: new <const>() => {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("let x: Foo<const T>", "<stdin>: ERROR: Expected \">\" but found \"T\"\n");
    expectParseErrorTS("x = <T,>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("x = <const T>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("x = <T extends X>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("x = async <T,>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("x = async <const T>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("x = async <T extends X>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("x = <const const>() => {}", "<stdin>: ERROR: Expected \">\" but found \"const\"\n");
    expectPrintedTS("class Foo<const const const T> {}", "class Foo {\n}\n");
    expectPrintedTS("class Foo<const in out T> {}", "class Foo {\n}\n");
    expectPrintedTS("class Foo<in const out T> {}", "class Foo {\n}\n");
    expectPrintedTS("class Foo<in out const T> {}", "class Foo {\n}\n");
    expectPrintedTS("class Foo<const in const out const T> {}", "class Foo {\n}\n");
    expectParseErrorTS("class Foo<in const> {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("class Foo<out const> {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("class Foo<in out const> {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectPrintedTSX("<const>(x)</const>", "/* @__PURE__ */ React.createElement(\"const\", null, \"(x)\");\n");
    expectPrintedTSX("<const const/>", "/* @__PURE__ */ React.createElement(\"const\", { const: true });\n");
    expectPrintedTSX("<const const></const>", "/* @__PURE__ */ React.createElement(\"const\", { const: true });\n");
    expectPrintedTSX("<const T/>", "/* @__PURE__ */ React.createElement(\"const\", { T: true });\n");
    expectPrintedTSX("<const T></const>", "/* @__PURE__ */ React.createElement(\"const\", { T: true });\n");
    expectPrintedTSX("<const T>(y) = {}</const>", "/* @__PURE__ */ React.createElement(\"const\", { T: true }, \"(y) = \");\n");
    expectPrintedTSX("<const T extends/>", "/* @__PURE__ */ React.createElement(\"const\", { T: true, extends: true });\n");
    expectPrintedTSX("<const T extends></const>", "/* @__PURE__ */ React.createElement(\"const\", { T: true, extends: true });\n");
    expectPrintedTSX("<const T extends>(y) = {}</const>", "/* @__PURE__ */ React.createElement(\"const\", { T: true, extends: true }, \"(y) = \");\n");
    expectPrintedTSX("<const T,>() => {}", "() => {\n};\n");
    expectPrintedTSX("<const T, X>() => {}", "() => {\n};\n");
    expectPrintedTSX("<const T, const X>() => {}", "() => {\n};\n");
    expectPrintedTSX("<const T, const const X>() => {}", "() => {\n};\n");
    expectPrintedTSX("<const T extends X>() => {}", "() => {\n};\n");
    expectPrintedTSX("async <const T,>() => {}", "async () => {\n};\n");
    expectPrintedTSX("async <const T, X>() => {}", "async () => {\n};\n");
    expectPrintedTSX("async <const T, const X>() => {}", "async () => {\n};\n");
    expectPrintedTSX("async <const T, const const X>() => {}", "async () => {\n};\n");
    expectPrintedTSX("async <const T extends X>() => {}", "async () => {\n};\n");
    expectParseErrorTSX("<const T>() => {}", jsxErrorArrow + "<stdin>: ERROR: Unexpected end of file before a closing \"const\" tag\n<stdin>: NOTE: The opening \"const\" tag is here:\n");
    expectParseErrorTSX("<const const>() => {}", jsxErrorArrow + "<stdin>: ERROR: Unexpected end of file before a closing \"const\" tag\n<stdin>: NOTE: The opening \"const\" tag is here:\n");
    expectParseErrorTSX("<const const T,>() => {}", "<stdin>: ERROR: Expected \">\" but found \",\"\n");
    expectParseErrorTSX("<const const T extends X>() => {}", jsxErrorArrow + "<stdin>: ERROR: Unexpected end of file before a closing \"const\" tag\n<stdin>: NOTE: The opening \"const\" tag is here:\n");
    expectParseErrorTSX("async <const T>() => {}", "<stdin>: ERROR: Unexpected \"const\"\n");
    expectParseErrorTSX("async <const const>() => {}", "<stdin>: ERROR: Unexpected \"const\"\n");
    expectParseErrorTSX("async <const const T,>() => {}", "<stdin>: ERROR: Unexpected \"const\"\n");
    expectParseErrorTSX("async <const const T extends X>() => {}", "<stdin>: ERROR: Unexpected \"const\"\n");
}

TEST(JsParser, TestTSAsCast) {
    expectPrintedTS("x as any\n(y);", "x;\ny;\n");
    expectPrintedTS("x as any\n`y`;", "x;\n`y`;\n");
    expectPrintedTS("x as any\n`${y}`;", "x;\n`${y}`;\n");
    expectPrintedTS("x as any\n--y;", "x;\n--y;\n");
    expectPrintedTS("x as any\n++y;", "x;\n++y;\n");
    expectPrintedTS("x + y as any\n(z as any) + 1;", "x + y;\nz + 1;\n");
    expectPrintedTS("x + y as any\n(z as any) = 1;", "x + y;\nz = 1;\n");
    expectPrintedTS("x = y as any\n(z as any) + 1;", "x = y;\nz + 1;\n");
    expectPrintedTS("x = y as any\n(z as any) = 1;", "x = y;\nz = 1;\n");
    expectPrintedTS("x * y as any\n['z'];", "x * y;\n[\"z\"];\n");
    expectPrintedTS("x * y as any\n.z;", "x * y;\n");
    expectPrintedTS("x as y['x'];", "x;\n");
    expectPrintedTS("x as y!['x'];", "x;\n");
    expectPrintedTS("x as y\n['x'];", "x;\n[\"x\"];\n");
    expectPrintedTS("x as y\n!['x'];", "x;\n![\"x\"];\n");
    expectParseErrorTS("x = y as any `z`;", "<stdin>: ERROR: Expected \";\" but found \"`z`\"\n");
    expectParseErrorTS("x = y as any `${z}`;", "<stdin>: ERROR: Expected \";\" but found \"`${\"\n");
    expectParseErrorTS("x = y as any?.z;", "<stdin>: ERROR: Expected \";\" but found \"?.\"\n");
    expectParseErrorTS("x = y as any--;", "<stdin>: ERROR: Expected \";\" but found \"--\"\n");
    expectParseErrorTS("x = y as any++;", "<stdin>: ERROR: Expected \";\" but found \"++\"\n");
    expectParseErrorTS("x = y as any(z);", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseErrorTS("x = y as any\n= z;", "<stdin>: ERROR: Unexpected \"=\"\n");
    expectParseErrorTS("a, x as y `z`;", "<stdin>: ERROR: Expected \";\" but found \"`z`\"\n");
    expectParseErrorTS("a ? b : x as y `z`;", "<stdin>: ERROR: Expected \";\" but found \"`z`\"\n");
    expectParseErrorTS("x as any = y;", "<stdin>: ERROR: Expected \";\" but found \"=\"\n");
    expectParseErrorTS("(x as any = y);", "<stdin>: ERROR: Expected \")\" but found \"=\"\n");
    expectParseErrorTS("(x = y as any(z));", "<stdin>: ERROR: Expected \")\" but found \"(\"\n");
}

TEST(JsParser, TestTSSatisfies) {
    expectPrintedTS("const t1 = { a: 1 } satisfies I1;", "const t1 = { a: 1 };\n");
    expectPrintedTS("const t2 = { a: 1, b: 1 } satisfies I1;", "const t2 = { a: 1, b: 1 };\n");
    expectPrintedTS("const t3 = { } satisfies I1;", "const t3 = {};\n");
    expectPrintedTS("const t4: T1 = { a: 'a' } satisfies T1;", "const t4 = { a: \"a\" };\n");
    expectPrintedTS("const t5 = (m => m.substring(0)) satisfies T2;", "const t5 = ((m) => m.substring(0));\n");
    expectPrintedTS("const t6 = [1, 2] satisfies [number, number];", "const t6 = [1, 2];\n");
    expectPrintedTS("let t7 = { a: 'test' } satisfies A;", "let t7 = { a: \"test\" };\n");
    expectPrintedTS("let t8 = { a: 'test', b: 'test' } satisfies A;", "let t8 = { a: \"test\", b: \"test\" };\n");
    expectPrintedTS("export default {} satisfies Foo;", "export default {};\n");
    expectPrintedTS("export default { a: 1 } satisfies Foo;", "export default { a: 1 };\n");
    expectPrintedTS(
        "const p = { isEven: n => n % 2 === 0, isOdd: n => n % 2 === 1 } satisfies Predicates;",
        "const p = { isEven: (n) => n % 2 === 0, isOdd: (n) => n % 2 === 1 };\n");
    expectPrintedTS(
        "let obj: { f(s: string): void } & Record<string, unknown> = { f(s) { }, g(s) { } } satisfies { g(s: string): void } & Record<string, unknown>;",
        "let obj = { f(s) {\n}, g(s) {\n} };\n");
    expectPrintedTS(
        "const car = { start() { }, move(d) { }, stop() { } } satisfies Movable & Record<string, unknown>;",
        "const car = { start() {\n}, move(d) {\n}, stop() {\n} };\n");
    expectPrintedTS("var v = undefined satisfies 1;", "var v = void 0;\n");
    expectPrintedTS("const a = { x: 10 } satisfies Partial<Point2d>;", "const a = { x: 10 };\n");
    expectPrintedTS(
        "const p = { a: 0, b: \"hello\", x: 8 } satisfies Partial<Record<Keys, unknown>>;",
        "const p = { a: 0, b: \"hello\", x: 8 };\n");
    expectPrintedTS(
        "const p = { a: 0, b: \"hello\", x: 8 } satisfies Record<Keys, unknown>;",
        "const p = { a: 0, b: \"hello\", x: 8 };\n");
    expectPrintedTS(
        "const x2 = { m: true, s: \"false\" } satisfies Facts;",
        "const x2 = { m: true, s: \"false\" };\n");
    expectPrintedTS(
        "export const Palette = { white: { r: 255, g: 255, b: 255 }, black: { r: 0, g: 0, d: 0 }, blue: { r: 0, g: 0, b: 255 }, } satisfies Record<string, Color>;",
        "export const Palette = { white: { r: 255, g: 255, b: 255 }, black: { r: 0, g: 0, d: 0 }, blue: { r: 0, g: 0, b: 255 } };\n");
    expectPrintedTS(
        "const a: \"baz\" = \"foo\" satisfies \"foo\" | \"bar\";",
        "const a = \"foo\";\n");
    expectPrintedTS(
        "const b: { xyz: \"baz\" } = { xyz: \"foo\" } satisfies { xyz: \"foo\" | \"bar\" };",
        "const b = { xyz: \"foo\" };\n");
}

TEST(JsParser, TestTSClass) {
    expectPrintedTS("export default class Foo {}", "export default class Foo {\n}\n");
    expectPrintedTS("export default class Foo extends Bar<T> {}", "export default class Foo extends Bar {\n}\n");
    expectPrintedTS("export default class Foo extends Bar<T>() {}", "export default class Foo extends Bar() {\n}\n");
    expectPrintedTS("export default class Foo implements Bar<T> {}", "export default class Foo {\n}\n");
    expectPrintedTS("export default class Foo<T> {}", "export default class Foo {\n}\n");
    expectPrintedTS("export default class Foo<T> extends Bar<T> {}", "export default class Foo extends Bar {\n}\n");
    expectPrintedTS("export default class Foo<T> extends Bar<T>() {}", "export default class Foo extends Bar() {\n}\n");
    expectPrintedTS("export default class Foo<T> implements Bar<T> {}", "export default class Foo {\n}\n");
    expectPrintedTS("(class Foo<T> {})", "(class Foo {\n});\n");
    expectPrintedTS("(class Foo<T> extends Bar<T> {})", "(class Foo extends Bar {\n});\n");
    expectPrintedTS("(class Foo<T> extends Bar<T>() {})", "(class Foo extends Bar() {\n});\n");
    expectPrintedTS("(class Foo<T> implements Bar<T> {})", "(class Foo {\n});\n");

    expectPrintedTS("export default class {}", "export default class {\n}\n");
    expectPrintedTS("export default class extends Foo<T> {}", "export default class extends Foo {\n}\n");
    expectPrintedTS("export default class implements Foo<T> {}", "export default class {\n}\n");
    expectPrintedTS("export default class <T> {}", "export default class {\n}\n");
    expectPrintedTS("export default class <T> extends Foo<T> {}", "export default class extends Foo {\n}\n");
    expectPrintedTS("export default class <T> implements Foo<T> {}", "export default class {\n}\n");
    expectPrintedTS("(class <T> {})", "(class {\n});\n");
    expectPrintedTS("(class extends Foo<T> {})", "(class extends Foo {\n});\n");
    expectPrintedTS("(class extends Foo<T>() {})", "(class extends Foo() {\n});\n");
    expectPrintedTS("(class implements Foo<T> {})", "(class {\n});\n");
    expectPrintedTS("(class <T> extends Foo<T> {})", "(class extends Foo {\n});\n");
    expectPrintedTS("(class <T> extends Foo<T>() {})", "(class extends Foo() {\n});\n");
    expectPrintedTS("(class <T> implements Foo<T> {})", "(class {\n});\n");

    // Check ASI for "abstract"
    expectPrintedTS("abstract \n class A {}", "abstract;\nclass A {\n}\n");
    expectPrintedTS("export default abstract \n class A {}", "export default abstract;\nclass A {\n}\n");
    expectPrintedTS("abstract class A { abstract \n foo(): void {} }", "class A {\n  abstract;\n  foo() {\n  }\n}\n");

    expectPrintedTS("abstract class A { abstract foo(): void; bar(): void {} }", "class A {\n  bar() {\n  }\n}\n");
    expectPrintedTS("export abstract class A { abstract foo(): void; bar(): void {} }", "export class A {\n  bar() {\n  }\n}\n");
    expectPrintedTS("export default abstract", "export default abstract;\n");
    expectPrintedTS("export default abstract - after", "export default abstract - after;\n");
    expectPrintedTS("export default abstract class { abstract foo(): void; bar(): void {} } - after", "export default class {\n  bar() {\n  }\n}\n-after;\n");
    expectPrintedTS("export default abstract class A { abstract foo(): void; bar(): void {} } - after", "export default class A {\n  bar() {\n  }\n}\n-after;\n");

    expectPrintedTS("class A<T extends number> extends B.C<D, E> {}", "class A extends B.C {\n}\n");
    expectPrintedTS("class A<T extends number> implements B.C<D, E>, F.G<H, I> {}", "class A {\n}\n");
    expectPrintedTS("class A<T extends number> extends X implements B.C<D, E>, F.G<H, I> {}", "class A extends X {\n}\n");

    const std::string reservedWordError =
        " is a reserved word and cannot be used in strict mode\n"
        "<stdin>: NOTE: All code inside a class is implicitly in strict mode\n";

    expectParseErrorTS("class Foo { constructor(public) {} }", "<stdin>: ERROR: \"public\"" + reservedWordError);
    expectParseErrorTS("class Foo { constructor(protected) {} }", "<stdin>: ERROR: \"protected\"" + reservedWordError);
    expectParseErrorTS("class Foo { constructor(private) {} }", "<stdin>: ERROR: \"private\"" + reservedWordError);
    expectPrintedTS("class Foo { constructor(readonly) {} }", "class Foo {\n  constructor(readonly) {\n  }\n}\n");
    expectPrintedTS("class Foo { constructor(override) {} }", "class Foo {\n  constructor(override) {\n  }\n}\n");

    expectPrintedTS("class Foo { constructor(public x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(protected x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(private x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(override x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(public readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(protected readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(private readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");
    expectPrintedTS("class Foo { constructor(override readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n  x;\n}\n");

    expectPrintedAssignSemanticsTS("class Foo { constructor(public x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(protected x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(private x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(override x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(public readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(protected readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(private readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { constructor(override readonly x) {} }", "class Foo {\n  constructor(x) {\n    this.x = x;\n  }\n}\n");

    expectParseErrorTS("class Foo { constructor(public {x}) {} }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("class Foo { constructor(protected {x}) {} }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("class Foo { constructor(private {x}) {} }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("class Foo { constructor(readonly {x}) {} }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("class Foo { constructor(override {x}) {} }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");

    expectParseErrorTS("class Foo { constructor(public [x]) {} }", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseErrorTS("class Foo { constructor(protected [x]) {} }", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseErrorTS("class Foo { constructor(private [x]) {} }", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseErrorTS("class Foo { constructor(readonly [x]) {} }", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseErrorTS("class Foo { constructor(override [x]) {} }", "<stdin>: ERROR: Expected identifier but found \"[\"\n");

    expectPrintedAssignSemanticsTS("class Foo { foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo: number = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { ['foo']: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { ['foo']: number = 0 }", "class Foo {\n  constructor() {\n    this[\"foo\"] = 0;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo(): void {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo(): void; foo(): void {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectParseErrorTS("class Foo { foo(): void foo(): void {} }", "<stdin>: ERROR: Expected \";\" but found \"foo\"\n");

    expectPrintedAssignSemanticsTS("class Foo { foo?: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo?: number = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo?(): void {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo?(): void; foo(): void {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectParseErrorTS("class Foo { foo?(): void foo(): void {} }", "<stdin>: ERROR: Expected \";\" but found \"foo\"\n");

    expectPrintedAssignSemanticsTS("class Foo { foo!: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { foo!: number = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");
    expectParseErrorTS("class Foo { foo!() {} }", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseErrorTS("class Foo { *foo!() {} }", "<stdin>: ERROR: Expected \"(\" but found \"!\"\n");
    expectParseErrorTS("class Foo { get foo!() {} }", "<stdin>: ERROR: Expected \"(\" but found \"!\"\n");
    expectParseErrorTS("class Foo { set foo!(x) {} }", "<stdin>: ERROR: Expected \"(\" but found \"!\"\n");
    expectParseErrorTS("class Foo { async foo!() {} }", "<stdin>: ERROR: Expected \"(\" but found \"!\"\n");

    expectPrintedAssignSemanticsTS("class Foo { 'foo' = 0 }", "class Foo {\n  constructor() {\n    this[\"foo\"] = 0;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { ['foo'] = 0 }", "class Foo {\n  constructor() {\n    this[\"foo\"] = 0;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { [foo] = 0 }", "var _a;\n_a = foo;\nclass Foo {\n  constructor() {\n    this[_a] = 0;\n  }\n}\n");
    expectPrintedMangleAssignSemanticsTS("class Foo { 'foo' = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");
    expectPrintedMangleAssignSemanticsTS("class Foo { ['foo'] = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");

    expectPrintedAssignSemanticsTS("class Foo { foo \n ?: number }", "class Foo {\n}\n");
    expectParseErrorTS("class Foo { foo \n !: number }", "<stdin>: ERROR: Expected identifier but found \"!\"\n");

    expectPrintedAssignSemanticsTS("class Foo { public foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { private foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { protected foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare public foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { public declare foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { override foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { override public foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { public override foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare override public foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare foo = 123 }", "class Foo {\n}\n");

    expectPrintedAssignSemanticsTS("class Foo { public static foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { private static foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { protected static foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare static foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare public static foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { public declare static foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { public static declare foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { override static foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { override public static foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { public override static foo: number }", "class Foo {\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { public static override foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare override public static foo: number }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { declare static foo = 123 }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { static declare foo = 123 }", "class Foo {\n}\n");

    expectParseErrorTS("class Foo { declare #foo }", "<stdin>: ERROR: \"declare\" cannot be used with a private identifier\n");
    expectParseErrorTS("class Foo { declare [foo: string]: number }", "<stdin>: ERROR: \"declare\" cannot be used with an index signature\n");
    expectParseErrorTS("class Foo { declare foo() }", "<stdin>: ERROR: \"declare\" cannot be used with a method\n");
    expectParseErrorTS("class Foo { declare get foo() }", "<stdin>: ERROR: \"declare\" cannot be used with a getter\n");
    expectParseErrorTS("class Foo { declare set foo(x) }", "<stdin>: ERROR: \"declare\" cannot be used with a setter\n");

    expectParseErrorTS("class Foo { declare static #foo }", "<stdin>: ERROR: \"declare\" cannot be used with a private identifier\n");
    expectParseErrorTS("class Foo { declare static [foo: string]: number }", "<stdin>: ERROR: \"declare\" cannot be used with an index signature\n");
    expectParseErrorTS("class Foo { declare static foo() }", "<stdin>: ERROR: \"declare\" cannot be used with a method\n");
    expectParseErrorTS("class Foo { declare static get foo() }", "<stdin>: ERROR: \"declare\" cannot be used with a getter\n");
    expectParseErrorTS("class Foo { declare static set foo(x) }", "<stdin>: ERROR: \"declare\" cannot be used with a setter\n");

    expectParseErrorTS("class Foo { static declare #foo }", "<stdin>: ERROR: \"declare\" cannot be used with a private identifier\n");
    expectParseErrorTS("class Foo { static declare [foo: string]: number }", "<stdin>: ERROR: \"declare\" cannot be used with an index signature\n");
    expectParseErrorTS("class Foo { static declare foo() }", "<stdin>: ERROR: \"declare\" cannot be used with a method\n");
    expectParseErrorTS("class Foo { static declare get foo() }", "<stdin>: ERROR: \"declare\" cannot be used with a getter\n");
    expectParseErrorTS("class Foo { static declare set foo(x) }", "<stdin>: ERROR: \"declare\" cannot be used with a setter\n");

    expectPrintedAssignSemanticsTS("class Foo { [key: string]: any\nfoo = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");
    expectPrintedAssignSemanticsTS("class Foo { [key: string]: any; foo = 0 }", "class Foo {\n  constructor() {\n    this.foo = 0;\n  }\n}\n");

    expectParseErrorTS("class Foo<> {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("class Foo<,> {}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseErrorTS("class Foo<T><T> {}", "<stdin>: ERROR: Expected \"{\" but found \"<\"\n");

    expectPrintedTS("class Foo { foo<T>() {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectPrintedTS("class Foo { foo?<T>() {} }", "class Foo {\n  foo() {\n  }\n}\n");
    expectPrintedTS("class Foo { [foo]<T>() {} }", "class Foo {\n  [foo]() {\n  }\n}\n");
    expectPrintedTS("class Foo { [foo]?<T>() {} }", "class Foo {\n  [foo]() {\n  }\n}\n");
    expectParseErrorTS("class Foo { foo<T> }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseErrorTS("class Foo { foo?<T> }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseErrorTS("class Foo { foo!<T>() {} }", "<stdin>: ERROR: Expected \";\" but found \"<\"\n");
    expectParseErrorTS("class Foo { [foo]<T> }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseErrorTS("class Foo { [foo]?<T> }", "<stdin>: ERROR: Expected \"(\" but found \"}\"\n");
    expectParseErrorTS("class Foo { [foo]!<T>() {} }", "<stdin>: ERROR: Expected \";\" but found \"<\"\n");

    // See: https://github.com/microsoft/TypeScript/pull/60225
    expectPrintedTS("class A { get \n x() {} }", "class A {\n  get x() {\n  }\n}\n");
    expectPrintedTS("class A { set \n x(_) {} }", "class A {\n  set x(_) {\n  }\n}\n");
    expectPrintedTS("class A { get \n *x() {} }", "class A {\n  get;\n  *x() {\n  }\n}\n");
    expectPrintedTS("class A { set \n *x(_) {} }", "class A {\n  set;\n  *x(_) {\n  }\n}\n");
    expectParseErrorTS("class A { get \n async x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectParseErrorTS("class A { set \n async x(_) {} }", "<stdin>: ERROR: Expected \"(\" but found \"x\"\n");
    expectParseErrorTS("class A { async get \n *x() {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
    expectParseErrorTS("class A { async set \n *x(_) {} }", "<stdin>: ERROR: Expected \"(\" but found \"*\"\n");
}

TEST(JsParser, TestTSAutoAccessors) {
    expectPrintedTS("class Foo { accessor }", "class Foo {\n  accessor;\n}\n");
    expectPrintedTS("class Foo { accessor x }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("class Foo { accessor x? }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("class Foo { accessor x! }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("class Foo { accessor x = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrintedTS("class Foo { accessor x? = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrintedTS("class Foo { accessor x! = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrintedTS("class Foo { accessor x: any }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("class Foo { accessor x?: any }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("class Foo { accessor x!: any }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("class Foo { accessor x: any = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrintedTS("class Foo { accessor x?: any = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrintedTS("class Foo { accessor x!: any = y }", "class Foo {\n  accessor x = y;\n}\n");
    expectPrintedTS("class Foo { accessor [x] }", "class Foo {\n  accessor [x];\n}\n");
    expectPrintedTS("class Foo { accessor [x]? }", "class Foo {\n  accessor [x];\n}\n");
    expectPrintedTS("class Foo { accessor [x]! }", "class Foo {\n  accessor [x];\n}\n");
    expectPrintedTS("class Foo { accessor [x] = y }", "class Foo {\n  accessor [x] = y;\n}\n");
    expectPrintedTS("class Foo { accessor [x]? = y }", "class Foo {\n  accessor [x] = y;\n}\n");
    expectPrintedTS("class Foo { accessor [x]! = y }", "class Foo {\n  accessor [x] = y;\n}\n");
    expectPrintedTS("class Foo { accessor [x]: any }", "class Foo {\n  accessor [x];\n}\n");
    expectPrintedTS("class Foo { accessor [x]?: any }", "class Foo {\n  accessor [x];\n}\n");
    expectPrintedTS("class Foo { accessor [x]!: any }", "class Foo {\n  accessor [x];\n}\n");
    expectPrintedTS("class Foo { accessor [x]: any = y }", "class Foo {\n  accessor [x] = y;\n}\n");
    expectPrintedTS("class Foo { accessor [x]?: any = y }", "class Foo {\n  accessor [x] = y;\n}\n");
    expectPrintedTS("class Foo { accessor [x]!: any = y }", "class Foo {\n  accessor [x] = y;\n}\n");

    expectParseErrorTS("class Foo { accessor x<T> }", "<stdin>: ERROR: Expected \";\" but found \"<\"\n");
    expectParseErrorTS("class Foo { accessor x<T>() {} }", "<stdin>: ERROR: Expected \";\" but found \"<\"\n");

    expectPrintedTS("declare class Foo { accessor x }", "");
    expectPrintedTS("declare class Foo { accessor #x }", "");
    expectPrintedTS("declare class Foo { static accessor x }", "");
    expectPrintedTS("declare class Foo { static accessor #x }", "");

    // TypeScript doesn't allow these combinations, but we shouldn't crash
    expectPrintedTS("class Foo { declare accessor x }", "class Foo {\n}\n");
    expectPrintedTS("class Foo { readonly accessor x }", "class Foo {\n  accessor x;\n}\n");
    expectPrintedTS("interface Foo { accessor x }", "");
    expectPrintedTS("interface Foo { static accessor x }", "");
    expectPrintedTS("let x: { accessor x }", "let x;\n");
    expectPrintedTS("let x: { static accessor x }", "let x;\n");
    expectParseErrorTS("class Foo { accessor declare x }", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectParseErrorTS("class Foo { accessor readonly x }", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
}

TEST(JsParser, TestTSPrivateIdentifiers) {
    // The TypeScript compiler still moves private field initializers into the
    // constructor, but it has to leave the private field declaration in place so
    // the private field is still declared.
    expectPrintedTS("class Foo { #foo }", "class Foo {\n  #foo;\n}\n");
    expectPrintedTS("class Foo { #foo = 1 }", "class Foo {\n  #foo = 1;\n}\n");
    expectPrintedTS("class Foo { #foo() {} }", "class Foo {\n  #foo() {\n  }\n}\n");
    expectPrintedTS("class Foo { get #foo() {} }", "class Foo {\n  get #foo() {\n  }\n}\n");
    expectPrintedTS("class Foo { set #foo(x) {} }", "class Foo {\n  set #foo(x) {\n  }\n}\n");

    // The TypeScript compiler doesn't currently support static private fields
    // because it moves static field initializers to after the class body and
    // private fields can't be used outside the class body. It remains to be seen
    // how the TypeScript compiler will transform private static fields once it
    // finally does support them. For now just leave the initializer in place.
    expectPrintedTS("class Foo { static #foo }", "class Foo {\n  static #foo;\n}\n");
    expectPrintedTS("class Foo { static #foo = 1 }", "class Foo {\n  static #foo = 1;\n}\n");
    expectPrintedTS("class Foo { static #foo() {} }", "class Foo {\n  static #foo() {\n  }\n}\n");
    expectPrintedTS("class Foo { static get #foo() {} }", "class Foo {\n  static get #foo() {\n  }\n}\n");
    expectPrintedTS("class Foo { static set #foo(x) {} }", "class Foo {\n  static set #foo(x) {\n  }\n}\n");

    // Decorators are not valid on private members
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec #foo = 1 }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec get #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec set #foo(x) {x} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec accessor #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static #foo = 1 }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static get #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static set #foo(x) {x} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static accessor #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");

    // Decorators are now able to access private names, since the TypeScript
    // compiler was changed to move them into a "static {}" block within the
    // class body: https://github.com/microsoft/TypeScript/pull/50074
    expectParseErrorExperimentalDecoratorTS("class Foo { static #foo; @dec(Foo.#foo) bar }", "");
    expectParseErrorExperimentalDecoratorTS("class Foo { static #foo; @dec(Foo.#foo) bar() {} }", "");
    expectParseErrorExperimentalDecoratorTS("class Foo { static #foo; bar(@dec(Foo.#foo) x) {} }", "");
}

TEST(JsParser, TestTSInterface) {
    expectPrintedTS("interface\nA\n{ a }", "interface;\nA;\n{\n  a;\n}\n");

    expectPrintedTS("interface A { a } x", "x;\n");
    expectPrintedTS("interface A { a; b } x", "x;\n");
    expectPrintedTS("interface A { a() } x", "x;\n");
    expectPrintedTS("interface A { a(); b } x", "x;\n");
    expectPrintedTS("interface Foo { foo(): Foo \n is: Bar } x", "x;\n");
    expectPrintedTS("interface A<T extends number> extends B.C<D, E>, F.G<H, I> {} x", "x;\n");
    expectPrintedTS("export interface A<T extends number> extends B.C<D, E>, F.G<H, I> {} x", "x;\n");
    expectPrintedTS("export default interface Foo {} x", "x;\n");
    expectParseErrorTS("export default interface + x",
        "<stdin>: ERROR: \"interface\" is a reserved word and cannot be used in an ECMAScript module\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n");

    // Check ASI for "interface"
    expectPrintedTS("interface\nFoo\n{}", "interface;\nFoo;\n{\n}\n");
    expectPrintedTS("export default interface\nFoo {} x", "x;\n");
    expectPrintedTS("export default interface\nFoo\n{} x", "x;\n");
    expectParseErrorTS("interface\nFoo {}", "<stdin>: ERROR: Expected \";\" but found \"{\"\n");
    expectParseErrorTS("export interface\nFoo {}", "<stdin>: ERROR: Unexpected \"interface\"\n");
    expectParseErrorTS("export interface\nFoo\n{}", "<stdin>: ERROR: Unexpected \"interface\"\n");
    expectParseErrorTS("export default interface\nFoo", "<stdin>: ERROR: Expected \"{\" but found end of file\n");
}

TEST(JsParser, TestTSNamespace) {
    expectPrintedTS("namespace\nx\n{ var y }", "namespace;\nx;\n{\n  var y;\n}\n");

    // Check ES5 emit
    expectPrintedTargetTS(5, "namespace x { export var y = 1 }", "var x;\n(function(x) {\n  x.y = 1;\n})(x || (x = {}));\n");
    expectPrintedTargetTS(2015, "namespace x { export var y = 1 }", "var x;\n((x) => {\n  x.y = 1;\n})(x || (x = {}));\n");

    // Certain syntax isn't allowed inside a namespace block
    expectParseErrorTS("namespace x { return }", "<stdin>: ERROR: A return statement cannot be used here:\n");
    expectParseErrorTS("namespace x { await 1 }", "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n");
    expectParseErrorTS("namespace x { if (y) return }", "<stdin>: ERROR: A return statement cannot be used here:\n");
    expectParseErrorTS("namespace x { if (y) await 1 }", "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n");
    expectParseErrorTS("namespace x { this }", "<stdin>: ERROR: Cannot use \"this\" here:\n");
    expectParseErrorTS("namespace x { () => this }", "<stdin>: ERROR: Cannot use \"this\" here:\n");
    expectParseErrorTS("namespace x { class y { [this] } }", "<stdin>: ERROR: Cannot use \"this\" here:\n");
    expectParseErrorTS("namespace x { (function() { this }) }", "");
    expectParseErrorTS("namespace x { function y() { this } }", "");
    expectParseErrorTS("namespace x { class y { x = this } }", "");
    expectParseErrorTS("export namespace x { export let yield = 1 }",
        "<stdin>: ERROR: \"yield\" is a reserved word and cannot be used in an ECMAScript module\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n");
    expectPrintedTS("namespace x { export let await = 1, y = await }",
        R"(var x;
((x) => {
  x.await = 1;
  x.y = x.await;
})(x || (x = {}));
)");
    expectPrintedTS("namespace x { export let yield = 1, y = yield }",
        R"(var x;
((x) => {
  x.yield = 1;
  x.y = x.yield;
})(x || (x = {}));
)");

    expectPrintedTS("namespace Foo { 0 }",
        R"(var Foo;
((Foo) => {
  0;
})(Foo || (Foo = {}));
)");
    expectPrintedTS("export namespace Foo { 0 }",
        R"(export var Foo;
((Foo) => {
  0;
})(Foo || (Foo = {}));
)");

    // Namespaces should introduce a scope that prevents name collisions
    expectPrintedTS("namespace Foo { let x } let x",
        R"(var Foo;
((Foo) => {
  let x;
})(Foo || (Foo = {}));
let x;
)");

    // Exports in namespaces shouldn't collide with module exports
    expectPrintedTS("namespace Foo { export let x } export let x",
        R"(var Foo;
((Foo) => {
})(Foo || (Foo = {}));
export let x;
)");
    expectPrintedTS("declare namespace Foo { export let x } namespace x { 0 }",
        R"(var x;
((x) => {
  0;
})(x || (x = {}));
)");

    const std::string errorText = R"(<stdin>: ERROR: The symbol "foo" has already been declared
<stdin>: NOTE: The symbol "foo" was originally declared here:
)";

    // Namespaces with values are not allowed to merge
    expectParseErrorTS("var foo; namespace foo { 0 }", errorText);
    expectParseErrorTS("let foo; namespace foo { 0 }", errorText);
    expectParseErrorTS("const foo = 0; namespace foo { 0 }", errorText);
    expectParseErrorTS("namespace foo { 0 } var foo", errorText);
    expectParseErrorTS("namespace foo { 0 } let foo", errorText);
    expectParseErrorTS("namespace foo { 0 } const foo = 0", errorText);

    // Namespaces without values are allowed to merge
    expectPrintedTS("var foo; namespace foo {}", "var foo;\n");
    expectPrintedTS("let foo; namespace foo {}", "let foo;\n");
    expectPrintedTS("const foo = 0; namespace foo {}", "const foo = 0;\n");
    expectPrintedTS("namespace foo {} var foo", "var foo;\n");
    expectPrintedTS("namespace foo {} let foo", "let foo;\n");
    expectPrintedTS("namespace foo {} const foo = 0", "const foo = 0;\n");

    // Namespaces with types but no values are allowed to merge
    expectPrintedTS("var foo; namespace foo { export type bar = number }", "var foo;\n");
    expectPrintedTS("let foo; namespace foo { export type bar = number }", "let foo;\n");
    expectPrintedTS("const foo = 0; namespace foo { export type bar = number }", "const foo = 0;\n");
    expectPrintedTS("namespace foo { export type bar = number } var foo", "var foo;\n");
    expectPrintedTS("namespace foo { export type bar = number } let foo", "let foo;\n");
    expectPrintedTS("namespace foo { export type bar = number } const foo = 0", "const foo = 0;\n");

    // Namespaces are allowed to merge with certain symbols
    expectPrintedTS("function foo() {} namespace foo { 0 }",
        R"(function foo() {
}
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("function* foo() {} namespace foo { 0 }",
        R"(function* foo() {
}
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("async function foo() {} namespace foo { 0 }",
        R"(async function foo() {
}
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("class foo {} namespace foo { 0 }",
        R"(class foo {
}
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("enum foo { a } namespace foo { 0 }",
        R"(var foo = /* @__PURE__ */ ((foo) => {
  foo[foo["a"] = 0] = "a";
  return foo;
})(foo || {});
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("namespace foo {} namespace foo { 0 }",
        R"(var foo;
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectParseErrorTS("namespace foo { 0 } function foo() {}", errorText);
    expectParseErrorTS("namespace foo { 0 } function* foo() {}", errorText);
    expectParseErrorTS("namespace foo { 0 } async function foo() {}", errorText);
    expectParseErrorTS("namespace foo { 0 } class foo {}", errorText);
    expectPrintedTS("namespace foo { 0 } enum foo { a }",
        R"(((foo) => {
  0;
})(foo || (foo = {}));
var foo = /* @__PURE__ */ ((foo) => {
  foo[foo["a"] = 0] = "a";
  return foo;
})(foo || {});
)");
    expectPrintedTS("namespace foo { 0 } namespace foo {}",
        R"(var foo;
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("namespace foo { 0 } namespace foo { 0 }",
        R"(var foo;
((foo) => {
  0;
})(foo || (foo = {}));
((foo) => {
  0;
})(foo || (foo = {}));
)");
    expectPrintedTS("function foo() {} namespace foo { 0 } function foo() {}",
        R"(function foo() {
}
((foo) => {
  0;
})(foo || (foo = {}));
function foo() {
}
)");
    expectPrintedTS("function* foo() {} namespace foo { 0 } function* foo() {}",
        R"(function* foo() {
}
((foo) => {
  0;
})(foo || (foo = {}));
function* foo() {
}
)");
    expectPrintedTS("async function foo() {} namespace foo { 0 } async function foo() {}",
        R"(async function foo() {
}
((foo) => {
  0;
})(foo || (foo = {}));
async function foo() {
}
)");

    // Namespace merging shouldn't allow for other merging
    expectParseErrorTS("class foo {} namespace foo { 0 } class foo {}", errorText);
    expectParseErrorTS("class foo {} namespace foo { 0 } enum foo {}", errorText);
    expectParseErrorTS("enum foo {} namespace foo { 0 } class foo {}", errorText);
    expectParseErrorTS("namespace foo { 0 } namespace foo { 0 } let foo", errorText);
    expectParseErrorTS("namespace foo { 0 } enum foo {} class foo {}", errorText);

    // Test dot nested namespace syntax
    expectPrintedTS("namespace foo.bar { foo(bar) }",
        R"(var foo;
((foo) => {
  let bar;
  ((bar) => {
    foo(bar);
  })(bar = foo.bar || (foo.bar = {}));
})(foo || (foo = {}));
)");

    // "module" is a deprecated alias for "namespace"
    expectPrintedTS("module foo { export namespace bar { foo(bar) } }",
        R"(var foo;
((foo) => {
  let bar;
  ((bar) => {
    foo(bar);
  })(bar = foo.bar || (foo.bar = {}));
})(foo || (foo = {}));
)");
    expectPrintedTS("namespace foo { export module bar { foo(bar) } }",
        R"(var foo;
((foo) => {
  let bar;
  ((bar) => {
    foo(bar);
  })(bar = foo.bar || (foo.bar = {}));
})(foo || (foo = {}));
)");
    expectPrintedTS("module foo.bar { foo(bar) }",
        R"(var foo;
((foo) => {
  let bar;
  ((bar) => {
    foo(bar);
  })(bar = foo.bar || (foo.bar = {}));
})(foo || (foo = {}));
)");
}

TEST(JsParser, TestTSNamespaceExports) {
    expectPrintedTS(R"(
		namespace A {
			export namespace B {
				export function fn() {}
			}
			namespace C {
				export function fn() {}
			}
			namespace D {
				function fn() {}
			}
		}
	)",
        R"(var A;
((A) => {
  let B;
  ((B) => {
    function fn() {
    }
    B.fn = fn;
  })(B = A.B || (A.B = {}));
  let C;
  ((C) => {
    function fn() {
    }
    C.fn = fn;
  })(C || (C = {}));
  let D;
  ((D) => {
    function fn() {
    }
  })(D || (D = {}));
})(A || (A = {}));
)");

    expectPrintedTS(R"(
		namespace A {
			export namespace B {
				export class Class {}
			}
			namespace C {
				export class Class {}
			}
			namespace D {
				class Class {}
			}
		}
	)",
        R"(var A;
((A) => {
  let B;
  ((B) => {
    class Class {
    }
    B.Class = Class;
  })(B = A.B || (A.B = {}));
  let C;
  ((C) => {
    class Class {
    }
    C.Class = Class;
  })(C || (C = {}));
  let D;
  ((D) => {
    class Class {
    }
  })(D || (D = {}));
})(A || (A = {}));
)");

    expectPrintedTS(R"(
		namespace A {
			export namespace B {
				export enum Enum {}
			}
			namespace C {
				export enum Enum {}
			}
			namespace D {
				enum Enum {}
			}
		}
	)",
        R"(var A;
((A) => {
  let B;
  ((B) => {
    let Enum;
    ((Enum) => {
    })(Enum = B.Enum || (B.Enum = {}));
  })(B = A.B || (A.B = {}));
  let C;
  ((C) => {
    let Enum;
    ((Enum) => {
    })(Enum = C.Enum || (C.Enum = {}));
  })(C || (C = {}));
  let D;
  ((D) => {
    let Enum;
    ((Enum) => {
    })(Enum || (Enum = {}));
  })(D || (D = {}));
})(A || (A = {}));
)");

    expectPrintedTS(R"(
		namespace A {
			export namespace B {
				export let foo = 1
				foo += foo
			}
			namespace C {
				export let foo = 1
				foo += foo
			}
			namespace D {
				let foo = 1
				foo += foo
			}
		}
	)",
        R"(var A;
((A) => {
  let B;
  ((B) => {
    B.foo = 1;
    B.foo += B.foo;
  })(B = A.B || (A.B = {}));
  let C;
  ((C) => {
    C.foo = 1;
    C.foo += C.foo;
  })(C || (C = {}));
  let D;
  ((D) => {
    let foo = 1;
    foo += foo;
  })(D || (D = {}));
})(A || (A = {}));
)");

    expectPrintedTS(R"(
		namespace A {
			export namespace B {
				export const foo = 1
			}
			namespace C {
				export const foo = 1
			}
			namespace D {
				const foo = 1
			}
		}
	)",
        R"(var A;
((A) => {
  let B;
  ((B) => {
    B.foo = 1;
  })(B = A.B || (A.B = {}));
  let C;
  ((C) => {
    C.foo = 1;
  })(C || (C = {}));
  let D;
  ((D) => {
    const foo = 1;
  })(D || (D = {}));
})(A || (A = {}));
)");

    expectPrintedTS(R"(
		namespace A {
			export namespace B {
				export var foo = 1
				foo += foo
			}
			namespace C {
				export var foo = 1
				foo += foo
			}
			namespace D {
				var foo = 1
				foo += foo
			}
		}
	)",
        R"(var A;
((A) => {
  let B;
  ((B) => {
    B.foo = 1;
    B.foo += B.foo;
  })(B = A.B || (A.B = {}));
  let C;
  ((C) => {
    C.foo = 1;
    C.foo += C.foo;
  })(C || (C = {}));
  let D;
  ((D) => {
    var foo = 1;
    foo += foo;
  })(D || (D = {}));
})(A || (A = {}));
)");

    expectPrintedTS(R"(
		namespace ns {
			export declare const L1
			console.log(L1)

			export declare let [[L2 = x, { [y]: L3 }]]
			console.log(L2, L3)

			export declare function F()
			console.log(F)

			export declare function F2() { }
			console.log(F2)

			export declare class C { }
			console.log(C)

			export declare enum E { }
			console.log(E)

			export declare namespace N { }
			console.log(N)
		}
	)",
        R"(var ns;
((ns) => {
  console.log(ns.L1);
  console.log(ns.L2, ns.L3);
  console.log(F);
  console.log(F2);
  console.log(C);
  console.log(E);
  console.log(N);
})(ns || (ns = {}));
)");

    expectPrintedTS(R"(
		namespace a { export var a = 123; log(a) }
		namespace b { export let b = 123; log(b) }
		namespace c { export enum c {} log(c) }
		namespace d { export class d {} log(d) }
		namespace e { export namespace e {} log(e) }
		namespace f { export function f() {} log(f) }
	)",
        R"(var a;
((_a) => {
  _a.a = 123;
  log(_a.a);
})(a || (a = {}));
var b;
((_b) => {
  _b.b = 123;
  log(_b.b);
})(b || (b = {}));
var c;
((_c) => {
  let c;
  ((c) => {
  })(c = _c.c || (_c.c = {}));
  log(c);
})(c || (c = {}));
var d;
((_d) => {
  class d {
  }
  _d.d = d;
  log(d);
})(d || (d = {}));
var e;
((e) => {
  log(e);
})(e || (e = {}));
var f;
((_f) => {
  function f() {
  }
  _f.f = f;
  log(f);
})(f || (f = {}));
)");

    expectPrintedTS(R"(
		namespace a { export declare var a }
		namespace b { export declare let b }
		namespace c { export declare enum c {} }
		namespace d { export declare class d {} }
		namespace e { export declare namespace e {} }
		namespace f { export declare function f() {} }
	)",
        R"(var a;
((_a) => {
})(a || (a = {}));
var b;
((_b) => {
})(b || (b = {}));
var c;
((c) => {
})(c || (c = {}));
var d;
((d) => {
})(d || (d = {}));
var f;
((f) => {
})(f || (f = {}));
)");
}

TEST(JsParser, TestTSNamespaceDestructuring) {
    expectPrintedTS(R"(
		namespace A {
			export var [
				a,
				[, b = c, ...d],
				{[x]: [[y]] = z, ...o},
			] = ref
		}
	)",
        R"(var A;
((A) => {
  [
    A.a,
    [, A.b = c, ...A.d],
    { [x]: [[A.y]] = z, ...A.o }
  ] = ref;
})(A || (A = {}));
)");
}

TEST(JsParser, TestTSEnum) {
    expectParseErrorTS("enum x { y z }", "<stdin>: ERROR: Expected \",\" after \"y\" in enum\n");
    expectParseErrorTS("enum x { 'y' 'z' }", "<stdin>: ERROR: Expected \",\" after \"y\" in enum\n");
    expectParseErrorTS("enum x { y = 0 z }", "<stdin>: ERROR: Expected \",\" before \"z\" in enum\n");
    expectParseErrorTS("enum x { 'y' = 0 'z' }", "<stdin>: ERROR: Expected \",\" before \"z\" in enum\n");

    // Check ES5 emit
    expectPrintedTargetTS(5, "enum x { y = 1 }", "var x = /* @__PURE__ */ function(x) {\n  x[x[\"y\"] = 1] = \"y\";\n  return x;\n}(x || {});\n");
    expectPrintedTargetTS(2015, "enum x { y = 1 }", "var x = /* @__PURE__ */ ((x) => {\n  x[x[\"y\"] = 1] = \"y\";\n  return x;\n})(x || {});\n");

    // Certain syntax isn't allowed inside an enum block
    expectParseErrorTS("enum x { y = this }", "<stdin>: ERROR: Cannot use \"this\" here:\n");
    expectParseErrorTS("enum x { y = () => this }", "<stdin>: ERROR: Cannot use \"this\" here:\n");
    expectParseErrorTS("enum x { y = function() { this } }", "");

    expectPrintedTS("enum Foo { A, B }",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["A"] = 0] = "A";
  Foo[Foo["B"] = 1] = "B";
  return Foo;
})(Foo || {});
)");
    expectPrintedTS("export enum Foo { A; B }",
        R"(export var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["A"] = 0] = "A";
  Foo[Foo["B"] = 1] = "B";
  return Foo;
})(Foo || {});
)");
    expectPrintedTS("enum Foo { A, B, C = 3.3, D, E }",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["A"] = 0] = "A";
  Foo[Foo["B"] = 1] = "B";
  Foo[Foo["C"] = 3.3] = "C";
  Foo[Foo["D"] = 4.3] = "D";
  Foo[Foo["E"] = 5.3] = "E";
  return Foo;
})(Foo || {});
)");
    expectPrintedTS("enum Foo { A, B, C = 'x', D, E, F = `y`, G = `${z}`, H = tag`` }",
        R"(var Foo = ((Foo) => {
  Foo[Foo["A"] = 0] = "A";
  Foo[Foo["B"] = 1] = "B";
  Foo["C"] = "x";
  Foo[Foo["D"] = void 0] = "D";
  Foo[Foo["E"] = void 0] = "E";
  Foo["F"] = `y`;
  Foo["G"] = `${z}`;
  Foo[Foo["H"] = tag``] = "H";
  return Foo;
})(Foo || {});
)");

    // TypeScript allows splitting an enum into multiple blocks
    expectPrintedTS("enum Foo { A = 1 } enum Foo { B = 2 }",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["A"] = 1] = "A";
  return Foo;
})(Foo || {});
var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["B"] = 2] = "B";
  return Foo;
})(Foo || {});
)");

    expectPrintedTS(R"(
		enum Foo {
			'a' = 10.01,
			'a b' = 100,
			c = a + Foo.a + Foo['a b'],
			d,
			e = a + Foo.a + Foo['a b'] + Math.random(),
			f,
		}
		enum Bar {
			a = Foo.a
		}
	)",
        R"(var Foo = ((Foo) => {
  Foo[Foo["a"] = 10.01] = "a";
  Foo[Foo["a b"] = 100] = "a b";
  Foo[Foo["c"] = 120.02] = "c";
  Foo[Foo["d"] = 121.02] = "d";
  Foo[Foo["e"] = 120.02 + Math.random()] = "e";
  Foo[Foo["f"] = void 0] = "f";
  return Foo;
})(Foo || {});
var Bar = /* @__PURE__ */ ((Bar) => {
  Bar[Bar["a"] = 10.01 /* a */] = "a";
  return Bar;
})(Bar || {});
)");

    expectPrintedTS(R"(
		enum Foo { A }
		x = [Foo.A, Foo?.A, Foo?.A()]
		y = [Foo['A'], Foo?.['A'], Foo?.['A']()]
	)",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["A"] = 0] = "A";
  return Foo;
})(Foo || {});
x = [0 /* A */, Foo?.A, Foo?.A()];
y = [0 /* A */, Foo?.["A"], Foo?.["A"]()];
)");

    // Check shadowing
    expectPrintedTS("enum Foo { Foo }",
        R"(var Foo = /* @__PURE__ */ ((_Foo) => {
  _Foo[_Foo["Foo"] = 0] = "Foo";
  return _Foo;
})(Foo || {});
)");
    expectPrintedTS("enum Foo { Bar = Foo }",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["Bar"] = Foo] = "Bar";
  return Foo;
})(Foo || {});
)");
    expectPrintedTS("enum Foo { Foo = 1, Bar = Foo }",
        R"(var Foo = /* @__PURE__ */ ((_Foo) => {
  _Foo[_Foo["Foo"] = 1] = "Foo";
  _Foo[_Foo["Bar"] = 1 /* Foo */] = "Bar";
  return _Foo;
})(Foo || {});
)");

    // Check top-level "var" and nested "let"
    expectPrintedTS("enum a { b = 1 }", "var a = /* @__PURE__ */ ((a) => {\n  a[a[\"b\"] = 1] = \"b\";\n  return a;\n})(a || {});\n");
    expectPrintedTS("{ enum a { b = 1 } }", "{\n  let a;\n  ((a) => {\n    a[a[\"b\"] = 1] = \"b\";\n  })(a || (a = {}));\n}\n");

    // Check "await" and "yield"
    expectPrintedTS("enum x { await = 1, y = await }",
        R"(var x = /* @__PURE__ */ ((x) => {
  x[x["await"] = 1] = "await";
  x[x["y"] = 1 /* await */] = "y";
  return x;
})(x || {});
)");
    expectPrintedTS("enum x { yield = 1, y = yield }",
        R"(var x = /* @__PURE__ */ ((x) => {
  x[x["yield"] = 1] = "yield";
  x[x["y"] = 1 /* yield */] = "y";
  return x;
})(x || {});
)");
    expectParseErrorTS("enum x { y = await 1 }", "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n");
    expectParseErrorTS("function *f() { enum x { y = yield 1 } }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseErrorTS("async function f() { enum x { y = await 1 } }", "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n");
    expectParseErrorTS("export enum x { yield = 1, y = yield }",
        "<stdin>: ERROR: \"yield\" is a reserved word and cannot be used in an ECMAScript module\n"
        "<stdin>: NOTE: This file is considered to be an ECMAScript module because of the \"export\" keyword here:\n");

    // Check enum use before declaration
    expectPrintedTS("foo = Foo.FOO; enum Foo { FOO } bar = Foo.FOO",
        R"(foo = 0 /* FOO */;
var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["FOO"] = 0] = "FOO";
  return Foo;
})(Foo || {});
bar = 0 /* FOO */;
)");

    expectPrintedTS("() => { const enum Foo { A } () => Foo.A }",
        R"(() => {
  let Foo;
  ((Foo) => {
    Foo[Foo["A"] = 0] = "A";
  })(Foo || (Foo = {}));
  () => 0 /* A */;
};
)");
}

TEST(JsParser, TestTSEnumConstantFolding) {
    expectPrintedTS(R"(
		enum Foo {
			add = 1 + 2,
			sub = -1 - 2,
			mul = 10 * 20,

			div_pos_inf = 1 / 0,
			div_neg_inf = 1 / -0,
			div_nan = 0 / 0,
			div_neg_zero = 1 / (1 / -0),

			div0 = 10 / 20,
			div1 = 10 / -20,
			div2 = -10 / 20,
			div3 = -10 / -20,

			mod0 = 123 % 100,
			mod1 = 123 % -100,
			mod2 = -123 % 100,
			mod3 = -123 % -100,

			fmod0 = 1.375 % 0.75,
			fmod1 = 1.375 % -0.75,
			fmod2 = -1.375 % 0.75,
			fmod3 = -1.375 % -0.75,

			pow0 = 2.25 ** 3,
			pow1 = 2.25 ** -3,
			pow2 = (-2.25) ** 3,
			pow3 = (-2.25) ** -3,
		}
	)",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["add"] = 3] = "add";
  Foo[Foo["sub"] = -3] = "sub";
  Foo[Foo["mul"] = 200] = "mul";
  Foo[Foo["div_pos_inf"] = Infinity] = "div_pos_inf";
  Foo[Foo["div_neg_inf"] = -Infinity] = "div_neg_inf";
  Foo[Foo["div_nan"] = NaN] = "div_nan";
  Foo[Foo["div_neg_zero"] = -0] = "div_neg_zero";
  Foo[Foo["div0"] = 0.5] = "div0";
  Foo[Foo["div1"] = -0.5] = "div1";
  Foo[Foo["div2"] = -0.5] = "div2";
  Foo[Foo["div3"] = 0.5] = "div3";
  Foo[Foo["mod0"] = 23] = "mod0";
  Foo[Foo["mod1"] = 23] = "mod1";
  Foo[Foo["mod2"] = -23] = "mod2";
  Foo[Foo["mod3"] = -23] = "mod3";
  Foo[Foo["fmod0"] = 0.625] = "fmod0";
  Foo[Foo["fmod1"] = 0.625] = "fmod1";
  Foo[Foo["fmod2"] = -0.625] = "fmod2";
  Foo[Foo["fmod3"] = -0.625] = "fmod3";
  Foo[Foo["pow0"] = 11.390625] = "pow0";
  Foo[Foo["pow1"] = 0.0877914951989026] = "pow1";
  Foo[Foo["pow2"] = -11.390625] = "pow2";
  Foo[Foo["pow3"] = -0.0877914951989026] = "pow3";
  return Foo;
})(Foo || {});
)");

    expectPrintedTS(R"(
		enum Foo {
			pos = +54321012345,
			neg = -54321012345,
			cpl = ~54321012345,

			shl0 = 987654321 << 2,
			shl1 = 987654321 << 31,
			shl2 = 987654321 << 34,

			shr0 = -987654321 >> 2,
			shr1 = -987654321 >> 31,
			shr2 = -987654321 >> 34,

			ushr0 = -987654321 >>> 2,
			ushr1 = -987654321 >>> 31,
			ushr2 = -987654321 >>> 34,

			bitand = 0xDEADF00D & 0xBADCAFE,
			bitor = 0xDEADF00D | 0xBADCAFE,
			bitxor = 0xDEADF00D ^ 0xBADCAFE,
		}
	)",
        R"(var Foo = /* @__PURE__ */ ((Foo) => {
  Foo[Foo["pos"] = 54321012345] = "pos";
  Foo[Foo["neg"] = -54321012345] = "neg";
  Foo[Foo["cpl"] = 1513562502] = "cpl";
  Foo[Foo["shl0"] = -344350012] = "shl0";
  Foo[Foo["shl1"] = -2147483648] = "shl1";
  Foo[Foo["shl2"] = -344350012] = "shl2";
  Foo[Foo["shr0"] = -246913581] = "shr0";
  Foo[Foo["shr1"] = -1] = "shr1";
  Foo[Foo["shr2"] = -246913581] = "shr2";
  Foo[Foo["ushr0"] = 826828243] = "ushr0";
  Foo[Foo["ushr1"] = 1] = "ushr1";
  Foo[Foo["ushr2"] = 826828243] = "ushr2";
  Foo[Foo["bitand"] = 179159052] = "bitand";
  Foo[Foo["bitor"] = -542246145] = "bitor";
  Foo[Foo["bitxor"] = -721405197] = "bitxor";
  return Foo;
})(Foo || {});
)");
}

TEST(JsParser, TestTSFunction) {
    expectPrintedTS("function foo(): void; function foo(): void {}", "function foo() {\n}\n");

    expectPrintedTS("function foo<A>() {}", "function foo() {\n}\n");
    expectPrintedTS("function foo<A extends B<A>>() {}", "function foo() {\n}\n");
    expectPrintedTS("function foo<A extends B<C<A>>>() {}", "function foo() {\n}\n");
    expectPrintedTS("function foo<A,B,C,>() {}", "function foo() {\n}\n");
    expectPrintedTS("function foo<A extends B<C>= B<C>>() {}", "function foo() {\n}\n");
    expectPrintedTS("function foo<A extends B<C<D>>= B<C<D>>>() {}", "function foo() {\n}\n");
    expectPrintedTS("function foo<A extends B<C<D<E>>>= B<C<D<E>>>>() {}", "function foo() {\n}\n");

    expectParseErrorTS("function foo<>() {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("function foo<,>() {}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseErrorTS("function foo<T><T>() {}", "<stdin>: ERROR: Expected \"(\" but found \"<\"\n");

    expectPrintedTS("export default function <T>() {}", "export default function() {\n}\n");
    expectParseErrorTS("export default function <>() {}", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("export default function <,>() {}", "<stdin>: ERROR: Expected identifier but found \",\"\n");
    expectParseErrorTS("export default function <T><T>() {}", "<stdin>: ERROR: Expected \"(\" but found \"<\"\n");

    expectPrintedTS(R"(
		export default function foo();
		export default function foo(x);
		export default function foo(x?, y?) {}
	)", "export default function foo(x, y) {\n}\n");
}

TEST(JsParser, TestTSDecl) {
    expectPrintedTS("var a!: string, b!: boolean", "var a, b;\n");
    expectPrintedTS("let a!: string, b!: boolean", "let a, b;\n");
    expectPrintedTS("const a!: string = '', b!: boolean = false", "const a = \"\", b = false;\n");
    expectPrintedTS("var a\n!b", "var a;\n!b;\n");
    expectPrintedTS("let a\n!b", "let a;\n!b;\n");
    expectParseErrorTS("var a!", "<stdin>: ERROR: Expected \":\" but found end of file\n");
    expectParseErrorTS("var a! = ", "<stdin>: ERROR: Expected \":\" but found \"=\"\n");
    expectParseErrorTS("var a!, b", "<stdin>: ERROR: Expected \":\" but found \",\"\n");

    expectPrinted("a ? ({b}) => {} : c", "a ? ({ b }) => {\n} : c;\n");
    expectPrinted("a ? (({b}) => {}) : c", "a ? (({ b }) => {\n}) : c;\n");
    expectPrinted("a ? (({b})) : c", "a ? { b } : c;\n");
    expectParseError("a ? (({b})) => {} : c", "<stdin>: ERROR: Invalid binding pattern\n");
    expectPrintedTS("a ? ({b}) => {} : c", "a ? ({ b }) => {\n} : c;\n");
    expectPrintedTS("a ? (({b}) => {}) : c", "a ? (({ b }) => {\n}) : c;\n");
    expectPrintedTS("a ? (({b})) : c", "a ? { b } : c;\n");
    expectParseErrorTS("a ? (({b})) => {} : c", "<stdin>: ERROR: Invalid binding pattern\n");
}

TEST(JsParser, TestTSDeclare) {
    expectPrintedTS("declare\nfoo", "declare;\nfoo;\n");
    expectPrintedTS("declare\nvar foo", "declare;\nvar foo;\n");
    expectPrintedTS("declare\nlet foo", "declare;\nlet foo;\n");
    expectPrintedTS("declare\nconst foo = 0", "declare;\nconst foo = 0;\n");
    expectPrintedTS("declare\nfunction foo() {}", "declare;\nfunction foo() {\n}\n");
    expectPrintedTS("declare\nclass Foo {}", "declare;\nclass Foo {\n}\n");
    expectPrintedTS("declare\nenum Foo {}", "declare;\nvar Foo = /* @__PURE__ */ ((Foo) => {\n  return Foo;\n})(Foo || {});\n");
    expectPrintedTS("class Foo { declare \n foo }", "class Foo {\n  declare;\n  foo;\n}\n");

    expectPrintedTS("declare;", "declare;\n");
    expectPrintedTS("declare();", "declare();\n");
    expectPrintedTS("declare[x];", "declare[x];\n");

    expectPrintedTS("declare var x: number", "");
    expectPrintedTS("declare let x: number", "");
    expectPrintedTS("declare const x: number", "");
    expectPrintedTS("declare var x = function() {}; function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare let x = function() {}; function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare const x = function() {}; function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare function fn(); function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare function fn()\n function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare function fn() {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare enum X {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare enum X { x = function() {} } function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare class X {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare class X { x = function() {} } function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare interface X {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare namespace X {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare namespace X { export var x = function() {} } function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare namespace X { export let x = function() {} } function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare namespace X { export const x = function() {} } function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare namespace X { export function fn() {} } function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare module X {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare module 'X' {} function scope() {}", "function scope() {\n}\n");
    expectPrintedTS("declare module 'X'; let foo", "let foo;\n");
    expectPrintedTS("declare module 'X'\nlet foo", "let foo;\n");
    expectPrintedTS("declare module 'X' { let foo }", "");
    expectPrintedTS("declare module 'X'\n{ let foo }", "");
    expectPrintedTS("declare global { interface Foo {} let foo: any } let bar", "let bar;\n");
    expectPrintedTS("declare module M { const x }", "");
    expectPrintedTS("declare module M { global { const x } }", "");
    expectPrintedTS("declare module M { global { const x } function foo() {} }", "");
    expectPrintedTS("declare module M { global \n { const x } }", "");
    expectPrintedTS("declare module M { import 'path' }", "");
    expectPrintedTS("declare module M { import x from 'path' }", "");
    expectPrintedTS("declare module M { import {x} from 'path' }", "");
    expectPrintedTS("declare module M { import * as ns from 'path' }", "");
    expectPrintedTS("declare module M { import foo = bar }", "");
    expectPrintedTS("declare module M { export import foo = bar }", "");
    expectPrintedTS("declare module M { export {x} from 'path' }", "");
    expectPrintedTS("declare module M { export default 123 }", "");
    expectPrintedTS("declare module M { export default function x() {} }", "");
    expectPrintedTS("declare module M { export default class X {} }", "");
    expectPrintedTS("declare module M { export * as ns from 'path' }", "");
    expectPrintedTS("declare module M { export * from 'path' }", "");
    expectPrintedTS("declare module M { export = foo }", "");
    expectPrintedTS("declare module M { export as namespace ns }", "");
    expectPrintedTS("declare module M { export as namespace ns; }", "");
    expectParseErrorTS("declare module M { export as namespace ns.foo }", "<stdin>: ERROR: Expected \";\" but found \".\"\n");
    expectParseErrorTS("declare module M { export as namespace ns function foo() {} }", "<stdin>: ERROR: Expected \";\" but found \"function\"\n");
    expectParseErrorTS("module M { const x }", "<stdin>: ERROR: The constant \"x\" must be initialized\n");
    expectParseErrorTS("module M { const [] }", "<stdin>: ERROR: This constant must be initialized\n");
    expectParseErrorTS("module M { const {} }", "<stdin>: ERROR: This constant must be initialized\n");

    // This is a weird case where "," after a rest parameter is allowed
    expectPrintedTS("declare function fn(x: any, ...y, )", "");
    expectPrintedTS("declare function fn(x: any, ...y: any, )", "");
    expectParseErrorTS("function fn(x: any, ...y, )", "<stdin>: ERROR: Expected \")\" but found \",\"\n");
    expectParseErrorTS("function fn(x: any, ...y, ) {}", "<stdin>: ERROR: Expected \")\" but found \",\"\n");
    expectParseErrorTS("function fn(x: any, ...y: any, )", "<stdin>: ERROR: Expected \")\" but found \",\"\n");
    expectParseErrorTS("function fn(x: any, ...y: any, ) {}", "<stdin>: ERROR: Expected \")\" but found \",\"\n");

    // This declares a global module
    expectPrintedTS("export as namespace ns", "");
    expectParseErrorTS("export as namespace ns.foo", "<stdin>: ERROR: Expected \";\" but found \".\"\n");

    // TypeScript 4.4+ technically treats these as valid syntax, but I assume
    // this is a bug: https://github.com/microsoft/TypeScript/issues/54602
    expectParseErrorTS("declare foo", "<stdin>: ERROR: Unexpected \"foo\"\n");
    expectParseErrorTS("declare foo()", "<stdin>: ERROR: Unexpected \"foo\"\n");
    expectParseErrorTS("declare {foo}", "<stdin>: ERROR: Unexpected \"{\"\n");
}

TEST(JsParser, TestTSExperimentalDecorator) {
    // Tests of "declare class"
    expectPrintedExperimentalDecoratorTS("@dec(() => 0) declare class Foo {} {let foo}", "{\n  let foo;\n}\n");
    expectPrintedExperimentalDecoratorTS("@dec(() => 0) declare abstract class Foo {} {let foo}", "{\n  let foo;\n}\n");
    expectPrintedExperimentalDecoratorTS("@dec(() => 0) export declare class Foo {} {let foo}", "{\n  let foo;\n}\n");
    expectPrintedExperimentalDecoratorTS("@dec(() => 0) export declare abstract class Foo {} {let foo}", "{\n  let foo;\n}\n");
    expectPrintedExperimentalDecoratorTS("declare class Foo { @dec(() => 0) foo } {let foo}", "{\n  let foo;\n}\n");
    expectPrintedExperimentalDecoratorTS("declare class Foo { @dec(() => 0) foo() } {let foo}", "{\n  let foo;\n}\n");
    expectPrintedExperimentalDecoratorTS("declare class Foo { foo(@dec(() => 0) x) } {let foo}", "{\n  let foo;\n}\n");

    // Decorators must only work on class statements
    expectParseErrorExperimentalDecoratorTS("@dec enum foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec namespace foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec function foo() {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec abstract", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec declare: x", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorExperimentalDecoratorTS("@dec declare enum foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec declare namespace foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec declare function foo()", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export enum foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export namespace foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export function foo() {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export default abstract", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export declare enum foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export declare namespace foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@dec export declare function foo()", "<stdin>: ERROR: Decorators are not valid here\n");

    // Decorators must be forbidden outside class statements
    const std::string note = "<stdin>: NOTE: This is a class expression, not a class declaration:\n";
    expectParseErrorExperimentalDecoratorTS("(class { @dec foo })", "<stdin>: ERROR: TypeScript experimental decorators can only be used with class declarations\n" + note);
    expectParseErrorExperimentalDecoratorTS("(class { @dec foo() {} })", "<stdin>: ERROR: TypeScript experimental decorators can only be used with class declarations\n" + note);
    expectParseErrorExperimentalDecoratorTS("(class { foo(@dec x) {} })", "<stdin>: ERROR: TypeScript experimental decorators can only be used with class declarations\n" + note);
    expectParseErrorExperimentalDecoratorTS("({ @dec foo })", "<stdin>: ERROR: Expected identifier but found \"@\"\n");
    expectParseErrorExperimentalDecoratorTS("({ @dec foo() {} })", "<stdin>: ERROR: Expected identifier but found \"@\"\n");
    expectParseErrorExperimentalDecoratorTS("({ foo(@dec x) {} })", "<stdin>: ERROR: Expected identifier but found \"@\"\n");

    // Decorators aren't allowed with private names
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec #foo = 1 }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec *#foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec async #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec async* #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec accessor #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static #foo = 1 }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static *#foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static async #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static async* #foo() {} }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static accessor #foo }", "<stdin>: ERROR: TypeScript experimental decorators cannot be used on private identifiers\n");

    // Decorators aren't allowed on class constructors
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec constructor() {} }", "<stdin>: ERROR: Decorators are not allowed on class constructors\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec public constructor() {} }", "<stdin>: ERROR: Decorators are not allowed on class constructors\n");

    // Check use of "await"
    const std::string friendlyAwaitErrorWithNote = "<stdin>: ERROR: \"await\" can only be used inside an \"async\" function\n"
        "<stdin>: NOTE: Consider adding the \"async\" keyword here:\n";
    expectPrintedExperimentalDecoratorTS("async function foo() { @dec(await x) class Foo {} }",
        "async function foo() {\n  let Foo = class {\n  };\n  Foo = __decorateClass([\n    dec(await x)\n  ], Foo);\n}\n");
    expectPrintedExperimentalDecoratorTS("async function foo() { class Foo { @dec(await x) foo() {} } }",
        "async function foo() {\n  class Foo {\n    foo() {\n    }\n  }\n  __decorateClass([\n    dec(await x)\n  ], Foo.prototype, \"foo\", 1);\n}\n");
    expectPrintedExperimentalDecoratorTS("async function foo() { class Foo { foo(@dec(await x) y) {} } }",
        "async function foo() {\n  class Foo {\n    foo(y) {\n    }\n  }\n  __decorateClass([\n    __decorateParam(0, dec(await x))\n  ], Foo.prototype, \"foo\", 1);\n}\n");
    expectParseErrorExperimentalDecoratorTS("function foo() { @dec(await x) class Foo {} }", friendlyAwaitErrorWithNote);
    expectParseErrorExperimentalDecoratorTS("function foo() { class Foo { @dec(await x) foo() {} } }", friendlyAwaitErrorWithNote);
    expectParseErrorExperimentalDecoratorTS("function foo() { class Foo { foo(@dec(await x) y) {} } }", friendlyAwaitErrorWithNote);
    expectParseErrorExperimentalDecoratorTS("function foo() { class Foo { @dec(await x) async foo() {} } }", friendlyAwaitErrorWithNote);
    expectParseErrorExperimentalDecoratorTS("function foo() { class Foo { async foo(@dec(await x) y) {} } }",
        "<stdin>: ERROR: The keyword \"await\" cannot be used here:\n<stdin>: ERROR: Expected \")\" but found \"x\"\n");

    // Check lowered use of "await"
    expectPrintedTargetExperimentalDecoratorTS(2015, "async function foo() { @dec(await x) class Foo {} }",
        R"(function foo() {
  return __async(this, null, function* () {
    let Foo = class {
    };
    Foo = __decorateClass([
      dec(yield x)
    ], Foo);
  });
}
)");
    expectPrintedTargetExperimentalDecoratorTS(2015, "async function foo() { class Foo { @dec(await x) foo() {} } }",
        R"(function foo() {
  return __async(this, null, function* () {
    class Foo {
      foo() {
      }
    }
    __decorateClass([
      dec(yield x)
    ], Foo.prototype, "foo", 1);
  });
}
)");
    expectPrintedTargetExperimentalDecoratorTS(2015, "async function foo() { class Foo { foo(@dec(await x) y) {} } }",
        R"(function foo() {
  return __async(this, null, function* () {
    class Foo {
      foo(y) {
      }
    }
    __decorateClass([
      __decorateParam(0, dec(yield x))
    ], Foo.prototype, "foo", 1);
  });
}
)");

    // Check use of "yield"
    expectPrintedExperimentalDecoratorTS("function *foo() { @dec(yield x) class Foo {} }", // We currently allow this but TypeScript doesn't
        "function* foo() {\n  let Foo = class {\n  };\n  Foo = __decorateClass([\n    dec(yield x)\n  ], Foo);\n}\n");
    expectPrintedExperimentalDecoratorTS("function *foo() { class Foo { @dec(yield x) foo() {} } }", // We currently allow this but TypeScript doesn't
        "function* foo() {\n  class Foo {\n    foo() {\n    }\n  }\n  __decorateClass([\n    dec(yield x)\n  ], Foo.prototype, \"foo\", 1);\n}\n");
    expectParseErrorExperimentalDecoratorTS("function *foo() { class Foo { foo(@dec(yield x) y) {} } }", // TypeScript doesn't allow this (although it could because it would work fine)
        "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseErrorExperimentalDecoratorTS("function foo() { @dec(yield x) class Foo {} }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");
    expectParseErrorExperimentalDecoratorTS("function foo() { class Foo { @dec(yield x) foo() {} } }", "<stdin>: ERROR: Cannot use \"yield\" outside a generator function\n");

    // Check inline function expressions
    expectPrintedExperimentalDecoratorTS("@((x, y) => x + y) class Foo {}",
        "let Foo = class {\n};\nFoo = __decorateClass([\n  (x, y) => x + y\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@((x, y) => x + y) export class Foo {}",
        "export let Foo = class {\n};\nFoo = __decorateClass([\n  (x, y) => x + y\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@(function(x, y) { return x + y }) class Foo {}",
        "let Foo = class {\n};\nFoo = __decorateClass([\n  function(x, y) {\n    return x + y;\n  }\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@(function(x, y) { return x + y }) export class Foo {}",
        "export let Foo = class {\n};\nFoo = __decorateClass([\n  function(x, y) {\n    return x + y;\n  }\n], Foo);\n");

    // Don't allow decorators on static blocks
    expectPrintedTS("class Foo { static }", "class Foo {\n  static;\n}\n");
    expectPrintedExperimentalDecoratorTS("class Foo { @dec static }", "class Foo {\n  static;\n}\n__decorateClass([\n  dec\n], Foo.prototype, \"static\", 2);\n");
    expectParseErrorExperimentalDecoratorTS("class Foo { @dec static {} }", "<stdin>: ERROR: Expected \";\" but found \"{\"\n");

    // TypeScript experimental decorators allow more expressions than JavaScript decorators
    expectPrintedExperimentalDecoratorTS("@x() class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x()\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@x.y() class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x.y()\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@(() => {}) class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  () => {\n  }\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@123 class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  123\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@x?.() class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x?.()\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@x?.y() class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x?.y()\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@x?.[y]() class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x?.[y]()\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@new Function() class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  new Function()\n], Foo);\n");
    expectParseErrorExperimentalDecoratorTS("@x[y] class Foo {}", "<stdin>: ERROR: Expected \";\" but found \"class\"\n");
    expectParseErrorExperimentalDecoratorTS("@() => {} class Foo {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseErrorExperimentalDecoratorTS("x = @y function() {}",
        "<stdin>: ERROR: TypeScript experimental decorators cannot be used in expression position\n"
        "<stdin>: ERROR: Expected \"class\" but found \"function\"\n");

    // Check ASI for "abstract"
    expectPrintedExperimentalDecoratorTS("@x abstract class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x\n], Foo);\n");
    expectParseErrorExperimentalDecoratorTS("@x abstract\nclass Foo {}", "<stdin>: ERROR: Decorators are not valid here\n");

    // Check decorator locations in relation to the "export" keyword
    expectPrintedExperimentalDecoratorTS("@x export class Foo {}", "export let Foo = class {\n};\nFoo = __decorateClass([\n  x\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("export @x class Foo {}", "export let Foo = class {\n};\nFoo = __decorateClass([\n  x\n], Foo);\n");
    expectPrintedExperimentalDecoratorTS("@x export default class {}",
        "let stdin_default = class {\n};\nstdin_default = __decorateClass([\n  x\n], stdin_default);\nexport {\n  stdin_default as default\n};\n");
    expectPrintedExperimentalDecoratorTS("export default @x class {}",
        "let stdin_default = class {\n};\nstdin_default = __decorateClass([\n  x\n], stdin_default);\nexport {\n  stdin_default as default\n};\n");
    expectPrintedExperimentalDecoratorTS("@x export default class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x\n], Foo);\nexport {\n  Foo as default\n};\n");
    expectPrintedExperimentalDecoratorTS("export default @x class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x\n], Foo);\nexport {\n  Foo as default\n};\n");
    expectParseErrorExperimentalDecoratorTS("export default (@x class {})", "<stdin>: ERROR: TypeScript experimental decorators cannot be used in expression position\n");
    expectParseErrorExperimentalDecoratorTS("export default (@x class Foo {})", "<stdin>: ERROR: TypeScript experimental decorators cannot be used in expression position\n");
    expectParseErrorExperimentalDecoratorTS("export @x default class {}", "<stdin>: ERROR: Unexpected \"default\"\n");
    expectParseErrorExperimentalDecoratorTS("@x export @y class Foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@x export default abstract", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorExperimentalDecoratorTS("@x export @y default class {}", "<stdin>: ERROR: Decorators are not valid here\n<stdin>: ERROR: Unexpected \"default\"\n");

    // From the TypeScript team: "We do allow postfix ! because it's TypeScript only."
    // https://github.com/microsoft/TypeScript/issues/57756
    expectPrintedExperimentalDecoratorTS("@x!.y!.z class Foo {}", "let Foo = class {\n};\nFoo = __decorateClass([\n  x.y.z\n], Foo);\n");

    // TypeScript experimental decorators are actually allowed on declared and abstract fields
    expectPrintedExperimentalDecoratorTS("class Foo { @(() => {}) declare foo: any; @(() => {}) bar: any }",
        "class Foo {\n  bar;\n}\n__decorateClass([\n  () => {\n  }\n], Foo.prototype, \"foo\", 2);\n__decorateClass([\n  () => {\n  }\n], Foo.prototype, \"bar\", 2);\n");
    expectPrintedExperimentalDecoratorTS("abstract class Foo { @(() => {}) abstract foo: any; @(() => {}) bar: any }",
        "class Foo {\n  bar;\n}\n__decorateClass([\n  () => {\n  }\n], Foo.prototype, \"foo\", 2);\n__decorateClass([\n  () => {\n  }\n], Foo.prototype, \"bar\", 2);\n");
}

TEST(JsParser, TestTSDecorators) {
    expectPrintedTS("@x @y class Foo {}", "@x @y class Foo {\n}\n");
    expectPrintedTS("@x @y export class Foo {}", "@x @y export class Foo {\n}\n");
    expectPrintedTS("@x @y export default class Foo {}", "@x @y export default class Foo {\n}\n");
    expectPrintedTS("_ = @x @y class {}", "_ = @x @y class {\n};\n");

    expectPrintedTS("class Foo { @x y: any }", "class Foo {\n  @x y;\n}\n");
    expectPrintedTS("class Foo { @x y(): any {} }", "class Foo {\n  @x y() {\n  }\n}\n");
    expectPrintedTS("class Foo { @x static y: any }", "class Foo {\n  @x static y;\n}\n");
    expectPrintedTS("class Foo { @x static y(): any {} }", "class Foo {\n  @x static y() {\n  }\n}\n");
    expectPrintedTS("class Foo { @x accessor y: any }", "class Foo {\n  @x accessor y;\n}\n");

    expectPrintedTS("class Foo { @x #y: any }", "class Foo {\n  @x #y;\n}\n");
    expectPrintedTS("class Foo { @x #y(): any {} }", "class Foo {\n  @x #y() {\n  }\n}\n");
    expectPrintedTS("class Foo { @x static #y: any }", "class Foo {\n  @x static #y;\n}\n");
    expectPrintedTS("class Foo { @x static #y(): any {} }", "class Foo {\n  @x static #y() {\n  }\n}\n");
    expectPrintedTS("class Foo { @x accessor #y: any }", "class Foo {\n  @x accessor #y;\n}\n");

    expectParseErrorTS("class Foo { x(@y z) {} }", "<stdin>: ERROR: Parameter decorators only work when experimental decorators are enabled\n"
        "NOTE: You can enable experimental decorators by adding \"experimentalDecorators\": true to your \"tsconfig.json\" file.\n");
    expectParseErrorTS("class Foo { @x static {} }", "<stdin>: ERROR: Expected \";\" but found \"{\"\n");

    expectPrintedTS("@\na\n(\n)\n@\n(\nb\n)\nclass\nFoo\n{\n}\n", "@a()\n@b\nclass Foo {\n}\n");
    expectPrintedTS("@(a, b) class Foo {}", "@(a, b) class Foo {\n}\n");
    expectPrintedTS("@x() class Foo {}", "@x() class Foo {\n}\n");
    expectPrintedTS("@x.y() class Foo {}", "@x.y() class Foo {\n}\n");
    expectPrintedTS("@(() => {}) class Foo {}", "@(() => {\n}) class Foo {\n}\n");
    expectPrintedTS("class Foo { #x = @y.#x.y.#x class {} }", "class Foo {\n  #x = @y.#x.y.#x class {\n  };\n}\n");
    expectParseErrorTS("@123 class Foo {}", "<stdin>: ERROR: Expected identifier but found \"123\"\n");
    expectParseErrorTS("@x[y] class Foo {}", "<stdin>: ERROR: Expected \";\" but found \"class\"\n");
    expectParseErrorTS("@x?.() class Foo {}", "<stdin>: ERROR: Expected identifier but found \"(\"\n");
    expectParseErrorTS("@x?.y() class Foo {}",
        "<stdin>: ERROR: JavaScript decorator syntax does not allow \"?.\" here\n"
        "<stdin>: NOTE: Wrap this decorator in parentheses to allow arbitrary expressions:\n");
    expectParseErrorTS("@x?.[y]() class Foo {}", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseErrorTS("@new Function() class Foo {}", "<stdin>: ERROR: Expected identifier but found \"new\"\n");
    expectParseErrorTS("@() => {} class Foo {}", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseErrorTS("x = @y function() {}", "<stdin>: ERROR: Expected \"class\" but found \"function\"\n");

    expectPrintedTS("class Foo { @x<{}> y: any }", "class Foo {\n  @x y;\n}\n");
    expectPrintedTS("class Foo { @x<{}>() y: any }", "class Foo {\n  @x() y;\n}\n");
    expectPrintedTS("class Foo { @x<{}> @y<[], () => {}> z: any }", "class Foo {\n  @x @y z;\n}\n");
    expectPrintedTS("class Foo { @x<{}>() @y<[], () => {}>() z: any }", "class Foo {\n  @x() @y() z;\n}\n");
    expectPrintedTS("class Foo { @x<{}>.y<[], () => {}> z: any }", "class Foo {\n  @x.y z;\n}\n");

    // TypeScript 5.0+ allows this but Babel doesn't. I believe this is a bug
    // with TypeScript: https://github.com/microsoft/TypeScript/issues/55336
    expectParseErrorTS("class Foo { @x<{}>().y<[], () => {}>() z: any }",
        "<stdin>: ERROR: JavaScript decorator syntax does not allow \".\" after a call expression\n"
        "<stdin>: NOTE: Wrap this decorator in parentheses to allow arbitrary expressions:\n");

    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "@dec class Foo {}",
        R"(var _Foo_decorators, _init;
_Foo_decorators = [dec];
class Foo {
}
_init = __decoratorStart(null);
Foo = __decorateElement(_init, 0, "Foo", _Foo_decorators, Foo);
__runInitializers(_init, 1, Foo);
)");
    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "class Foo { @dec x }",
        R"(var _x_dec, _init;
_x_dec = [dec];
class Foo {
  constructor() {
    __publicField(this, "x", __runInitializers(_init, 8, this)), __runInitializers(_init, 11, this);
  }
}
_init = __decoratorStart(null);
__decorateElement(_init, 5, "x", _x_dec, Foo);
__decoratorMetadata(_init, Foo);
)");
    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "class Foo { @dec x() {} }",
        R"(var _x_dec, _init;
_x_dec = [dec];
class Foo {
  constructor() {
    __runInitializers(_init, 5, this);
  }
  x() {
  }
}
_init = __decoratorStart(null);
__decorateElement(_init, 1, "x", _x_dec, Foo);
__decoratorMetadata(_init, Foo);
)");
    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "class Foo { @dec accessor x }",
        R"(var _x_dec, _init, _x;
_x_dec = [dec];
class Foo {
  constructor() {
    __privateAdd(this, _x, __runInitializers(_init, 8, this)), __runInitializers(_init, 11, this);
  }
}
_init = __decoratorStart(null);
_x = new WeakMap();
__decorateElement(_init, 4, "x", _x_dec, Foo, _x);
__decoratorMetadata(_init, Foo);
)");
    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "class Foo { @dec static x }",
        R"(var _x_dec, _init;
_x_dec = [dec];
class Foo {
}
_init = __decoratorStart(null);
__decorateElement(_init, 13, "x", _x_dec, Foo);
__decoratorMetadata(_init, Foo);
__publicField(Foo, "x", __runInitializers(_init, 8, Foo)), __runInitializers(_init, 11, Foo);
)");
    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "class Foo { @dec static x() {} }",
        R"(var _x_dec, _init;
_x_dec = [dec];
class Foo {
  static x() {
  }
}
_init = __decoratorStart(null);
__decorateElement(_init, 9, "x", _x_dec, Foo);
__decoratorMetadata(_init, Foo);
__runInitializers(_init, 3, Foo);
)");
    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kDecorators, "class Foo { @dec static accessor x }",
        R"(var _x_dec, _init, _x;
_x_dec = [dec];
class Foo {
}
_init = __decoratorStart(null);
_x = new WeakMap();
__decorateElement(_init, 12, "x", _x_dec, Foo, _x);
__decoratorMetadata(_init, Foo);
__privateAdd(Foo, _x, __runInitializers(_init, 8, Foo)), __runInitializers(_init, 11, Foo);
)");

    // Check ASI for "abstract"
    expectPrintedTS("@x abstract class Foo {}", "@x class Foo {\n}\n");
    expectParseErrorTS("@x abstract\nclass Foo {}", "<stdin>: ERROR: Decorators are not valid here\n");

    // Check decorator locations in relation to the "export" keyword
    expectPrintedTS("@x export class Foo {}", "@x export class Foo {\n}\n");
    expectPrintedTS("export @x class Foo {}", "@x export class Foo {\n}\n");
    expectPrintedTS("@x export default class {}", "@x export default class {\n}\n");
    expectPrintedTS("export default @x class {}", "@x export default class {\n}\n");
    expectPrintedTS("@x export default class Foo {}", "@x export default class Foo {\n}\n");
    expectPrintedTS("export default @x class Foo {}", "@x export default class Foo {\n}\n");
    expectPrintedTS("export default (@x class {})", "export default (@x class {\n});\n");
    expectPrintedTS("export default (@x class Foo {})", "export default (@x class Foo {\n});\n");
    expectParseErrorTS("export @x default class {}", "<stdin>: ERROR: Unexpected \"default\"\n");
    expectParseErrorTS("@x export @y class Foo {}", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorTS("@x export default abstract", "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorTS("@x export @y default class {}", "<stdin>: ERROR: Decorators are not valid here\n<stdin>: ERROR: Unexpected \"default\"\n");

    // From the TypeScript team: "We do allow postfix ! because it's TypeScript only."
    // https://github.com/microsoft/TypeScript/issues/57756
    expectPrintedTS("@x!.y!.z class Foo {}", "@x.y.z class Foo {\n}\n");

    // JavaScript decorators are not allowed on declared or abstract fields
    expectParseErrorTS("class Foo { @(() => {}) declare foo: any; @(() => {}) bar: any }",
        "<stdin>: ERROR: Decorators are not valid here\n");
    expectParseErrorTS("abstract class Foo { @(() => {}) abstract foo: any; @(() => {}) bar: any }",
        "<stdin>: ERROR: Decorators are not valid here\n");
}

TEST(JsParser, TestTSTry) {
    expectPrintedTS("try {} catch (x: any) {}", "try {\n} catch (x) {\n}\n");
    expectPrintedTS("try {} catch (x: unknown) {}", "try {\n} catch (x) {\n}\n");
    expectPrintedTS("try {} catch (x: number) {}", "try {\n} catch (x) {\n}\n");

    expectPrintedTS("try {} catch ({x}: any) {}", "try {\n} catch ({ x }) {\n}\n");
    expectPrintedTS("try {} catch ({x}: unknown) {}", "try {\n} catch ({ x }) {\n}\n");
    expectPrintedTS("try {} catch ({x}: number) {}", "try {\n} catch ({ x }) {\n}\n");

    expectPrintedTS("try {} catch ([x]: any) {}", "try {\n} catch ([x]) {\n}\n");
    expectPrintedTS("try {} catch ([x]: unknown) {}", "try {\n} catch ([x]) {\n}\n");
    expectPrintedTS("try {} catch ([x]: number) {}", "try {\n} catch ([x]) {\n}\n");

    expectParseErrorTS("try {} catch (x!) {}", "<stdin>: ERROR: Expected \")\" but found \"!\"\n");
    expectParseErrorTS("try {} catch (x!: any) {}", "<stdin>: ERROR: Expected \")\" but found \"!\"\n");
    expectParseErrorTS("try {} catch (x!: unknown) {}", "<stdin>: ERROR: Expected \")\" but found \"!\"\n");
}

TEST(JsParser, TestTSArrow) {
    expectPrintedTS("(a?) => {}", "(a) => {\n};\n");
    expectPrintedTS("(a?: number) => {}", "(a) => {\n};\n");
    expectPrintedTS("(a?: number = 0) => {}", "(a = 0) => {\n};\n");
    expectParseErrorTS("(a? = 0) => {}", "<stdin>: ERROR: Unexpected \"=\"\n");

    expectPrintedTS("(a?, b) => {}", "(a, b) => {\n};\n");
    expectPrintedTS("(a?: number, b) => {}", "(a, b) => {\n};\n");
    expectPrintedTS("(a?: number = 0, b) => {}", "(a = 0, b) => {\n};\n");
    expectParseErrorTS("(a? = 0, b) => {}", "<stdin>: ERROR: Unexpected \"=\"\n");

    expectPrintedTS("(a: number) => {}", "(a) => {\n};\n");
    expectPrintedTS("(a: number = 0) => {}", "(a = 0) => {\n};\n");
    expectPrintedTS("(a: number, b) => {}", "(a, b) => {\n};\n");

    expectPrintedTS("(): void => {}", "() => {\n};\n");
    expectPrintedTS("(a): void => {}", "(a) => {\n};\n");
    expectParseErrorTS("x: void => {}", "<stdin>: ERROR: Unexpected \"=>\"\n");
    expectPrintedTS("a ? (1 + 2) : (3 + 4)", "a ? 1 + 2 : 3 + 4;\n");
    expectPrintedTS("(foo) ? (foo as Bar) : null;", "foo ? foo : null;\n");
    expectPrintedTS("((foo) ? (foo as Bar) : null)", "foo ? foo : null;\n");
    expectPrintedTS("let x = a ? (b, c) : (d, e)", "let x = a ? (b, c) : (d, e);\n");

    expectPrintedTS("let x: () => void = () => {}", "let x = () => {\n};\n");
    expectPrintedTS("let x: (y) => void = () => {}", "let x = () => {\n};\n");
    expectPrintedTS("let x: (this) => void = () => {}", "let x = () => {\n};\n");
    expectPrintedTS("let x: (this: any) => void = () => {}", "let x = () => {\n};\n");
    expectPrintedTS("let x = (y: any): (() => {}) => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): () => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): (y) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ([,[b]]) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ([a,[b]]) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ([a,[b],]) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({a}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({a,}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({a:{b}}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({0:{b}}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({'a':{b}}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({if:{b}}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({...a}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): ({a,...b}) => {} => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): (y[]) => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("let x = (y: any): (a | b) => {};", "let x = (y) => {\n};\n");
    expectPrintedTS("type x = ({...fi}) => {};", "");
    expectParseErrorTS("let x = (y: any): (y) => {};", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorTS("let x = (y: any): (y) => {return 0};", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorTS("let x = (y: any): asserts y is (y) => {};", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorTS("type x = ({...if}) => {};", "<stdin>: ERROR: Unexpected \"...\"\n");

    expectPrintedTS("async (): void => {}", "async () => {\n};\n");
    expectPrintedTS("async (a): void => {}", "async (a) => {\n};\n");
    expectParseErrorTS("async x: void => {}", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");

    expectPrintedTS("function foo(x: boolean): asserts x", "");
    expectPrintedTS("function foo(x: boolean): asserts<T>", "");
    expectPrintedTS("function foo(x: boolean): asserts\nx", "x;\n");
    expectPrintedTS("function foo(x: boolean): asserts<T>\nx", "x;\n");
    expectParseErrorTS("function foo(x: boolean): asserts<T> x", "<stdin>: ERROR: Expected \";\" but found \"x\"\n");
    expectPrintedTS("(x: boolean): asserts x => {}", "(x) => {\n};\n");
    expectPrintedTS("(x: boolean): asserts this is object => {}", "(x) => {\n};\n");
    expectPrintedTS("(x: T): asserts x is NonNullable<T> => {}", "(x) => {\n};\n");
    expectPrintedTS("(x: any): x is number => {}", "(x) => {\n};\n");
    expectPrintedTS("(x: any): this is object => {}", "(x) => {\n};\n");
    expectPrintedTS("(x: any): (() => void) => {}", "(x) => {\n};\n");
    expectPrintedTS("(x: any): ((y: any) => void) => {}", "(x) => {\n};\n");
    expectPrintedTS("function foo(this: any): this is number {}", "function foo() {\n}\n");
    expectPrintedTS("function foo(this: any): asserts this is number {}", "function foo() {\n}\n");
    expectPrintedTS("(symbol: any): symbol is number => {}", "(symbol) => {\n};\n");

    expectPrintedTS("let x: () => {} | ({y: z});", "let x;\n");
    expectPrintedTS("function x(): ({y: z}) {}", "function x() {\n}\n");

    expectParseErrorTargetTS(5, "return check ? (hover = 2, bar) : baz()", "");
    expectParseErrorTargetTS(5, "return check ? (hover = 2, bar) => 0 : baz()",
        "<stdin>: ERROR: Transforming default arguments to the configured target environment is not supported yet\n");

    expectPrintedTS("function f(async?) { g(async in x) }", "function f(async) {\n  g(async in x);\n}\n");
    expectPrintedTS("function f(async?) { g(async as boolean) }", "function f(async) {\n  g(async);\n}\n");
    expectPrintedTS("function f() { g(async as => boolean) }", "function f() {\n  g(async (as) => boolean);\n}\n");

    expectPrintedTS("x = a ? (b = c) : d", "x = a ? b = c : d;\n");
    expectPrintedTS("x = a ? (b = c) : d => e", "x = a ? b = c : (d) => e;\n");
    expectPrintedTS("x = a ? (b = c) : T => d : (e = f)", "x = a ? (b = c) => d : e = f;\n");
    expectPrintedTS("x = a ? (b = c) : T => d : (e = f) : T => g", "x = a ? (b = c) => d : (e = f) => g;\n");
    expectPrintedTS("x = a ? b ? c : (d = e) : f => g", "x = a ? b ? c : d = e : (f) => g;\n");
    expectPrintedTS("x = a ? b ? (c = d) => e : (f = g) : h => i", "x = a ? b ? (c = d) => e : f = g : (h) => i;\n");
    expectPrintedTS("x = a ? b ? (c = d) : T => e : (f = g) : h => i", "x = a ? b ? (c = d) => e : f = g : (h) => i;\n");
    expectPrintedTS("x = a ? b ? (c = d) : T => e : (f = g) : (h = i) : T => j", "x = a ? b ? (c = d) => e : f = g : (h = i) => j;\n");
    expectPrintedTS("x = a ? (b) : T => c : d", "x = a ? (b) => c : d;\n");
    expectPrintedTS("x = a ? b - (c) : d => e", "x = a ? b - c : (d) => e;\n");
    expectPrintedTS("x = a ? b = (c) : T => d : e", "x = a ? b = (c) => d : e;\n");
    expectParseErrorTS("x = a ? (b = c) : T => d : (e = f) : g", "<stdin>: ERROR: Expected \";\" but found \":\"\n");
    expectParseErrorTS("x = a ? b ? (c = d) : T => e : (f = g)", "<stdin>: ERROR: Expected \":\" but found end of file\n");
    expectParseErrorTS("x = a ? - (b) : c => d : e", "<stdin>: ERROR: Expected \";\" but found \":\"\n");
    expectParseErrorTS("x = a ? b - (c) : d => e : f", "<stdin>: ERROR: Expected \";\" but found \":\"\n");

    // Note: Newlines are important (they trigger backtracking)
    expectPrintedTS("x = (\n  a ? (b = c) : { d: e }\n)", "x = a ? b = c : { d: e };\n");

    // Need to clone "#private" identifier state in the parser
    expectPrintedTS("x = class { #y; y = a ? (b : T) : T => this.#y : c }", "x = class {\n  #y;\n  y = a ? (b) => this.#y : c;\n};\n");

    // Need to clone "in" operator state in the parser
    expectPrintedTS("for (x = a ? () : T => b in c : d; ; ) ;", "for (x = a ? () => b in c : d; ; ) ;\n");
}

TEST(JsParser, TestTSSuperCall) {
    expectPrintedAssignSemanticsTS("class A extends B { x = 1 }",
        R"(class A extends B {
  constructor() {
    super(...arguments);
    this.x = 1;
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x }",
        R"(class A extends B {
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x = 1; constructor() { foo() } }",
        R"(class A extends B {
  constructor() {
    this.x = 1;
    foo();
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x; constructor() { foo() } }",
        R"(class A extends B {
  constructor() {
    foo();
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x = 1; constructor() { foo(); super(1); } }",
        R"(class A extends B {
  constructor() {
    foo();
    super(1);
    this.x = 1;
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x = 1; constructor() { foo(); y ||= super(1); } }",
        R"(class A extends B {
  constructor() {
    var __super = (...args) => {
      super(...args);
      this.x = 1;
      return this;
    };
    foo();
    y ||= __super(1);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x; constructor() { foo(); super(1); } }",
        R"(class A extends B {
  constructor() {
    foo();
    super(1);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { x; constructor() { foo(); y ||= super(1); } }",
        R"(class A extends B {
  constructor() {
    foo();
    y ||= super(1);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { [x] = 1; constructor() { foo(); super(1); } }",
        R"(var _a, _b;
class A extends (_b = B, _a = x, _b) {
  constructor() {
    foo();
    super(1);
    this[_a] = 1;
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { [x] = 1; constructor() { foo(); y ||= super(1); } }",
        R"(var _a, _b;
class A extends (_b = B, _a = x, _b) {
  constructor() {
    var __super = (...args) => {
      super(...args);
      this[_a] = 1;
      return this;
    };
    foo();
    y ||= __super(1);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { [x]; constructor() { foo(); super(1); } }",
        R"(var _a;
class A extends (_a = B, x, _a) {
  constructor() {
    foo();
    super(1);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { [x]; constructor() { foo(); y ||= super(1); } }",
        R"(var _a;
class A extends (_a = B, x, _a) {
  constructor() {
    foo();
    y ||= super(1);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { constructor(public x = 1) { foo(); super(1); } }",
        R"(class A extends B {
  constructor(x = 1) {
    foo();
    super(1);
    this.x = x;
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { constructor(public x = 1) { foo(); super(1); super(2); } }",
        R"(class A extends B {
  constructor(x = 1) {
    var __super = (...args) => {
      super(...args);
      this.x = x;
      return this;
    };
    foo();
    __super(1);
    __super(2);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { constructor(public x = 1) { if (false) super(1); super(2); } }",
        R"(class A extends B {
  constructor(x = 1) {
    if (false) __super(1);
    super(2);
    this.x = x;
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { constructor(public x = 1) { if (foo) super(1); super(2); } }",
        R"(class A extends B {
  constructor(x = 1) {
    var __super = (...args) => {
      super(...args);
      this.x = x;
      return this;
    };
    if (foo) __super(1);
    __super(2);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { constructor(public x = 1) { if (foo) super(1); else super(2); } }",
        R"(class A extends B {
  constructor(x = 1) {
    var __super = (...args) => {
      super(...args);
      this.x = x;
      return this;
    };
    if (foo) __super(1);
    else __super(2);
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { #x; y; constructor() { super() } }",
        R"(class A extends B {
  #x;
  constructor() {
    super();
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { #x = 1; y; constructor() { super() } }",
        R"(class A extends B {
  #x = 1;
  constructor() {
    super();
  }
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { #x; y = 1; constructor() { super() } }",
        R"(class A extends B {
  constructor() {
    super();
    this.y = 1;
  }
  #x;
}
)");

    expectPrintedAssignSemanticsTS("class A extends B { #x = 1; y = 2; constructor() { super() } }",
        R"(class A extends B {
  constructor() {
    super();
    this.#x = 1;
    this.y = 2;
  }
  #x;
}
)");
}

TEST(JsParser, TestTSCall) {
    expectPrintedTS("foo()", "foo();\n");
    expectPrintedTS("foo<number>()", "foo();\n");
    expectPrintedTS("foo<number, boolean>()", "foo();\n");
}

TEST(JsParser, TestTSNew) {
    expectPrintedTS("new Foo()", "new Foo();\n");
    expectPrintedTS("new Foo<number>()", "new Foo();\n");
    expectPrintedTS("new Foo<number, boolean>()", "new Foo();\n");
    expectPrintedTS("new Foo<number>", "new Foo();\n");
    expectPrintedTS("new Foo<number, boolean>", "new Foo();\n");

    expectPrintedTS("new Foo!()", "new Foo();\n");
    expectPrintedTS("new Foo!<number>()", "new Foo();\n");
    expectPrintedTS("new Foo!.Bar()", "new Foo.Bar();\n");
    expectPrintedTS("new Foo!.Bar<number>()", "new Foo.Bar();\n");
    expectPrintedTS("new Foo!['Bar']()", "new Foo[\"Bar\"]();\n");
    expectPrintedTS("new Foo\n!(x)", "new Foo();\n!x;\n");
    expectPrintedTS("new Foo<number>!(x)", "new Foo() < number > !x;\n");
    expectParseErrorTS("new Foo<number>!()", "<stdin>: ERROR: Unexpected \")\"\n");
    expectParseErrorTS("new Foo\n!.Bar()", "<stdin>: ERROR: Unexpected \".\"\n");
    expectParseError("new Foo!()", "<stdin>: ERROR: Unexpected \"!\"\n");
}

TEST(JsParser, TestTSInstantiationExpression) {
    expectPrintedTS("f<number>", "f;\n");
    expectPrintedTS("f<number, boolean>", "f;\n");
    expectPrintedTS("f.g<number>", "f.g;\n");
    expectPrintedTS("f<number>.g", "f.g;\n");
    expectPrintedTS("f<number>.g<number>", "f.g;\n");
    expectPrintedTS("f['g']<number>", "f[\"g\"];\n");
    expectPrintedTS("(f<number>)<number>", "f;\n");

    // Function call
    expectPrintedTS("const x1 = f<true>\n(true);", "const x1 = f(true);\n");
    // Relational expression
    expectPrintedTS("const x1 = f<true>\ntrue;", "const x1 = f;\ntrue;\n");
    // Instantiation expression
    expectPrintedTS("const x1 = f<true>;\n(true);", "const x1 = f;\ntrue;\n");

    // Trailing commas are not allowed
    expectPrintedTS("const x = Array<number>\n(0);", "const x = Array(0);\n");
    expectPrintedTS("const x = Array<number>;\n(0);", "const x = Array;\n0;\n");
    expectParseErrorTS("const x = Array<number,>\n(0);", "<stdin>: ERROR: Expected identifier but found \">\"\n");
    expectParseErrorTS("const x = Array<number,>;\n(0);", "<stdin>: ERROR: Expected identifier but found \">\"\n");

    expectPrintedTS("f<number>?.();", "f?.();\n");
    expectPrintedTS("f?.<number>();", "f?.();\n");
    expectPrintedTS("f<<T>() => T>?.();", "f?.();\n");
    expectPrintedTS("f?.<<T>() => T>();", "f?.();\n");

    expectPrintedTS("f<number>['g'];", "f < number > [\"g\"];\n");

    expectPrintedTS("type T21 = typeof Array<string>; f();", "f();\n");
    expectPrintedTS("type T22 = typeof Array<string, number>; f();", "f();\n");

    expectPrintedTS("f<x>, g<y>;", "f, g;\n");
    expectPrintedTS("f<<T>() => T>;", "f;\n");
    expectPrintedTS("f.x<<T>() => T>;", "f.x;\n");
    expectPrintedTS("f['x']<<T>() => T>;", "f[\"x\"];\n");
    expectPrintedTS("f<x>g<y>;", "f < x > g;\n");
    expectPrintedTS("f<x>=g<y>;", "f < x >= g;\n");
    expectPrintedTS("f<x>>g<y>;", "f < x >> g;\n");
    expectPrintedTS("f<x>>>g<y>;", "f < x >>> g;\n");
    expectParseErrorTS("f<x>>=g<y>;", "<stdin>: ERROR: Invalid assignment target\n");
    expectParseErrorTS("f<x>>>=g<y>;", "<stdin>: ERROR: Invalid assignment target\n");
    expectPrintedTS("f<x,y>g<y>;", "f < x, y > g;\n");
    expectPrintedTS("f<x,y>=g<y>;", "f < x, y >= g;\n");
    expectPrintedTS("f<x,y>>g<y>;", "f < x, y >> g;\n");
    expectPrintedTS("f<x,y>>>g<y>;", "f < x, y >>> g;\n");
    expectPrintedTS("f<x,y>>=g<y>;", "f < x, y >>= g;\n");
    expectPrintedTS("f<x,y>>>=g<y>;", "f < x, y >>>= g;\n");
    expectPrintedTS("f<x> = g<y>;", "f = g;\n");
    expectParseErrorTS("f<x> > g<y>;", "<stdin>: ERROR: Unexpected \">\"\n");
    expectParseErrorTS("f<x> >> g<y>;", "<stdin>: ERROR: Unexpected \">>\"\n");
    expectParseErrorTS("f<x> >>> g<y>;", "<stdin>: ERROR: Unexpected \">>>\"\n");
    expectParseErrorTS("f<x> >= g<y>;", "<stdin>: ERROR: Unexpected \">=\"\n");
    expectParseErrorTS("f<x> >>= g<y>;", "<stdin>: ERROR: Unexpected \">>=\"\n");
    expectParseErrorTS("f<x> >>>= g<y>;", "<stdin>: ERROR: Unexpected \">>>=\"\n");
    expectPrintedTS("f<x,y> = g<y>;", "f = g;\n");
    expectParseErrorTS("f<x,y> > g<y>;", "<stdin>: ERROR: Unexpected \">\"\n");
    expectParseErrorTS("f<x,y> >> g<y>;", "<stdin>: ERROR: Unexpected \">>\"\n");
    expectParseErrorTS("f<x,y> >>> g<y>;", "<stdin>: ERROR: Unexpected \">>>\"\n");
    expectParseErrorTS("f<x,y> >= g<y>;", "<stdin>: ERROR: Unexpected \">=\"\n");
    expectParseErrorTS("f<x,y> >>= g<y>;", "<stdin>: ERROR: Unexpected \">>=\"\n");
    expectParseErrorTS("f<x,y> >>>= g<y>;", "<stdin>: ERROR: Unexpected \">>>=\"\n");
    expectPrintedTS("[f<x>];", "[f];\n");
    expectPrintedTS("f<x> ? g<y> : h<z>;", "f ? g : h;\n");
    expectPrintedTS("{ f<x> }", "{\n  f;\n}\n");
    expectPrintedTS("f<x> + g<y>;", "f < x > +g;\n");
    expectPrintedTS("f<x> - g<y>;", "f < x > -g;\n");
    expectPrintedTS("f<x> * g<y>;", "f * g;\n");
    expectPrintedTS("f<x> *= g<y>;", "f *= g;\n");
    expectPrintedTS("f<x> == g<y>;", "f == g;\n");
    expectPrintedTS("f<x> ?? g<y>;", "f ?? g;\n");
    expectPrintedTS("f<x> in g<y>;", "f in g;\n");
    expectPrintedTS("f<x> instanceof g<y>;", "f instanceof g;\n");
    expectPrintedTS("f<x> as g<y>;", "f;\n");
    expectPrintedTS("f<x> satisfies g<y>;", "f;\n");
    expectPrintedTS("class A extends B { f() { super.f<x>=y } }", "class A extends B {\n  f() {\n    super.f < x >= y;\n  }\n}\n");
    expectPrintedTS("class A extends B { f() { super.f<x,y>=z } }", "class A extends B {\n  f() {\n    super.f < x, y >= z;\n  }\n}\n");

    expectParseErrorTS("const a8 = f<number><number>;", "<stdin>: ERROR: Unexpected \";\"\n");
    expectParseErrorTS("const b1 = f?.<number>;", "<stdin>: ERROR: Expected \"(\" but found \";\"\n");

    // See: https://github.com/microsoft/TypeScript/issues/48711
    expectPrintedTS("type x = y\n<number>\nz", "z;\n");
    expectPrintedTSX("type x = y\n<number>\nz\n</number>", "/* @__PURE__ */ React.createElement(\"number\", null, \"z\");\n");
    expectPrintedTS("type x = typeof y\n<number>\nz", "z;\n");
    expectPrintedTSX("type x = typeof y\n<number>\nz\n</number>", "/* @__PURE__ */ React.createElement(\"number\", null, \"z\");\n");
    expectPrintedTS("interface Foo { \n (a: number): a \n <T>(): void \n }", "");
    expectPrintedTSX("interface Foo { \n (a: number): a \n <T>(): void \n }", "");
    expectPrintedTS("interface Foo { \n (a: number): typeof a \n <T>(): void \n }", "");
    expectPrintedTSX("interface Foo { \n (a: number): typeof a \n <T>(): void \n }", "");
    expectParseErrorTS("type x = y\n<number>\nz\n</number>", "<stdin>: ERROR: Unterminated regular expression\n");
    expectParseErrorTSX("type x = y\n<number>\nz", "<stdin>: ERROR: Unexpected end of file before a closing \"number\" tag\n<stdin>: NOTE: The opening \"number\" tag is here:\n");
    expectParseErrorTS("type x = typeof y\n<number>\nz\n</number>", "<stdin>: ERROR: Unterminated regular expression\n");
    expectParseErrorTSX("type x = typeof y\n<number>\nz", "<stdin>: ERROR: Unexpected end of file before a closing \"number\" tag\n<stdin>: NOTE: The opening \"number\" tag is here:\n");

    // See: https://github.com/microsoft/TypeScript/issues/48654
    expectPrintedTS("x<true> y", "x < true > y;\n");
    expectPrintedTS("x<true>\ny", "x;\ny;\n");
    expectPrintedTS("x<true>\nif (y) {}", "x;\nif (y) {\n}\n");
    expectPrintedTS("x<true>\nimport 'y'", "x;\nimport \"y\";\n");
    expectPrintedTS("x<true>\nimport('y')", "x;\nimport(\"y\");\n");
    expectPrintedTS("x<true>\nimport.meta", "x;\nimport.meta;\n");
    expectPrintedTS("x<true> import('y')", "x < true > import(\"y\");\n");
    expectPrintedTS("x<true> import.meta", "x < true > import.meta;\n");
    expectPrintedTS("new x<number> y", "new x() < number > y;\n");
    expectPrintedTS("new x<number>\ny", "new x();\ny;\n");
    expectPrintedTS("new x<number>\nif (y) {}", "new x();\nif (y) {\n}\n");
    expectPrintedTS("new x<true>\nimport 'y'", "new x();\nimport \"y\";\n");
    expectPrintedTS("new x<true>\nimport('y')", "new x();\nimport(\"y\");\n");
    expectPrintedTS("new x<true>\nimport.meta", "new x();\nimport.meta;\n");
    expectPrintedTS("new x<true> import('y')", "new x() < true > import(\"y\");\n");
    expectPrintedTS("new x<true> import.meta", "new x() < true > import.meta;\n");

    // See: https://github.com/microsoft/TypeScript/issues/48759
    expectParseErrorTS("x<true>\nimport<T>('y')", "<stdin>: ERROR: Unexpected \"<\"\n");
    expectParseErrorTS("new x<true>\nimport<T>('y')", "<stdin>: ERROR: Unexpected \"<\"\n");

    expectParseErrorTS("return Array < ;", "<stdin>: ERROR: Unexpected \";\"\n");
    expectParseErrorTS("return Array < > ;", "<stdin>: ERROR: Unexpected \">\"\n");
    expectParseErrorTS("return Array < , > ;", "<stdin>: ERROR: Unexpected \",\"\n");
    expectPrintedTS("return Array < number > ;", "return Array;\n");
    expectPrintedTS("return Array < number > 1;", "return Array < number > 1;\n");
    expectPrintedTS("return Array < number > +1;", "return Array < number > 1;\n");
    expectPrintedTS("return Array < number > (1);", "return Array(1);\n");
    expectPrintedTS("return Array < number >> 1;", "return Array < number >> 1;\n");
    expectPrintedTS("return Array < number >>> 1;", "return Array < number >>> 1;\n");
    expectPrintedTS("return Array < Array < number >> ;", "return Array;\n");
    expectPrintedTS("return Array < Array < number > > ;", "return Array;\n");
    expectParseErrorTS("return Array < Array < number > > 1;", "<stdin>: ERROR: Unexpected \">\"\n");
    expectPrintedTS("return Array < Array < number >> 1;", "return Array < Array < number >> 1;\n");
    expectParseErrorTS("return Array < Array < number > > +1;", "<stdin>: ERROR: Unexpected \">\"\n");
    expectPrintedTS("return Array < Array < number >> +1;", "return Array < Array < number >> 1;\n");
    expectPrintedTS("return Array < Array < number >> (1);", "return Array(1);\n");
    expectPrintedTS("return Array < Array < number > > (1);", "return Array(1);\n");
    expectPrintedTS("return Array < number > in x;", "return Array in x;\n");
    expectPrintedTS("return Array < Array < number >> in x;", "return Array in x;\n");
    expectPrintedTS("return Array < Array < number > > in x;", "return Array in x;\n");
    expectPrintedTS("for (var x = Array < number > in y) ;", "x = Array;\nfor (var x in y) ;\n");
    expectPrintedTS("for (var x = Array < Array < number >> in y) ;", "x = Array;\nfor (var x in y) ;\n");
    expectPrintedTS("for (var x = Array < Array < number > > in y) ;", "x = Array;\nfor (var x in y) ;\n");

    // See: https://github.com/microsoft/TypeScript/pull/49353
    expectPrintedTS("F<{}> 0", "F < {} > 0;\n");
    expectPrintedTS("F<{}> class F<T> {}", "F < {} > class F {\n};\n");
    expectPrintedTS("f<{}> function f<T>() {}", "f < {} > function f() {\n};\n");
    expectPrintedTS("F<{}>\n0", "F;\n0;\n");
    expectPrintedTS("F<{}>\nclass F<T> {}", "F;\nclass F {\n}\n");
    expectPrintedTS("f<{}>\nfunction f<T>() {}", "f;\nfunction f() {\n}\n");
}

TEST(JsParser, TestTSExponentiation) {
    // More info: https://github.com/microsoft/TypeScript/issues/41755
    expectParseErrorTS("await x! ** 2", "<stdin>: ERROR: Unexpected \"**\"\n");
    expectPrintedTS("await x as any ** 2", "(await x) ** 2;\n");
}

TEST(JsParser, TestTSImport) {
    expectPrintedTS("import {x} from 'foo'", "");
    expectPrintedTS("import {x} from 'foo'; log(x)", "import { x } from \"foo\";\nlog(x);\n");
    expectPrintedTS("import {x, y as z} from 'foo'; log(z)", "import { y as z } from \"foo\";\nlog(z);\n");

    expectPrintedTS("import x from 'foo'", "");
    expectPrintedTS("import x from 'foo'; log(x)", "import x from \"foo\";\nlog(x);\n");

    expectPrintedTS("import * as ns from 'foo'", "");
    expectPrintedTS("import * as ns from 'foo'; log(ns)", "import * as ns from \"foo\";\nlog(ns);\n");

    // Dead control flow must not affect usage tracking
    expectPrintedTS("import {x} from 'foo'; if (false) log(x)", "import \"foo\";\nif (false) log(x);\n");
    expectPrintedTS("import x from 'foo'; if (false) log(x)", "import \"foo\";\nif (false) log(x);\n");
    expectPrintedTS("import * as ns from 'foo'; if (false) log(ns)", "import \"foo\";\nif (false) log(ns);\n");
}

// This is TypeScript-specific export syntax
TEST(JsParser, TestTSExportEquals) {
    // This use of the "export" keyword should not trigger strict mode because
    // this syntax works in CommonJS modules, not in ECMAScript modules
    expectPrintedTS("export = []", "module.exports = [];\n");
    expectPrintedTS("export = []; with ({}) ;", "with ({}) ;\nmodule.exports = [];\n");
}

// This is TypeScript-specific import syntax
TEST(JsParser, TestTSImportEquals) {
    // This use of the "export" keyword should not trigger strict mode because
    // this syntax works in CommonJS modules, not in ECMAScript modules
    expectPrintedTS("import x = require('y')", "const x = require(\"y\");\n");
    expectPrintedTS("import x = require('y'); with ({}) ;", "const x = require(\"y\");\nwith ({}) ;\n");

    expectPrintedTS("import x = require('foo'); x()", "const x = require(\"foo\");\nx();\n");
    expectPrintedTS("import x = require('foo')\nx()", "const x = require(\"foo\");\nx();\n");
    expectPrintedTS("import x = require\nx()", "const x = require;\nx();\n");
    expectPrintedTS("import x = foo.bar; x()", "const x = foo.bar;\nx();\n");
    expectPrintedTS("import x = foo.bar\nx()", "const x = foo.bar;\nx();\n");
    expectParseErrorTS("import x = foo()", "<stdin>: ERROR: Expected \";\" but found \"(\"\n");
    expectParseErrorTS("import x = foo<T>.bar", "<stdin>: ERROR: Expected \";\" but found \"<\"\n");
    expectParseErrorTS("{ import x = foo.bar }", "<stdin>: ERROR: Unexpected \"x\"\n");

    expectPrintedTS("export import x = require('foo'); x()", "export const x = require(\"foo\");\nx();\n");
    expectPrintedTS("export import x = require('foo')\nx()", "export const x = require(\"foo\");\nx();\n");
    expectPrintedTS("export import x = foo.bar; x()", "export const x = foo.bar;\nx();\n");
    expectPrintedTS("export import x = foo.bar\nx()", "export const x = foo.bar;\nx();\n");

    expectParseError("export import foo = bar", "<stdin>: ERROR: Unexpected \"import\"\n");
    expectParseErrorTS("export import {foo} from 'bar'", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("export import foo from 'bar'", "<stdin>: ERROR: Expected \"=\" but found \"from\"\n");
    expectParseErrorTS("export import foo = bar; var x; export {x as foo}",
        R"(<stdin>: ERROR: Multiple exports with the same name "foo"
<stdin>: NOTE: The name "foo" was originally exported here:
)");
    expectParseErrorTS("{ export import foo = bar }", "<stdin>: ERROR: Unexpected \"export\"\n");

    const std::string errorText = R"(<stdin>: WARNING: This assignment will throw because "x" is a constant
<stdin>: NOTE: The symbol "x" was declared a constant here:
)";
    expectParseErrorTS("import x = require('y'); x = require('z')", errorText);
    expectParseErrorTS("import x = y.z; x = z.y", errorText);

    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kConstAndLet, "import x = require('y')", "var x = require(\"y\");\n");
}

TEST(JsParser, TestTSImportEqualsInNamespace) {
    expectPrintedTS("namespace ns { import foo = bar }", "");
    expectPrintedTS("namespace ns { import foo = bar; type x = foo.x }", "");
    expectPrintedTS("namespace ns { import foo = bar.x; foo }",
        R"(var ns;
((ns) => {
  const foo = bar.x;
  foo;
})(ns || (ns = {}));
)");
    expectPrintedTS("namespace ns { export import foo = bar }",
        R"(var ns;
((ns) => {
  ns.foo = bar;
})(ns || (ns = {}));
)");
    expectPrintedTS("namespace ns { export import foo = bar.x; foo }",
        R"(var ns;
((ns) => {
  ns.foo = bar.x;
  ns.foo;
})(ns || (ns = {}));
)");
    expectParseErrorTS("namespace ns { import {foo} from 'bar' }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("namespace ns { import foo from 'bar' }", "<stdin>: ERROR: Expected \"=\" but found \"from\"\n");
    expectParseErrorTS("namespace ns { export import {foo} from 'bar' }", "<stdin>: ERROR: Expected identifier but found \"{\"\n");
    expectParseErrorTS("namespace ns { export import foo from 'bar' }", "<stdin>: ERROR: Expected \"=\" but found \"from\"\n");
    expectParseErrorTS("namespace ns { { import foo = bar } }", "<stdin>: ERROR: Unexpected \"foo\"\n");
    expectParseErrorTS("namespace ns { { export import foo = bar } }", "<stdin>: ERROR: Unexpected \"export\"\n");

    expectPrintedWithUnsupportedFeaturesTS(compat::JSFeature::kConstAndLet, "namespace ns { import x = require('y'); x }",
        R"(var ns;
((ns) => {
  var x = require("y");
  x;
})(ns || (ns = {}));
)");
}

TEST(JsParser, TestTSTypeOnlyImport) {
    expectPrintedTS("import type foo from 'bar'; x", "x;\n");
    expectPrintedTS("import type foo from 'bar'\nx", "x;\n");
    expectPrintedTS("import type from from 'bar'; x", "x;\n");
    expectPrintedTS("import type * as foo from 'bar'; x", "x;\n");
    expectPrintedTS("import type * as foo from 'bar'\nx", "x;\n");
    expectPrintedTS("import type {foo, bar as baz} from 'bar'; x", "x;\n");
    expectPrintedTS("import type {'foo' as bar} from 'bar'\nx", "x;\n");
    expectPrintedTS("import type foo = require('bar'); x", "x;\n");
    expectPrintedTS("import type foo = bar.baz; x", "x;\n");
    expectPrintedTS("import type from = require('bar'); x", "x;\n");

    expectPrintedTS("import type = bar; type", "const type = bar;\ntype;\n");
    expectPrintedTS("import type = foo.bar; type", "const type = foo.bar;\ntype;\n");
    expectPrintedTS("import type = require('type'); type", "const type = require(\"type\");\ntype;\n");
    expectPrintedTS("import type from 'bar'; type", "import type from \"bar\";\ntype;\n");

    expectPrintedTS("import { type } from 'mod'; type", "import { type } from \"mod\";\ntype;\n");
    expectPrintedTS("import { x, type foo } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { x, type as } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { x, type foo as bar } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { x, type foo as as } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { type as as } from 'mod'; as", "import { type as as } from \"mod\";\nas;\n");
    expectPrintedTS("import { type as foo } from 'mod'; foo", "import { type as foo } from \"mod\";\nfoo;\n");
    expectPrintedTS("import { type as type } from 'mod'; type", "import { type } from \"mod\";\ntype;\n");
    expectPrintedTS("import { x, type as as foo } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { x, type as as as } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { x, type type as as } from 'mod'; x", "import { x } from \"mod\";\nx;\n");
    expectPrintedTS("import { x, \\u0074ype y } from 'mod'; x, y", "import { x } from \"mod\";\nx, y;\n");
    expectPrintedTS("import { x, type if as y } from 'mod'; x, y", "import { x } from \"mod\";\nx, y;\n");

    expectPrintedTS("import a = b; import c = a.c", "");
    expectPrintedTS("import c = a.c; import a = b", "");
    expectPrintedTS("import a = b; import c = a.c; c()", "const a = b;\nconst c = a.c;\nc();\n");
    expectPrintedTS("import c = a.c; import a = b; c()", "const c = a.c;\nconst a = b;\nc();\n");

    expectParseErrorTS("import type", "<stdin>: ERROR: Expected \"from\" but found end of file\n");
    expectParseErrorTS("import type * foo", "<stdin>: ERROR: Expected \"as\" but found \"foo\"\n");
    expectParseErrorTS("import type * as 'bar'", "<stdin>: ERROR: Expected identifier but found \"'bar'\"\n");
    expectParseErrorTS("import type { 'bar' }", "<stdin>: ERROR: Expected \"as\" but found \"}\"\n");

    expectParseErrorTS("import type foo, * as foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \",\"\n");
    expectParseErrorTS("import type foo, {foo} from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \",\"\n");
    expectParseErrorTS("import type from, * as foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \",\"\n");
    expectParseErrorTS("import type from, {foo} from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \",\"\n");
    expectParseErrorTS("import type * as foo = require('bar')", "<stdin>: ERROR: Expected \"from\" but found \"=\"\n");
    expectParseErrorTS("import type {foo} = require('bar')", "<stdin>: ERROR: Expected \"from\" but found \"=\"\n");

    expectParseErrorTS("import { type as export } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"export\"\n");
    expectParseErrorTS("import { type as as export } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"export\"\n");
    expectParseErrorTS("import { type import } from 'mod'", "<stdin>: ERROR: Expected \"as\" but found \"}\"\n");
    expectParseErrorTS("import { type foo bar } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"bar\"\n");
    expectParseErrorTS("import { type foo as } from 'mod'", "<stdin>: ERROR: Expected identifier but found \"}\"\n");
    expectParseErrorTS("import { type foo as bar baz } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"baz\"\n");
    expectParseErrorTS("import { type as as as as } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"as\"\n");
    expectParseErrorTS("import { type \\u0061s x } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"x\"\n");
    expectParseErrorTS("import { type x \\u0061s y } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"\\\\u0061s\"\n");
    expectParseErrorTS("import { type x as if } from 'mod'", "<stdin>: ERROR: Expected identifier but found \"if\"\n");
    expectParseErrorTS("import { type as if } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"if\"\n");

    // Forbidden names
    expectParseErrorTS("import { type as eval } from 'mod'", "<stdin>: ERROR: Cannot use \"eval\" as an identifier here:\n");
    expectParseErrorTS("import { type as arguments } from 'mod'", "<stdin>: ERROR: Cannot use \"arguments\" as an identifier here:\n");

    // Arbitrary module namespace identifier names
    expectPrintedTS("import { x, type 'y' as z } from 'mod'; x, z", "import { x } from \"mod\";\nx, z;\n");
    expectParseErrorTS("import { x, type 'y' } from 'mod'", "<stdin>: ERROR: Expected \"as\" but found \"}\"\n");
    expectParseErrorTS("import { x, type 'y' as } from 'mod'", "<stdin>: ERROR: Expected identifier but found \"}\"\n");
    expectParseErrorTS("import { x, type 'y' as 'z' } from 'mod'", "<stdin>: ERROR: Expected identifier but found \"'z'\"\n");
    expectParseErrorTS("import { x, type as 'y' } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"'y'\"\n");
    expectParseErrorTS("import { x, type y as 'z' } from 'mod'", "<stdin>: ERROR: Expected identifier but found \"'z'\"\n");

    // See: https://github.com/tc39/proposal-defer-import-eval
    expectPrintedTS("import defer * as foo from 'bar'", "");
    expectPrintedTS("import defer * as foo from 'bar'; let x: foo.Type", "let x;\n");
    expectPrintedTS("import defer * as foo from 'bar'; let x = foo.value", "import defer * as foo from \"bar\";\nlet x = foo.value;\n");
    expectPrintedTS("import type defer from 'bar'", "");
    expectParseErrorTS("import type defer * as foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"*\"\n");

    // See: https://github.com/tc39/proposal-source-phase-imports
    expectPrintedTS("import type source from 'bar'", "");
    expectPrintedTS("import source foo from 'bar'", "");
    expectPrintedTS("import source foo from 'bar'; let x: foo", "let x;\n");
    expectPrintedTS("import source foo from 'bar'; let x = foo", "import source foo from \"bar\";\nlet x = foo;\n");
    expectPrintedTS("import source type from 'bar'", "");
    expectParseErrorTS("import source type foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"foo\"\n");
    expectParseErrorTS("import type source foo from 'bar'", "<stdin>: ERROR: Expected \"from\" but found \"foo\"\n");
}

TEST(JsParser, TestTSTypeOnlyExport) {
    expectPrintedTS("export type {foo, bar as baz} from 'bar'", "");
    expectPrintedTS("export type {foo, bar as baz}", "");
    expectPrintedTS("export type {foo} from 'bar'; x", "x;\n");
    expectPrintedTS("export type {foo} from 'bar'\nx", "x;\n");
    expectPrintedTS("export type {default} from 'bar'", "");
    expectParseErrorTS("export type {default}", "<stdin>: ERROR: Expected identifier but found \"default\"\n");

    expectPrintedTS("export { type } from 'mod'; type", "export { type } from \"mod\";\ntype;\n");
    expectPrintedTS("export { type, as } from 'mod'", "export { type, as } from \"mod\";\n");
    expectPrintedTS("export { x, type foo } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { x, type as } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { x, type foo as bar } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { x, type foo as as } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { type as as } from 'mod'; as", "export { type as as } from \"mod\";\nas;\n");
    expectPrintedTS("export { type as foo } from 'mod'; foo", "export { type as foo } from \"mod\";\nfoo;\n");
    expectPrintedTS("export { type as type } from 'mod'; type", "export { type } from \"mod\";\ntype;\n");
    expectPrintedTS("export { x, type as as foo } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { x, type as as as } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { x, type type as as } from 'mod'; x", "export { x } from \"mod\";\nx;\n");
    expectPrintedTS("export { x, \\u0074ype y }; let x, y", "export { x };\nlet x, y;\n");
    expectPrintedTS("export { x, \\u0074ype y } from 'mod'", "export { x } from \"mod\";\n");
    expectPrintedTS("export { x, type if } from 'mod'", "export { x } from \"mod\";\n");
    expectPrintedTS("export { x, type y as if }; let x", "export { x };\nlet x;\n");

    expectParseErrorTS("export { type foo bar } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"bar\"\n");
    expectParseErrorTS("export { type foo as } from 'mod'", "<stdin>: ERROR: Expected identifier but found \"}\"\n");
    expectParseErrorTS("export { type foo as bar baz } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"baz\"\n");
    expectParseErrorTS("export { type as as as as } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"as\"\n");
    expectParseErrorTS("export { type \\u0061s x } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"x\"\n");
    expectParseErrorTS("export { type x \\u0061s y } from 'mod'", "<stdin>: ERROR: Expected \"}\" but found \"\\\\u0061s\"\n");
    expectParseErrorTS("export { x, type if }", "<stdin>: ERROR: Expected identifier but found \"if\"\n");

    // Arbitrary module namespace identifier names
    expectPrintedTS("export { type as \"\" } from 'mod'", "export { type as \"\" } from \"mod\";\n");
    expectPrintedTS("export { x, type as as \"\" } from 'mod'", "export { x } from \"mod\";\n");
    expectPrintedTS("export { x, type x as \"\" } from 'mod'", "export { x } from \"mod\";\n");
    expectPrintedTS("export { x, type \"\" as x } from 'mod'", "export { x } from \"mod\";\n");
    expectPrintedTS("export { x, type \"\" as \" \" } from 'mod'", "export { x } from \"mod\";\n");
    expectPrintedTS("export { x, type \"\" } from 'mod'", "export { x } from \"mod\";\n");
    expectParseErrorTS("export { type \"\" }", "<stdin>: ERROR: Expected identifier but found \"\"\"\"\n");
    expectParseErrorTS("export { type \"\" as x }", "<stdin>: ERROR: Expected identifier but found \"\"\"\"\n");
    expectParseErrorTS("export { type \"\" as \" \" }", "<stdin>: ERROR: Expected identifier but found \"\"\"\"\n");

    // Named exports should be removed if they don't refer to a local symbol
    expectPrintedTS("const Foo = {}; export {Foo}", "const Foo = {};\nexport { Foo };\n");
    expectPrintedTS("type Foo = {}; export {Foo}", "export {};\n");
    expectPrintedTS("const Foo = {}; export {Foo as Bar}", "const Foo = {};\nexport { Foo as Bar };\n");
    expectPrintedTS("type Foo = {}; export {Foo as Bar}", "export {};\n");
    expectPrintedTS("import Foo from 'foo'; export {Foo}", "import Foo from \"foo\";\nexport { Foo };\n");
    expectPrintedTS("import {Foo} from 'foo'; export {Foo}", "import { Foo } from \"foo\";\nexport { Foo };\n");
    expectPrintedTS("import * as Foo from 'foo'; export {Foo}", "import * as Foo from \"foo\";\nexport { Foo };\n");
    expectPrintedTS("{ var Foo; } export {Foo}", "{\n  var Foo;\n}\nexport { Foo };\n");
    expectPrintedTS("{ let Foo; } export {Foo}", "{\n  let Foo;\n}\nexport {};\n");
    expectPrintedTS("export {Foo}", "export {};\n");
    expectParseError("export {Foo}", "<stdin>: ERROR: \"Foo\" is not declared in this file\n");

    // This is a syntax error in TypeScript, but we parse it anyway because
    // because we always discard all type annotations (even invalid ones).
    expectPrintedTS("export type * from 'foo'\nbar", "bar;\n");
    expectPrintedTS("export type * as foo from 'bar'; foo", "foo;\n");
    expectPrintedTS("export type * as 'f o' from 'bar'; foo", "foo;\n");
}

TEST(JsParser, TestTSOptionalChain) {
    expectParseError("a?.<T>()", "<stdin>: ERROR: Expected identifier but found \"<\"\n");
    expectParseError("a?.<<T>() => T>()", "<stdin>: ERROR: Expected identifier but found \"<<\"\n");
    expectPrintedTS("a?.<T>()", "a?.();\n");
    expectPrintedTS("a?.<<T>() => T>()", "a?.();\n");
    expectParseErrorTS("a?.<T>b", "<stdin>: ERROR: Expected \"(\" but found \"b\"\n");
    expectParseErrorTS("a?.<T>[b]", "<stdin>: ERROR: Expected \"(\" but found \"[\"\n");
    expectParseErrorTS("a?.<<T>() => T>b", "<stdin>: ERROR: Expected \"(\" but found \"b\"\n");
    expectParseErrorTS("a?.<<T>() => T>[b]", "<stdin>: ERROR: Expected \"(\" but found \"[\"\n");

    expectPrintedTS("a?.b.c", "a?.b.c;\n");
    expectPrintedTS("(a?.b).c", "(a?.b).c;\n");
    expectPrintedTS("a?.b!.c", "a?.b.c;\n");

    expectPrintedTS("a?.b[c]", "a?.b[c];\n");
    expectPrintedTS("(a?.b)[c]", "(a?.b)[c];\n");
    expectPrintedTS("a?.b![c]", "a?.b[c];\n");

    expectPrintedTS("a?.b(c)", "a?.b(c);\n");
    expectPrintedTS("(a?.b)(c)", "(a?.b)(c);\n");
    expectPrintedTS("a?.b!(c)", "a?.b(c);\n");

    expectPrintedTS("a?.b<T>(c)", "a?.b(c);\n");
    expectPrintedTS("a?.b<+T>(c)", "a?.b < +T > c;\n");
    expectPrintedTS("a?.b<<T>() => T>(c)", "a?.b(c);\n");
}

TEST(JsParser, TestTSJSX) {
    expectParseErrorTSX("<div>></div>",
        "<stdin>: ERROR: The character \">\" is not valid inside a JSX element\n"
        "NOTE: Did you mean to escape it as \"{'>'}\" instead?\n");
    expectParseErrorTSX("<div>{1}}</div>",
        "<stdin>: ERROR: The character \"}\" is not valid inside a JSX element\n"
        "NOTE: Did you mean to escape it as \"{'}'}\" instead?\n");

    expectPrintedTS("const x = <number>1", "const x = 1;\n");
    expectPrintedTSX("const x = <number>1</number>", "const x = /* @__PURE__ */ React.createElement(\"number\", null, \"1\");\n");
    expectParseErrorTSX("const x = <number>1", "<stdin>: ERROR: Unexpected end of file before a closing \"number\" tag\n<stdin>: NOTE: The opening \"number\" tag is here:\n");

    expectPrintedTSX("<x>a{}c</x>", "/* @__PURE__ */ React.createElement(\"x\", null, \"a\", \"c\");\n");
    expectPrintedTSX("<x>a{/* comment */}c</x>", "/* @__PURE__ */ React.createElement(\"x\", null, \"a\", \"c\");\n");
    expectPrintedTSX("<x>a{b}c</x>", "/* @__PURE__ */ React.createElement(\"x\", null, \"a\", b, \"c\");\n");
    expectPrintedTSX("<x>a{...b}c</x>", "/* @__PURE__ */ React.createElement(\"x\", null, \"a\", ...b, \"c\");\n");

    expectPrintedTSX("const x = <Foo<T>></Foo>", "const x = /* @__PURE__ */ React.createElement(Foo, null);\n");
    expectPrintedTSX("const x = <Foo<T> data-foo></Foo>", "const x = /* @__PURE__ */ React.createElement(Foo, { \"data-foo\": true });\n");
    expectParseErrorTSX("const x = <Foo<T>=>", "<stdin>: ERROR: Expected \">\" but found \"=>\"\n");

    expectPrintedTS("const x = <T>() => {}", "const x = () => {\n};\n");
    expectPrintedTS("const x = <T>(y)", "const x = y;\n");
    expectPrintedTS("const x = <T>(y, z)", "const x = (y, z);\n");
    expectPrintedTS("const x = <T>(y: T) => {}", "const x = (y) => {\n};\n");
    expectPrintedTS("const x = <T>(y, z) => {}", "const x = (y, z) => {\n};\n");
    expectPrintedTS("const x = <T = X>(y: T) => {}", "const x = (y) => {\n};\n");
    expectPrintedTS("const x = <T = X>(y, z) => {}", "const x = (y, z) => {\n};\n");
    expectPrintedTS("const x = <T extends X>(y: T) => {}", "const x = (y) => {\n};\n");
    expectPrintedTS("const x = <T extends X>(y, z) => {}", "const x = (y, z) => {\n};\n");
    expectPrintedTS("const x = <T extends X = Y>(y: T) => {}", "const x = (y) => {\n};\n");
    expectPrintedTS("const x = <T extends X = Y>(y, z) => {}", "const x = (y, z) => {\n};\n");

    expectPrintedTS("const x = async <T>() => {}", "const x = async () => {\n};\n");
    expectPrintedTS("const x = async <T>(y)", "const x = async(y);\n");
    expectPrintedTS("const x = async\n<T>(y)", "const x = async(y);\n");
    expectPrintedTS("const x = async <T>(y, z)", "const x = async(y, z);\n");
    expectPrintedTS("const x = async <T>(y: T) => {}", "const x = async (y) => {\n};\n");
    expectPrintedTS("const x = async <T>(y, z) => {}", "const x = async (y, z) => {\n};\n");
    expectPrintedTS("const x = async <T = X>(y: T) => {}", "const x = async (y) => {\n};\n");
    expectPrintedTS("const x = async <T = X>(y, z) => {}", "const x = async (y, z) => {\n};\n");
    expectPrintedTS("const x = async <T extends X>(y: T) => {}", "const x = async (y) => {\n};\n");
    expectPrintedTS("const x = async <T extends X>(y, z) => {}", "const x = async (y, z) => {\n};\n");
    expectPrintedTS("const x = async <T extends X = Y>(y: T) => {}", "const x = async (y) => {\n};\n");
    expectPrintedTS("const x = async <T extends X = Y>(y, z) => {}", "const x = async (y, z) => {\n};\n");
    expectPrintedTS("const x = (async <T, X> y)", "const x = (async < T, X > y);\n");
    expectPrintedTS("const x = (async <T, X>(y))", "const x = async(y);\n");
    expectParseErrorTS("const x = async <T,>(y)", "<stdin>: ERROR: Expected \"=>\" but found end of file\n");
    expectParseErrorTS("const x = async <T>(y: T)", "<stdin>: ERROR: Unexpected \":\"\n");
    expectParseErrorTS("const x = async\n<T>() => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseErrorTS("const x = async\n<T>(x) => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");

    expectPrintedTS("const x = <{}>() => {}", "const x = () => {\n};\n");
    expectPrintedTS("const x = <{}>(y)", "const x = y;\n");
    expectPrintedTS("const x = <{}>(y, z)", "const x = (y, z);\n");
    expectPrintedTS("const x = <{}>(y, z) => {}", "const x = (y, z) => {\n};\n");

    expectPrintedTS("const x = <[]>() => {}", "const x = () => {\n};\n");
    expectPrintedTS("const x = <[]>(y)", "const x = y;\n");
    expectPrintedTS("const x = <[]>(y, z)", "const x = (y, z);\n");
    expectPrintedTS("const x = <[]>(y, z) => {}", "const x = (y, z) => {\n};\n");

    const std::string invalid = "<stdin>: ERROR: The character \">\" is not valid inside a JSX element\nNOTE: Did you mean to escape it as \"{'>'}\" instead?\n";
    const std::string invalidWithHint = "<stdin>: ERROR: The character \">\" is not valid inside a JSX element\n<stdin>: NOTE: TypeScript's TSX syntax interprets "
        "arrow functions with a single generic type parameter as an opening JSX element. If you want it to be interpreted as an arrow function instead, "
        "you need to add a trailing comma after the type parameter to disambiguate:\n";
    expectPrintedTSX("<T extends/>", "/* @__PURE__ */ React.createElement(T, { extends: true });\n");
    expectPrintedTSX("<T extends>(y) = {}</T>", "/* @__PURE__ */ React.createElement(T, { extends: true }, \"(y) = \");\n");
    expectParseErrorTSX("<T extends X/>", "<stdin>: ERROR: Expected \">\" but found \"/\"\n");
    expectParseErrorTSX("<T extends X>(y) = {}</T>", "<stdin>: ERROR: Expected \"=>\" but found \"=\"\n");
    expectParseErrorTSX("(<T>(y) => {}</T>)", invalidWithHint);
    expectParseErrorTSX("(<T>(x: X<Y>) => {}</Y></T>)", invalidWithHint);
    expectParseErrorTSX("(<T extends>(y) => {}</T>)", invalid);
    expectParseErrorTSX("(<T extends={false}>(y) => {}</T>)", invalid);
    expectPrintedTSX("(<T = X>(y) => {})", "((y) => {\n});\n");
    expectPrintedTSX("(<T extends X>(y) => {})", "((y) => {\n});\n");
    expectPrintedTSX("(<T extends X = Y>(y) => {})", "((y) => {\n});\n");
    expectPrintedTSX("(<T,>() => {})", "(() => {\n});\n");
    expectPrintedTSX("(<T, X>(y) => {})", "((y) => {\n});\n");
    expectPrintedTSX("(<T, X>(y): (() => {}) => {})", "((y) => {\n});\n");
    expectParseErrorTSX("(<T>() => {})", invalidWithHint + "<stdin>: ERROR: Unexpected end of file before a closing \"T\" tag\n<stdin>: NOTE: The opening \"T\" tag is here:\n");
    expectParseErrorTSX("(<T>(x: X<Y>) => {})", invalidWithHint + "<stdin>: ERROR: Unexpected end of file before a closing \"Y\" tag\n<stdin>: NOTE: The opening \"Y\" tag is here:\n");
    expectParseErrorTSX("(<T>(x: X<Y>) => {})</Y>", invalidWithHint + "<stdin>: ERROR: Unexpected end of file before a closing \"T\" tag\n<stdin>: NOTE: The opening \"T\" tag is here:\n");
    expectParseErrorTSX("(<[]>(y))", "<stdin>: ERROR: Expected identifier but found \"[\"\n");
    expectParseErrorTSX("(<T[]>(y))", "<stdin>: ERROR: Expected \">\" but found \"[\"\n");
    expectParseErrorTSX("(<T = X>(y))", "<stdin>: ERROR: Expected \"=>\" but found \")\"\n");
    expectParseErrorTSX("(<T, X>(y))", "<stdin>: ERROR: Expected \"=>\" but found \")\"\n");
    expectParseErrorTSX("(<T, X>y => {})", "<stdin>: ERROR: Expected \"(\" but found \"y\"\n");

    // TypeScript doesn't currently parse these even though it seems unambiguous
    expectPrintedTSX("async <T,>() => {}", "async () => {\n};\n");
    expectPrintedTSX("async <T extends X>() => {}", "async () => {\n};\n");
    expectPrintedTSX("async <T>()", "async();\n");
    expectParseErrorTSX("async <T>() => {}", "<stdin>: ERROR: Expected \";\" but found \"=>\"\n");
    expectParseErrorTSX("async <T extends>() => {}", "<stdin>: ERROR: Expected \";\" but found \"extends\"\n");
}

TEST(JsParser, TestTSNoAmbiguousLessThan) {
    expectPrintedTSNoAmbiguousLessThan("(<T,>() => {})", "(() => {\n});\n");
    expectPrintedTSNoAmbiguousLessThan("(<T, X>() => {})", "(() => {\n});\n");
    expectPrintedTSNoAmbiguousLessThan("(<T extends X>() => {})", "(() => {\n});\n");
    expectParseErrorTSNoAmbiguousLessThan("(<T>x)",
        "<stdin>: ERROR: This syntax is not allowed in files with the \".mts\" or \".cts\" extension\n");
    expectParseErrorTSNoAmbiguousLessThan("(<T>() => {})",
        "<stdin>: ERROR: This syntax is not allowed in files with the \".mts\" or \".cts\" extension\n");
    expectParseErrorTSNoAmbiguousLessThan("(<T>(x) => {})",
        "<stdin>: ERROR: This syntax is not allowed in files with the \".mts\" or \".cts\" extension\n");
    expectParseErrorTSNoAmbiguousLessThan("<x>y</x>",
        "<stdin>: ERROR: This syntax is not allowed in files with the \".mts\" or \".cts\" extension\n"
        "<stdin>: ERROR: Unterminated regular expression\n");
    expectParseErrorTSNoAmbiguousLessThan("<x extends></x>",
        "<stdin>: ERROR: This syntax is not allowed in files with the \".mts\" or \".cts\" extension\n"
        "<stdin>: ERROR: Unexpected \">\"\n");
    expectParseErrorTSNoAmbiguousLessThan("<x extends={y}></x>",
        "<stdin>: ERROR: This syntax is not allowed in files with the \".mts\" or \".cts\" extension\n"
        "<stdin>: ERROR: Unexpected \"=\"\n");
}

TEST(JsParser, TestTSClassSideEffectOrder) {
    // The order of computed property side effects must not change
    expectPrintedAssignSemanticsTS(R"(class Foo {
	[a()]() {}
	[b()];
	[c()] = 1;
	[d()]() {}
	static [e()];
	static [f()] = 1;
	static [g()]() {}
	[h()];
}
)",
        R"(var _a, _b, _c;
class Foo {
  constructor() {
    this[_c] = 1;
  }
  [a()]() {
  }
  [(b(), _c = c(), d())]() {
  }
  static {
    this[_b] = 1;
  }
  static [(e(), _b = f(), _a = g(), h(), _a)]() {
  }
}
)");
    expectPrintedAssignSemanticsTS(R"(class Foo {
	static [x()] = 1;
}
)",
        R"(var _a;
_a = x();
class Foo {
  static {
    this[_a] = 1;
  }
}
)");
    expectPrintedAssignSemanticsTargetTS(2021, R"(class Foo {
	[a()]() {}
	[b()];
	[c()] = 1;
	[d()]() {}
	static [e()];
	static [f()] = 1;
	static [g()]() {}
	[h()];
}
)",
        R"(var _a, _b, _c;
class Foo {
  constructor() {
    this[_c] = 1;
  }
  [a()]() {
  }
  [(b(), _c = c(), d())]() {
  }
  static [(e(), _b = f(), _a = g(), h(), _a)]() {
  }
}
Foo[_b] = 1;
)");
}

TEST(JsParser, TestTSMangleStringEnumLength) {
    expectPrintedTS("enum x { y = '' } z = x.y.length",
        "var x = /* @__PURE__ */ ((x) => {\n  x[\"y\"] = \"\";\n  return x;\n})(x || {});\nz = \"\" /* y */.length;\n");

    expectPrintedMangleTS("enum x { y = '' } x.y.length++",
        "var x = /* @__PURE__ */ ((x) => (x.y = \"\", x))(x || {});\n\"\" /* y */.length++;\n");

    expectPrintedMangleTS("enum x { y = '' } x.y.length = z",
        "var x = /* @__PURE__ */ ((x) => (x.y = \"\", x))(x || {});\n\"\" /* y */.length = z;\n");

    expectPrintedMangleTS("enum x { y = '' } z = x.y.length",
        "var x = /* @__PURE__ */ ((x) => (x.y = \"\", x))(x || {});\nz = 0;\n");

    expectPrintedMangleTS("enum x { y = 'abc' } z = x.y.length",
        "var x = /* @__PURE__ */ ((x) => (x.y = \"abc\", x))(x || {});\nz = 3;\n");

    expectPrintedMangleTS("enum x { y = 'ȧḃċ' } z = x.y.length",
        "var x = /* @__PURE__ */ ((x) => (x.y = \"ȧḃċ\", x))(x || {});\nz = 3;\n");

    expectPrintedMangleTS("enum x { y = '👯‍♂️' } z = x.y.length",
        "var x = /* @__PURE__ */ ((x) => (x.y = \"👯‍♂️\", x))(x || {});\nz = 5;\n");
}

TEST(JsParser, TestTSES5) {
    // Errors from lowering hypothetical arrow function arguments to ES5 should
    // not leak out when backtracking. This comes up when parentheses are followed
    // by a colon in TypeScript because the colon could deliminate an arrow
    expectPrintedTargetTS(2015, "0 ? ([]) : 0", "0 ? [] : 0;\n");
    expectPrintedTargetTS(2015, "0 ? ({}) : 0", "0 ? {} : 0;\n");
    expectPrintedTargetTS(5, "0 ? ([]) : 0", "0 ? [] : 0;\n");
    expectPrintedTargetTS(5, "0 ? ({}) : 0", "0 ? {} : 0;\n");
    expectPrintedTargetTS(2015, "0 ? ([]): 0 => 0 : 0", "0 ? ([]) => 0 : 0;\n");
    expectPrintedTargetTS(2015, "0 ? ({}): 0 => 0 : 0", "0 ? ({}) => 0 : 0;\n");
    expectParseErrorTargetTS(5, "0 ? ([]): 0 => 0 : 0", "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
    expectParseErrorTargetTS(5, "0 ? ({}): 0 => 0 : 0", "<stdin>: ERROR: Transforming destructuring to the configured target environment is not supported yet\n");
}

TEST(JsParser, TestTSUsing) {
    expectPrintedTS("using x = y", "using x = y;\n");
    expectPrintedTS("using x: any = y", "using x = y;\n");
    expectPrintedTS("using x: any = y, z: any = _", "using x = y, z = _;\n");
    expectParseErrorTS("export using x: any = y", "<stdin>: ERROR: Unexpected \"using\"\n");
    expectParseErrorTS("namespace ns { export using x: any = y }", "<stdin>: ERROR: Unexpected \"using\"\n");
}
