---
title: Enumerations
description: The scoped enumerations of the guchho engine.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp. Do not edit by hand. -->

# Enumerations

The vocabulary a build is described in. Every decision a caller can make about a build is a scoped enumeration, so option structs stay a flat list of assignments.

In this page:

- [`SourceMap`](#sourcemap)
- [`SourcesContent`](#sourcescontent)
- [`LegalComments`](#legalcomments)
- [`JSX`](#jsx)
- [`Target`](#target)
- [`Loader`](#loader)
- [`Platform`](#platform)
- [`Format`](#format)
- [`Exports`](#exports)
- [`Packages`](#packages)
- [`EngineName`](#enginename)
- [`StderrColor`](#stderrcolor)
- [`LogLevel`](#loglevel)
- [`Charset`](#charset)
- [`TreeShaking`](#treeshaking)
- [`DropFlags`](#dropflags)
- [`MangleQuoted`](#manglequoted)
- [`AbsPathsFlags`](#abspathsflags)
- [`SideEffects`](#sideeffects)
- [`ResolveKind`](#resolvekind)
- [`MessageKind`](#messagekind)
- [`TargetParse`](#targetparse)

## SourceMap

How the source map for each emitted file is produced.
  kNone             - no map is generated.
  kInline           - the map is appended to the file as a trailing
                      "//# sourceMappingURL=data:..." comment, so a single
                      artefact is enough to debug in a browser.
  kLinked           - a "//# sourceMappingURL=<name>.map" comment plus a
                      separate ".map" file. The usual choice for a build
                      whose files are uploaded to a web server.
  kExternal         - the ".map" file is emitted with no linking comment, for
                      pipelines that upload maps to a symbol server.
  kInlineAndExternal - both of the above, so the artefact works on its own
                      and a symbol server still has a file to fetch.

```cpp
enum class SourceMap : uint8_t {
    kNone,
    kInline,
    kLinked,
    kExternal,
    kInlineAndExternal,
};
```

## SourcesContent

Whether the pre-transform source text is embedded in the map.
  kInclude - write a "sourcesContent" array, making the map self-contained:
             a debugger can show the original source without asking the file
             system for it.
  kExclude - omit it. Maps get much smaller, which pays off when the same
             sources are already reachable from wherever the debugger runs.

```cpp
enum class SourcesContent : uint8_t {
    kInclude,
    kExclude,
};
```

## LegalComments

What happens to licence banners found in the inputs ("/*! ... */" headers,
"//! ..." comments and similar markers that a rewriter must not delete).
  kDefault   - resolved to kEndOfFile when bundling, kInline otherwise.
  kNone      - drop the banners. Only legal when the licence allows it.
  kInline    - keep each banner next to the code it covers, so the text stays
               with its module after concatenation.
  kEndOfFile - collect every banner and emit them once, at the top of the
               output file.
  kLinked    - put the text in a "<name>.LEGAL.txt" file and leave a short
               pointer comment in the output.
  kExternal  - write the "<name>.LEGAL.txt" files with no pointer comment.

```cpp
enum class LegalComments : uint8_t {
    kDefault,
    kNone,
    kInline,
    kEndOfFile,
    kLinked,
    kExternal,
};
```

## JSX

How markup in .jsx-style sources is lowered.
  kTransform - emit classic createElement calls, using "jsx_factory" and
               "jsx_fragment" for the element and the fragment tag.
  kPreserve  - leave the markup untouched, so the file is copied through
               still containing JSX and must be processed again later.
  kAutomatic - emit imports of the runtime's jsx()/jsxs()/Fragment helpers,
               taken from "jsx_import_source". This is the modern default
               for new code bases because it needs no global pragma.

```cpp
enum class JSX : uint8_t {
    kTransform,
    kPreserve,
    kAutomatic,
};
```

## Target

The language level the output must stay within. Syntax or APIs that a
target does not support are lowered automatically; everything else is passed
through untouched, so this doubles as a promise about the emitted file.
  kDefault - no lowering based on a language level; only the "supported"
             overrides and the "platform" rules apply.
  kESNext  - everything the current parser understands, including syntax no
             engine ships yet.
  kES5..kES2025 - lower everything above the named edition.

```cpp
enum class Target : uint8_t {
    kDefault,
    kESNext,
    kES5,
    kES2015,
    kES2016,
    kES2017,
    kES2018,
    kES2019,
    kES2020,
    kES2021,
    kES2022,
    kES2023,
    kES2024,
    kES2025,
};
```

## Loader

How a file's bytes become a module. The loader is the single biggest lever
on what a file contributes to the graph: the same bytes under "kText" are an
inert string, under "kJSON" a real object literal that participates in
bundling, and under "kCopy" not a module at all but an opaque asset that is
emitted verbatim and referenced by URL.
  kNone       - not chosen yet; the engine picks one from the file extension,
                the nearest package metadata, and any per-path overrides in
                BuildOptions::loader.
  kBase64     - a data:...;base64 URL string.
  kBinary     - a typed-array view over the raw bytes, for real binary data.
  kCopy       - pass the file through untouched and expose its output URL.
  kCSS        - a stylesheet that participates in CSS bundling.
  kDataURL    - a data: URL for the file's contents.
  kDefault    - defer to the same guessing logic as kNone, chosen explicitly.
  kEmpty      - an empty module, useful for stubbing a file out entirely.
  kFile       - a file wrapper object exposing its path and bytes.
  kGlobalCSS  - a stylesheet whose rules land in the global scope rather
                than in a local scope, so it cannot be tree-shaken.
  kJS         - JavaScript.
  kJSON       - a JSON document, parsed into an object whose keys may be
                referenced as named imports.
  kJSX        - JavaScript containing markup.
  kLocalCSS   - a stylesheet whose rules stay local to the importing module,
                so unused rules can be dropped.
  kText       - a plain string, and nothing else.
  kTS         - a typed source: annotations are erased, the runtime is
                plain JavaScript.
  kTSX        - a typed source that also contains markup.

```cpp
enum class Loader : uint16_t {
    kNone,
    kBase64,
    kBinary,
    kCopy,
    kCSS,
    kDataURL,
    kDefault,
    kEmpty,
    kFile,
    kGlobalCSS,
    kJS,
    kJSON,
    kJSX,
    kLocalCSS,
    kText,
    kTS,
    kTSX,
};
```

## Platform

Which environment the output is destined for. The platform decides which
export conditions are active, the meaning of a bare specifier, the globals a
module may assume, and a long tail of smaller rules, so it is normally the
first thing a caller sets.
  kDefault - resolved to kBrowser.
  kBrowser - activates the "browser" export condition and the "browser" field
             of package metadata, so a package can ship a browser-specific
             file. The output is expected to run against a document.
  kNode    - activates the "node" export condition; bare specifiers are
             looked up in node_modules and the runtime's built-in modules are
             reachable.
  kNeutral - activates neither condition, so package metadata is used as
             written. Useful for a library that emits declarations only and
             commits to neither environment.

```cpp
enum class Platform : uint8_t {
    kDefault,
    kBrowser,
    kNode,
    kNeutral,
};
```

## Format

The module syntax of the emitted file. This is what makes one bundle
loadable in more than one kind of loader.
  kDefault   - resolved from the other options. A bundle becomes an IIFE for
               a browser build, a CommonJS module for a Node build and an ES
               module for a neutral one; a file that is only being converted
               keeps the import/export syntax of the input. Set it explicitly
               when the output has to be predictable across platforms.
  kIIFE      - wrap everything in a self-invoking function; the result is a
               script with no module system at all.
  kCommonJS  - emit module.exports and require(); works with any CommonJS
               host.
  kESModule  - emit import/export; the only format that supports "splitting".
  kUMD       - universal wrapper: one file that works as a script, as a
               CommonJS module and as an AMD module, and also publishes a
               global name.
  kAMD       - asynchronous module definition, with the "amd_*" options
               below.
  kSystem    - SystemJS, with the "system_null_setters" option below.

```cpp
enum class Format : uint8_t {
    kDefault,
    kIIFE,
    kCommonJS,
    kESModule,
    kUMD,
    kAMD,
    kSystem,
};
```

## Exports

Which of the entry point's exports a bundle format exposes as its public
value.
  kUnset   - nothing was asked for, so every format keeps the behaviour it
             has always had: a wrapper publishes a namespace object
             carrying every export name.
  kDefault - "--exports=default": the entry point's default export is the
             value. The CommonJS export, the IIFE and AMD module values
             and the UMD global are all that one default export,
             directly, rather than a namespace object with a "default"
             property on it. Valid for every format; the entry must have
             a default export unless it is a CommonJS script.
  kNamed   - "--exports=named": the namespace object, spelled the way
             spells it. Identical to kUnset.
  kNone    - "--exports=none": publish nothing; valid only when no entry
             point exports anything.
  kAuto    - "--exports=auto": let the entry's actual exports choose, the
             way "auto" does (none / default / named).

```cpp
enum class Exports : uint8_t {
    kUnset,
    kDefault,
    kNamed,
    kNone,
    kAuto,
};
```

## Packages

How imports that name a package are treated.
  kDefault  - resolved to kBundle.
  kBundle   - resolve the package and inline its code into the output.
  kExternal - keep the import statement and never touch the package.

```cpp
enum class Packages : uint8_t {
    kDefault,
    kBundle,
    kExternal,
};
```

## EngineName

The engines a build claims compatibility with. Each entry pairs an engine
with a version, and the union of their capabilities becomes the set of
features the output is allowed to use. It is the alternative to "target":
name the browsers and runtimes you actually test on, and the engine computes
the necessary lowering itself.

```cpp
enum class EngineName : uint8_t {
    kChrome,
    kDeno,
    kEdge,
    kFirefox,
    kHermes,
    kIE,
    kIOS,
    kNode,
    kOpera,
    kRhino,
    kSafari,
};
```

## StderrColor

Whether diagnostics written to stderr may contain ANSI colour escapes.
  kColorIfTerminal - colour only when stderr is a real terminal. This is the
                     right default for a library: redirected output stays
                     free of escape codes.
  kColorNever      - never colour.
  kColorAlways     - always colour, including into a pipe or a log file.

```cpp
enum class StderrColor : uint8_t {
    kColorIfTerminal,
    kColorNever,
    kColorAlways,
};
```

## LogLevel

How much Guchho says about its own progress. Anything quieter than the
configured level is dropped, and a message about one specific subsystem can
be turned up or down on its own through BuildOptions::log_override.
  kSilent   - print nothing at all.
  kVerbose  - trace every step, including internal caching decisions.
  kDebug   - trace phases and timings.
  kInfo    - summaries, timings and the "rebuild started/finished" lines.
  kWarning - only things that may indicate a mistake in the inputs.
  kError   - only failures.

```cpp
enum class LogLevel : uint8_t {
    kSilent,
    kVerbose,
    kDebug,
    kInfo,
    kWarning,
    kError,
};
```

## Charset

How non-ASCII characters are escaped in the output.
  kDefault - resolved to kASCII, which escapes every non-ASCII character as
             a "\uXXXX" sequence so the file survives any encoding chain
             between the build machine and the browser.
  kASCII    - always escape.
  kUTF8     - emit the characters as-is when the output charset allows it,
              which is smaller and easier to read.

```cpp
enum class Charset : uint8_t {
    kDefault,
    kASCII,
    kUTF8,
};
```

## TreeShaking

Whether unused exports and unreachable statements are removed.
  kDefault - resolved to enabled when bundling or when the output is an
             IIFE, because in both cases there is no way for a consumer to
             concatenate more code onto the end of the file and rely on the
             parts that were dropped; otherwise disabled, so that a file
             being rewritten still behaves like the original.
  kFalse   - never remove anything.
  kTrue    - always remove, which is also safe for a non-bundled file.

```cpp
enum class TreeShaking : uint8_t {
    kDefault,
    kFalse,
    kTrue,
};
```

## DropFlags

Which debugging constructs are deleted from the output. These are bit flags,
so they combine with operator| and are tested with Has():

  BuildOptions::drop = DropFlags::kDropConsole | DropFlags::kDropDebugger;

An empty value keeps everything.

```cpp
enum class DropFlags : uint8_t {
    kNone = 0,
    kDropConsole = 1 << 0,  // every console.* call is replaced with undefined
    kDropDebugger = 1 << 1, // debugger statements
};
```

## MangleQuoted

Whether property names may be shortened when they appear as string keys as
well as identifiers. This trades a bit of output size for output that no
longer round-trips through JSON, so it is opt-in.
  kFalse - only real property names are mangled.
  kTrue  - "obj.foo" and obj["foo"] get the same shortened name, which keeps
           lookups working but changes the strings in the output.

```cpp
enum class MangleQuoted : uint8_t {
    kFalse,
    kTrue,
};
```

## AbsPathsFlags

Which surfaces print fully absolute paths. Paths are normally shown relative
to the working directory, because absolute paths differ on every machine and
make build logs and caches impossible to share. These flags opt specific
surfaces out of that shortening, and they combine like DropFlags.

```cpp
enum class AbsPathsFlags : uint8_t {
    kNone = 0,
    kCodeAbsPath = 1 << 0,     // paths inside generated source, e.g. import specifiers
    kLogAbsPath = 1 << 1,      // paths inside diagnostics and log lines
    kMetafileAbsPath = 1 << 2, // paths inside the metafile JSON
};
```

## SideEffects

The answer an onResolve hook gives to "may this module be dropped when
nothing imports anything from it?".
  kTrue  - keep it, even if unused. The safe answer, and the default.
  kFalse - it is side-effect free, so drop it when nothing uses it. Only say
           this when loading the module genuinely has no observable effect,
           because getting it wrong deletes code that was doing work.

```cpp
enum class SideEffects : uint8_t {
    kTrue,
    kFalse,
};
```

## ResolveKind

What triggered a resolution request, so an onResolve hook can react
differently to an entry point than to a nested import.
  kNone              - no trigger recorded.
  kEntryPoint        - a path named in BuildOptions::entry_points.
  kJSImportStatement - a static import statement.
  kJSRequireCall     - a require() call.
  kJSDynamicImport   - a dynamic import() with a literal specifier.
  kJSRequireResolve  - require.resolve().
  kCSSImportRule     - an @import rule in a stylesheet.
  kCSSComposesFrom   - "composes: ... from ..." in a CSS module.
  kCSSURLToken       - a url() reference inside a stylesheet, which is
                       fetched as a file rather than resolved as a module.

```cpp
enum class ResolveKind : uint8_t {
    kNone,
    kEntryPoint,
    kJSImportStatement,
    kJSRequireCall,
    kJSDynamicImport,
    kJSRequireResolve,
    kCSSImportRule,
    kCSSComposesFrom,
    kCSSURLToken,
};
```

## MessageKind

The severity a batch of messages is being formatted as. It decides the
bracketed tag in front of the text ("[ERROR]" or "[WARNING]") and the colour
that tag and its icon are painted in.

```cpp
enum class MessageKind : uint8_t {
    kError,
    kWarning,
};
```

## TargetParse

Why a target string could not be read.

```cpp
enum class TargetParse : uint8_t {
    kOk,
    kUnknownName,    // nothing in the table has that name
    kMissingVersion, // an engine name with no version after it
};
```
