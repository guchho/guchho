---
title: Compiler API
description: The lex, parse and print functions of the guchho package.
---

# Compiler API

One function per stage, for each language the engine has a lexer, parser and
printer for. Each stage sends one request to the same service a build uses, so
the lexer, the parser and the printer are the engine's own.

The AST never crosses the wire: a parse answers with a numeric handle and a
structural summary, a transform turns one handle into another, and a print turns
a handle into text.

```ts
type Language = 'html' | 'css' | 'js'
```

## lexer

```ts
function lexer(input: CompileSource, options: LexOptions): Promise<LexResult>
```

`language` is required. There is no default, because inferring one from the
source text is guessing.

```ts
interface LexOptions extends CompileOptions {
  includeComments?: boolean  // CSS only; the comment list is populated
}
```

## parse

```ts
function parse(input: CompileSource, options: ParseOptions): Promise<ParseResult>
```

```ts
interface ParseOptions extends CompileOptions, CssMinifyOptions {
  fragment?: boolean              // HTML: parse as a fragment
  collectImportRecords?: boolean  // HTML: count resolved imports
  collectInlineCode?: boolean     // HTML: count inline script and style
}
```

The result carries a handle to hand to `print()`, plus language-specific
summaries — `nodes` for HTML, `rules` for CSS, `symbols` for JS.

## print

```ts
function print(ast: AstHandle, options: PrintOptions): Promise<PrintResult>
```

```ts
interface PrintOptions extends CompileOptions, CssMinifyOptions {
  pretty?: boolean             // HTML: reindent rather than stream
  minify?: boolean             // HTML: minify the printed document
  scriptingEnabled?: boolean   // HTML: whether script elements run
  asciiOnly?: boolean          // escape non-ASCII
}
```

Handles are numbered per language, so handle `0` names a valid tree in each of
the three. Passing a handle to the wrong language is refused before the engine
sees it.

## Options

All three stages share `CompileOptions`:

```ts
interface CompileSourceFileOptions {
  sourcefile?: string  // the name a diagnostic blames; defaults to "<stdin>"
}
interface CompileOptions extends CompileSourceFileOptions {
  language: Language
}
```

## See also

- [JavaScript API](README.md)
- [Generated reference](generated/README.md)
