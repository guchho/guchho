---
title: Glossary
description: The vocabulary used throughout the Guchho documentation.
---

# Glossary

Terms are defined as Guchho uses them; where a word is overloaded in the wider
ecosystem, the definition here is the one the [reference](../reference/README.md)
relies on.

**Asset** - a file the build copies to the output rather than compiles:
images, fonts, and anything without a loader that transforms it.

**Bundle** - to follow every import from the entry points and emit the result as
one or a few files. Off by default; an HTML entry point switches it on when it
was not specified either way.

**Code splitting** - emitting shared or lazily-reached modules as separate
chunks rather than inlining everything into one file. Controlled by `splitting`.

**Define** - a build-time substitution of an identifier for literal text, such
as `process.env.NODE_ENV` for `"production"`. See
[inject configuration](../recipes/inject-config-at-build-time.md).

**Entry point** - a file the build starts from. Everything reachable from an
entry point is part of the build; everything else is not.

**External** - a package left as an import in the output instead of being
bundled in. Used for libraries and for anything the consumer provides.

**Format** - the module convention of the output: `iife`, `cjs`, `esm`, `umd`,
`amd` or `system`.

**Loader** - the compiler used for a file type. A loader turns bytes of a given
extension into JavaScript (or CSS, into the stylesheet graph).

**Mangle** - to rename identifiers or property names to shorter ones. Identifier
mangling is part of minification; property mangling is opt-in and scoped by
pattern.

**Metafile** - a JSON description of a build: its outputs, the inputs behind
each, and their sizes. Used for size analysis.

**Minify** - to reduce output size by whitespace removal, syntax rewriting and
identifier renaming.

**Output file** - a file the build produces, in memory or on disk, depending on
whether `write` was requested.

**Platform** - the runtime whose conventions the output follows: `browser`,
`node` or `neutral`.

**Plugin** - a named unit of build customisation. See the
[plugin documentation](../plugins/README.md).

**Source map** - a file that maps generated output back to original sources so a
debugger can show the code you wrote.

**Target** - the language version or engines the output must run on, such as
`es2020` or `chrome80`.

**Transform** - a single-file compile with no graph: one input, one output. See
the [transform recipe](../recipes/transform-single-files.md).

**Tree shaking** - removing exports and statements that no reachable code uses.
`treeShaking` defaults to `"default"`, which enables it when bundling or when
the output is an IIFE; a plain non-bundled ESM transform leaves it off because
a consumer may still import the unused names.

**Watch** - rebuilding automatically when a file the build read changes.
