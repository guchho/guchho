---
title: Why Guchho
description: The design choices behind Guchho and how they compare to other tools.
---

# Why Guchho

Guchho is a build tool for the web that treats an HTML document as the unit of
work. It reads the document, follows the scripts, stylesheets, images and other
assets the document references, and writes an output directory in which every
URL points at a file that actually exists.

That sounds like a small difference from a JavaScript bundler. It changes what
you configure, what the tool must understand, and what a build can notice. This
page explains those choices and shows what they look like in a real project.

## A build you can read

Suppose the project looks like this:

```text
shop/
├── index.html
├── src/
│   ├── main.js
│   ├── cart.js
│   └── style.css
└── assets/
    └── logo.svg
```

`index.html` is the entry point:

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <link rel="stylesheet" href="./src/style.css">
  </head>
  <body>
    <img src="./assets/logo.svg" alt="Shop">
    <script type="module" src="./src/main.js"></script>
  </body>
</html>
```

`main.js` imports another module:

```js
import { total } from './cart.js';

document.querySelector('h1').textContent = `Total: ${total()}`;
```

One command builds the whole thing:

```bash
npx guchho build index.html --outdir=dist --minify --sourcemap=linked
```

The output is a complete directory, not a pile of files that still need to be
referenced by hand:

```text
dist/
├── assets/
│   ├── logo-3F9A12.svg
│   ├── main-7C2D4E.js
│   ├── main-7C2D4E.js.map
│   ├── style-91B0AF.css
│   └── style-91B0AF.css.map
└── index.html
```

The generated HTML points at the emitted names:

```html
<link rel="stylesheet" href="./assets/style-91B0AF.css">
<img src="./assets/logo-3F9A12.svg" alt="Shop">
<script type="module" src="./assets/main-7C2D4E.js" crossorigin=""></script>
```

The hashes are derived from the content. An unchanged file keeps its name; a
changed file gets a new one, so a browser can cache the old copy forever without
serving stale bytes.

There is one graph behind that output. The stylesheet in the document, the
script in the document, the import inside the script and the image in the markup
all go through the same resolver and the same loader map. That is why a rule
that applies to an image imported from JavaScript also applies to an image
written directly in the HTML.

## Native, not transpiled

The engine is a single C++20 binary. Parsing, linking, minification and tree
shaking happen in native code, so a build does not spend its time in a garbage
collector or walking `node_modules`.

This is a design choice with visible consequences:

- **No runtime toolchain.** Installing a prebuilt package does not require a C++
  compiler, CMake or a Node toolchain on the build machine. The postinstall step
  places the binary for the current platform.
- **One long-lived process can serve many builds.** The JavaScript API talks to
  a service process and can keep it warm. `context()` and `serve()` reuse what
  the first build learned instead of starting from an empty cache each time.
- **The CLI is not the only entry point.** `guchho build` is a thin shim over
  the same service protocol the API uses, so the command line and an embedding
  program take the same path through the engine.
- **The work is predictable.** There is no plugin process to start, no worker
  pool to size and no separate CSS toolchain to configure. The pipeline is the
  same whether it is run on a laptop or in CI.

You can inspect the environment and the configuration the engine sees:

```bash
guchho --version
guchho info
```

`guchho info` is the first command to reach for when a build behaves differently
on one machine than on another. It prints the runtime, the resolved project
directory and the configuration file in effect.

## HTML-first

Guchho starts at your HTML entry, follows every `<script>`, `<link>`, `import`
and asset reference, and produces a complete output directory. You describe the
document; Guchho works out the graph.

That is different from starting at JavaScript and hoping the rest of the page
follows. A document can reference a stylesheet that imports a font, an image
that is only mentioned in markup, or a module that is only reachable through a
`<script type="module">`. All of those are part of the build because the
document says they are.

The important word is *follow*. A reference is not copied as-is when its target
is processed. It is resolved, loaded, transformed, emitted under its output
name, and then the referring URL is rewritten. If the target changes its name
because of a content hash, the reference changes with it.

This is also why a `public` directory is a convention rather than a special
case. A file is in the graph because something references it, not because it
sits in a directory with a particular name. If the document points at a file,
that file is in the graph; if nothing points at it, Guchho has no reason to copy
it.

### HTML is an entry point, not a template

Guchho does not run your HTML through a framework-specific template compiler.
It parses the document, understands the resource references the browser would
follow, processes the local files behind them, and prints the document back out.
That keeps the HTML pipeline useful for plain sites as well as applications, and
it means a document with no JavaScript at all is still a complete build:

```bash
npx guchho build src/index.html --outdir=dist
```

## One tool, not five

Lexer, parser, transformer, printer, minifier, linker, resolver, watcher and dev
server are all in the same binary. There is no plugin process, no worker pool,
and no separate CSS toolchain to configure.

The practical result is that a normal project does not need a chain of tools
with a configuration file for each stage:

| Job | Guchho command or option | What it replaces in a typical setup |
| --- | --- | --- |
| Parse and transform | `guchho build`, `guchho transform` | a language transpiler |
| Bundle and split | `--bundle`, `--splitting` | a JavaScript bundler |
| Minify | `--minify`, `--minify-*` | a separate JS minifier |
| Process CSS | a `.css` import or entry point | a CSS bundler and minifier |
| Generate source maps | `--sourcemap` | map plugins for every stage |
| Serve and rebuild | `guchho dev`, `guchho watch` | a watcher plus a dev server |
| Package a release | `guchho pack` | a shell `zip` or `tar` step |
| Check source | `guchho lint` | a separate linter |

You can still use other tools around Guchho. The point is that the common path
does not require you to assemble them yourself, and a flag on one command does
not have to be repeated in four configuration files.

## A real API

The CLI is a thin shim over a long-lived service protocol. Everything
`guchho build` does is also callable from JavaScript - including the compiler
stages (`lexer`, `parse`, `print`) - so the engine embeds in your own tools.

A build as a library call:

```js
import { build, BuildFailure } from 'guchho';

try {
  const result = await build({
    entrypoints: ['src/index.html'],
    outdir: 'dist',
    bundle: true,
    minify: true,
    sourcemap: 'linked',
  });

  console.log(`wrote ${result.outputs.length} files in ${result.duration} ms`);
} catch (error) {
  if (!(error instanceof BuildFailure)) throw error;

  for (const message of error.errors) {
    const at = message.location;
    console.error(
      at
        ? `${at.file}:${at.line}:${at.column}: ${message.text}`
        : message.text,
    );
  }
}
```

For one piece of source, use `transform` rather than a build:

```js
import { transform } from 'guchho';

const { code, map } = await transform(
  'const add = (a: number, b: number): number => a + b;',
  {
    loader: 'ts',
    target: 'es2020',
    minify: true,
    sourcemap: true,
    sourcefile: 'src/add.ts',
  },
);

console.log(code);
console.log(map);
```

A build can be kept warm so that repeated builds reuse parsed files and resolved
modules:

```js
import { context } from 'guchho';

const ctx = await context({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
});

try {
  await ctx.rebuild();
  await ctx.rebuild();
} finally {
  await ctx.dispose();
}
```

And the parser stages are exported as their own functions when a tool wants to
look at a tree rather than emit a file:

```js
import { lexer, parse, print } from 'guchho';

const tokens = await lexer('const answer = 42;', { language: 'js' });
const tree = await parse('const answer = 42;', { language: 'js' });
const source = await print(tree.ast, { language: 'js' });

console.log(tokens.count, tree.partCount, source.code);
```

The AST does not cross the process boundary. `parse` answers with a handle and a
structural summary; `print` takes the handle back. That keeps the engine's own
parser and printer authoritative instead of growing a second, partial
implementation of them in JavaScript.

## Failure is data

Build failures are reported as structured diagnostics rather than as ad-hoc
errors. A failed build rejects with `BuildFailure`, whose `errors` array carries
the same messages the command line prints; a successful build returns its
warnings on the result. Messages can be filtered, reworded or forwarded to an
editor without parsing terminal text. Apart from `BuildFailure`, exceptions are
reserved for programming mistakes.

That distinction is deliberate:

- A syntax error in the source is **data**. It has a message, a file, a line, a
  column and often a note explaining the fix.
- A missing import is **data**. The build did its job by reporting that the
  graph could not be completed.
- A typo in an option name is **data** too. The option is reported as unknown
  rather than silently ignored.
- Passing a number where a string is required is a **programming mistake**, and
  the API throws a `TypeError`.

Warnings do not fail a build. A successful build can still carry warnings, and
the result tells you about both:

```js
const result = await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
});

for (const warning of result.warnings) {
  console.warn(`warning: ${warning.text}`);
}
```

A build that cannot produce output rejects with `BuildFailure`. Its `errors`
array carries the same diagnostics the command line prints:

```js
import { BuildFailure } from 'guchho';

try {
  await build({ entrypoints: ['src/index.html'], outdir: 'dist' });
} catch (error) {
  if (!(error instanceof BuildFailure)) throw error;

  for (const message of error.errors) {
    console.error(message.text);
  }

  process.exitCode = 1;
}
```

On the command line the same rule applies. Warnings are printed to stderr and do
not change a successful exit code; errors do.

## The output is the contract

A build describes an output directory. Guchho tries to make that description
complete rather than approximate:

- Every generated URL is rewritten to the file that was actually emitted.
- Every output name can carry a content hash, and the hash changes only when the
  content does.
- A source map can be inlined, linked or kept external, and the choice applies
  to every emitted file.
- A target names the language level the output stays within, so the result is a
  promise about what syntax the browser will see.
- `guchho pack` can turn the finished directory into a byte-reproducible
  archive, so a release is a function of its inputs rather than of the machine
  that built it.

That is why the tool has fewer hidden stages than a chain of separate tools: the
fewer places a file can change without the output knowing about it, the easier
it is to say what a build produced.

## What Guchho deliberately does not do

Knowing the boundaries is as useful as knowing the features.

- **It does not ship polyfills.** A target lowers syntax it knows how to lower.
  If the output uses a runtime API the target browser does not have, that API
  still needs a polyfill or a different implementation.
- **It does not guess a bad target.** `firefox115` is understood;
  `firefox-latest` is an error rather than a silent fallback to the newest
  table entry.
- **It does not archive files you did not name.** `guchho pack` walks the paths
  on the command line. It does not read `.gitignore`, and it does not infer
  `dist` from the configuration.
- **It does not replace a framework compiler.** A `.vue` or `.svelte` file needs
  the tool that understands that format. Guchho can still process the HTML, CSS
  and JavaScript those tools produce.
- **It does not provide synchronous entry points.** `buildSync` and
  `transformSync` are deliberately absent, because the service protocol is
  asynchronous by design.

## Where Guchho fits

- **A library or app** that wants one binary to parse, bundle, minify and serve.
- **A tool author** who wants the parser, printer and minifier as callable
  stages.
- **An editor integration** that needs to feed an unsaved buffer into the graph.
- **A release pipeline** that wants one command to produce a directory that can
  be served, uploaded or archived.

If the project already has a framework-specific build that owns the graph, use
Guchho for the parts it does well: processing the HTML document, bundling the
JavaScript entry points, lowering syntax, generating source maps and packaging
the result.

## See also

- [Getting started](getting-started.md) - go from an empty directory to a build.
- [Core concepts](core-concepts.md) - entries, graphs, loaders and outputs.
- [HTML pipeline](html-pipeline.md) - what an HTML entry point really does.
- [Bundling & splitting](bundling-and-splitting.md) - formats and chunks.
- [Minification](minification.md) - the switches that change the bytes.
- [JavaScript API](../api/README.md) - the engine as a library.
