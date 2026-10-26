---
title: Generated API reference
description: The guchho option and type surface, produced from index.d.ts.
---

<!-- GENERATED from package/npm/guchho/index.d.ts. Do not edit by hand. -->

# Generated API reference

This section transcribes `package/npm/guchho/index.d.ts`, the published
surface of the `guchho` package, exact as declared there. It is a frozen
snapshot: keep it in step with the declaration file when either changes.

Everything is asynchronous and there are no `…Sync` functions by design;
see the [API overview](../README.md). Classes live on
[their own page](../classes.md).

## Options

The options a caller hands to `build`, `transform`, `lint`, `pack`, `serve`, `watch` and `analyze`, plus the sections of a configuration file. [Full page](options.md).

- [`LintOptions`](options.md#lintoptions)
- [`CommonOptions`](options.md#commonoptions)
- [`BuildOptions`](options.md#buildoptions)
- [`BuildOptionsNested`](options.md#buildoptionsnested)
- [`BuildConfigSections`](options.md#buildconfigsections)
- [`TransformOptions`](options.md#transformoptions)
- [`PackOptions`](options.md#packoptions)
- [`ServeOptions`](options.md#serveoptions)
- [`WatchOptions`](options.md#watchoptions)

## Types & results

Diagnostics, files, the metafile, the loaders/formats/platforms vocabularies and the shapes a build answers with. [Full page](types.md).

- [`Location`](types.md#location)
- [`Note`](types.md#note)
- [`Message`](types.md#message)
- [`OutputFile`](types.md#outputfile)
- [`Metafile`](types.md#metafile)
- [`OutputSummary`](types.md#outputsummary)
- [`InputSummary`](types.md#inputsummary)
- [`AnalysisReport`](types.md#analysisreport)
- [`LintRuleLevel`](types.md#lintrulelevel)
- [`LintFileMessage`](types.md#lintfilemessage)
- [`LintMessage`](types.md#lintmessage)
- [`LintFileResult`](types.md#lintfileresult)
- [`LintResult`](types.md#lintresult)
- [`BuildResult`](types.md#buildresult)
- [`Loader`](types.md#loader)
- [`Format`](types.md#format)
- [`Platform`](types.md#platform)
- [`LogLevel`](types.md#loglevel)
- [`EntryPoint`](types.md#entrypoint)
- [`DevServer`](types.md#devserver)
- [`Watcher`](types.md#watcher)

## Plugin types

The hooks, the context every hook is called with, and the values they trade. [Full page](plugin-types.md).

- [`PluginOrder`](plugin-types.md#pluginorder)
- [`ResolveIdOptions`](plugin-types.md#resolveidoptions)
- [`ResolvedId`](plugin-types.md#resolvedid)
- [`LoadResult`](plugin-types.md#loadresult)
- [`SourceDescription`](plugin-types.md#sourcedescription)
- [`OutputChunk`](plugin-types.md#outputchunk)
- [`OutputAsset`](plugin-types.md#outputasset)
- [`OutputBundle`](plugin-types.md#outputbundle)
- [`NormalizedOutputOptions`](plugin-types.md#normalizedoutputoptions)
- [`EmittedFile`](plugin-types.md#emittedfile)
- [`GuchhoLog`](plugin-types.md#guchholog)
- [`PluginContext`](plugin-types.md#plugincontext)
- [`Plugin`](plugin-types.md#plugin)

## Compiler types

The tokens, nodes, options and results of `lexer`, `parse` and `print`. [Full page](compiler-types.md).

- [`CompileSource`](compiler-types.md#compilesource)
- [`AstHandle`](compiler-types.md#asthandle)
- [`SourcePosition`](compiler-types.md#sourceposition)
- [`CompileResult`](compiler-types.md#compileresult)
- [`Language`](compiler-types.md#language)
- [`CompileSourceFileOptions`](compiler-types.md#compilesourcefileoptions)
- [`CompileOptions`](compiler-types.md#compileoptions)
- [`LexOptions`](compiler-types.md#lexoptions)
- [`ParseOptions`](compiler-types.md#parseoptions)
- [`PrintOptions`](compiler-types.md#printoptions)
- [`HtmlTokenKind`](compiler-types.md#htmltokenkind)
- [`HtmlToken`](compiler-types.md#htmltoken)
- [`CssTokenKind`](compiler-types.md#csstokenkind)
- [`CssToken`](compiler-types.md#csstoken)
- [`CssComment`](compiler-types.md#csscomment)
- [`JsToken`](compiler-types.md#jstoken)
- [`HtmlNode`](compiler-types.md#htmlnode)
- [`CssMinifyOptions`](compiler-types.md#cssminifyoptions)
- [`CssRule`](compiler-types.md#cssrule)
- [`JsSymbol`](compiler-types.md#jssymbol)
- [`LexResult`](compiler-types.md#lexresult)
- [`ParseResult`](compiler-types.md#parseresult)
- [`PrintResult`](compiler-types.md#printresult)

