---
title: Compiler types
description: The types of the lexer, parser and printer stages.
---

<!-- GENERATED from package/npm/guchho/index.d.ts. Do not edit by hand. -->

# Compiler types

The tokens, nodes, options and results of `lexer`, `parse` and `print`.

In this page:

- [`CompileSource`](#compilesource)
- [`AstHandle`](#asthandle)
- [`SourcePosition`](#sourceposition)
- [`CompileResult`](#compileresult)
- [`Language`](#language)
- [`CompileSourceFileOptions`](#compilesourcefileoptions)
- [`CompileOptions`](#compileoptions)
- [`LexOptions`](#lexoptions)
- [`ParseOptions`](#parseoptions)
- [`PrintOptions`](#printoptions)
- [`HtmlTokenKind`](#htmltokenkind)
- [`HtmlToken`](#htmltoken)
- [`CssTokenKind`](#csstokenkind)
- [`CssToken`](#csstoken)
- [`CssComment`](#csscomment)
- [`JsToken`](#jstoken)
- [`HtmlNode`](#htmlnode)
- [`CssMinifyOptions`](#cssminifyoptions)
- [`CssRule`](#cssrule)
- [`JsSymbol`](#jssymbol)
- [`LexResult`](#lexresult)
- [`ParseResult`](#parseresult)
- [`PrintResult`](#printresult)

## CompileSource

The source a compile command reads. Bytes pass through as bytes.

```ts
export type CompileSource = string | Uint8Array
```

## AstHandle

The handle a parse returned. Opaque to this package: it is the service's id for a tree it holds, and only transform* and print* may use it, in the same language it came from.

```ts
export type AstHandle = number
```

## SourcePosition

Where a token or a node sat in the source. Zero when no location was kept.

```ts
export interface SourcePosition {
  /** Offset in bytes from the start of the input. */
  start: number
  /** Offset in bytes from the start of the input. */
  end: number
  /** 1-based line. */
  line: number
  /** 0-based column, in bytes. */
  column: number
  /** In bytes. */
  length: number
}
```

## CompileResult

What every compile command answers with, before its own fields.

```ts
export interface CompileResult {
  errors: Message[]
  warnings: Message[]
}
```

## Language

The languages the engine has a lexer, a parser and a printer for.

```ts
export type Language = 'html' | 'css' | 'js'
```

## CompileSourceFileOptions

The source name a diagnostic blames. Defaults to "<stdin>".

```ts
export interface CompileSourceFileOptions {
  sourcefile?: string
}
```

## CompileOptions

The options every stage needs.
"language" is required on all three. There is no default, because inferring one from the source text is guessing - and a guess that is right 95% of the time is a guess that silently mis-lexes the other 5%.

```ts
export interface CompileOptions extends CompileSourceFileOptions {
  language: Language
}
```

## LexOptions

```ts
export interface LexOptions extends CompileOptions {
  /** CSS only. Whether the comment list is populated; off is also cheaper. */
  includeComments?: boolean
}
```

## ParseOptions

```ts
export interface ParseOptions extends CompileOptions, CssMinifyOptions {
  /** HTML only. Parse as a fragment: no wrappers and no doctype complaint. */
  fragment?: boolean
  /** HTML only. Count the imports the parser resolved. */
  collectImportRecords?: boolean
  /** HTML only. Count inline script and style elements. */
  collectInlineCode?: boolean
}
```

## PrintOptions

```ts
export interface PrintOptions extends CompileOptions, CssMinifyOptions {
  /** HTML only. Reindent rather than stream. Off by default. */
  pretty?: boolean
  /** HTML only. Minify the printed document. */
  minify?: boolean
  /** HTML only. Whether script elements run, which print can ask about. */
  scriptingEnabled?: boolean
  /** Escape non-ASCII. */
  asciiOnly?: boolean
}
```

## HtmlTokenKind

```ts
export type HtmlTokenKind =
  | 'comment'
```

## HtmlToken

One HTML token. For a tag, "value" is the tag name and "length" says how many attributes it carried.

```ts
export interface HtmlToken extends SourcePosition {
  kind: HtmlTokenKind
  value: string
}
```

## CssTokenKind

```ts
export type CssTokenKind =
  | 'endOfFile' | 'atKeyword' | 'unterminatedString' | 'badUrl' | 'cdc' | 'cdo'
```

## CssToken

One CSS token. Whitespace and comments are separate lists, not tokens.

```ts
export interface CssToken extends SourcePosition {
  kind: CssTokenKind
  value: string
}
```

## CssComment

One CSS comment. Its text is the whole comment, "/" fences and all.

```ts
export interface CssComment extends SourcePosition {
  text: string
}
```

## JsToken

One JS token. Its kind is the grammar's own name.

```ts
export interface JsToken extends SourcePosition {
  kind: string
  value: string
}
```

## HtmlNode

One node of the flattened tree an HTML parse returns. An element carries its tag name and the names of its attributes - not their values, which belong in the printed form. "depth" is how the flat list is a tree again.

```ts
export interface HtmlNode {
  type: string
  depth: number
  tag?: string
  attributes?: string[]
}
```

## CssMinifyOptions

```ts
export interface CssMinifyOptions {
  minifyWhitespace?: boolean
  minifySyntax?: boolean
  minifyIdentifiers?: boolean
}
```

## CssRule

One top-level rule of the parsed stylesheet.

```ts
export interface CssRule {
  kind: string
  start: number
}
```

## JsSymbol

A named thing in scope, as a JS parse saw it.

```ts
export interface JsSymbol {
  name: string
  useCount: number
}
```

## LexResult

What lexer answers with.
The token list is the language's own: HtmlToken, CssToken or JsToken. "kind" is a string rather than one of three unions because the three kinds are three unrelated vocabularies and a union of them would put a type error in every caller that narrowed.

```ts
export interface LexResult extends CompileResult {
  tokens: (HtmlToken | CssToken | JsToken)[]
  /** CSS only, and only when includeComments was set. */
  comments?: CssComment[]
  count: number
}
```

## ParseResult

What parse answers with.
The summary fields are the language's - nodes for HTML, rules for CSS, parts and symbols for JS - and are all optional, because which ones are present depends on which language was parsed. A caller reading result.rules has already said which language it asked for, and narrowing on language narrows these.

```ts
export interface ParseResult extends CompileResult {
  /** The handle to hand to print(). */
  ast: AstHandle
  /** HTML: the flattened tree. */
  nodes?: HtmlNode[]
  /** HTML. */
  nodeCount?: number
  /** HTML, only when collectImportRecords was set. */
  importRecords?: number
  /** HTML, only when collectInlineCode was set. */
  inlineScripts?: number
  /** HTML, only when collectInlineCode was set. */
  inlineStyles?: number
  /** CSS: the top-level rules. */
  rules?: CssRule[]
  /** CSS. */
  ruleCount?: number
  /** CSS. */
  symbolCount?: number
  /** JS: whether the parser finished without a syntax error. */
  ok?: boolean
  /** JS. */
  partCount?: number
  /** JS: the named things in scope. */
  symbols?: JsSymbol[]
}
```

## PrintResult

```ts
export interface PrintResult extends CompileResult {
  /** The printed source. */
  code: string
}
```

