---
title: Getting started
description: Install Guchho and produce your first build.
---

# Getting started

This page goes from an empty directory to a built `dist` directory. Each
section introduces one command, shows what it writes, and explains what happened
underneath so the next command is a choice rather than a guess. You can follow
it in order, or skip to [Build](#build) if the project is already installed and
scaffolded.

## Install

Guchho ships as a platform-specific prebuilt binary behind the `guchho` npm
package. The postinstall step copies the correct binary for your platform into
place, so installing it does not require a C++ compiler or a CMake toolchain.

```bash
npm install -g guchho
```

Once the install finishes, ask the binary what it is rather than assuming it
landed on `PATH`:

```bash
guchho --version
guchho info
```

`--version` prints the installed package version. `guchho info` prints the
environment the engine is running in and the configuration it can see from the
current directory; it is the first command to reach for when a build behaves
differently on one machine than on another.

If you do not want a global install, run the package through `npx` from inside
the project. The rest of this page writes `npx guchho` so every command works in
either setup; a global install can drop the `npx` prefix.

Other distributions:

```bash
choco install guchho     # Windows (Chocolatey)

# From source (any OS with CMake >= 3.25 and a C++20 compiler)
bash scripts/build.sh --preset release-linux-x64
```

Building from source is the only route that needs a compiler and CMake. The
resulting binary is the same engine the npm package stages, so a project does
not need to change between the two.

See [Platform support](../resources/platform-support.md) for the full list.

## Create a project

`guchho init` writes into the current directory. It does not take a directory
argument, so create the directory and enter it first:

```bash
mkdir my-app
cd my-app
npx guchho init -y
```

With `-y` the command uses the default choices - an application in plain
JavaScript - and does not prompt. The generated tree is small on purpose:

```text
my-app/
├── guchho.config.js
└── src/
    ├── index.html
    ├── main.js
    └── style.css
```

The HTML document is the entry point:

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>My App</title>
    <link rel="stylesheet" href="./style.css">
  </head>
  <body>
    <h1>Hello from Guchho!</h1>
    <script type="module" src="./main.js"></script>
  </body>
</html>
```

The script imports the stylesheet and updates the heading:

```js
import './style.css';

const app = document.querySelector('h1');
if (app) {
  app.textContent = 'Hello from Guchho!';
}
```

The stylesheet contains the page's rules. The exact values do not matter here;
what matters is that all three files refer to one another by relative path, so
the project works wherever it is copied.

The config file is where the build is described:

```js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    target: 'esnext',
    minify: false,
    pretty: true,
    minifyHtml: false,
  },
};
```

Read those fields as a sentence about the output. `entry` is the file the build
starts from. `outdir` is where the result goes. `target: 'esnext'` says not to
lower syntax for an older runtime. `minify: false` keeps the generated
JavaScript readable, `pretty: true` indents the generated markup, and
`minifyHtml: false` keeps the markup readable as well. None of them is required
for the starter to build; they are written down so the first build is visible
rather than implicit.

The config is written as `guchho.config.js` when a JavaScript runtime is
available to evaluate it, or as `guchho.json` when none is found. Both forms
carry the same fields. If you want a different shape, choose a project **type**
and a **template**:

```bash
npx guchho init --type=lib --template=ts
npx guchho init --type=plugin --template=ts
npx guchho init --type=app --template=tsx
```

The type decides what the project is for (`app`, `lib` or `plugin`); the
template decides the language of the source file (`basic`, `ts`, `jsx` or
`tsx`). A library or plugin starter also writes a `package.json` and a
`README.md`, because those files are part of publishing a package. `--force`
allows generated files to be replaced; without it, an existing file is left
alone and nothing outside the generated set is ever removed.

## Develop

```bash
npx guchho dev
```

`dev` is build, watch and server in one command. It builds the project, serves
the output, watches every file the build read, and rebuilds when one changes.
The server listens on `localhost:3000` by default:

```text
http://localhost:3000
```

Open that address, edit `src/main.js`, and save. The change is picked up without
restarting the command, and an open page reloads when the rebuild finishes. For
example, change the text assignment to:

```js
if (app) {
  app.textContent = 'Hello from your first Guchho build!';
}
```

The next rebuild writes the new JavaScript and pushes a reload to the page. A
failed rebuild is not hidden behind the previous output: the server answers with
a `503` and the build's messages, so a syntax error is visible immediately.

The common flags change where the server listens rather than what it builds:

```bash
npx guchho dev --port=8080 --open
npx guchho dev --host=0.0.0.0
npx guchho dev --port=8080 --outdir=build
```

`--port` chooses the port, `--open` opens a browser, and `--host=0.0.0.0` makes
the server reachable from another machine on the network. `dev` also accepts the
build flags from [`guchho build`](../reference/cli/build.md), which is how you
develop with minification or source maps if the production build uses them.

The three long-running commands are easy to confuse, so keep the difference in
mind:

| Command | Serves HTTP | Rebuilds on change | Use it when |
| --- | --- | --- | --- |
| `guchho dev` | yes | yes, and reloads the page | developing an app in a browser. |
| `guchho watch` | no | yes | the consumer is a test runner or another process. |
| `guchho serve` | yes | no | you want to inspect an already-built directory. |

## Build

```bash
npx guchho build
```

The build finds the nearest config file, prints the file it is using, and writes
the result to the configured `outdir`. Flags on the command line win over the
config file, and the config file wins over built-in defaults, so the command
above and the command below can describe the same project:

```bash
npx guchho build src/index.html --outdir=dist --minify
```

Here `--outdir` names the output directory and `--minify` compresses the output.
A build without a config file uses the same engine and the same defaults; it
simply takes every setting from the command line.

When the entry point is JavaScript rather than HTML, the module format and the
source map are named directly:

```bash
npx guchho build src/main.js --outdir=dist --format=esm --minify --sourcemap
```

`--format=esm` emits ES modules, `--minify` compresses the output, and
`--sourcemap` writes linked source maps beside it. An HTML entry point chooses
the format its document needs, so a `--format` flag has nothing to decide there.

### What kind of entry point do you have?

The entry point decides how much of the graph is built:

| Entry | What Guchho reads | What it writes |
| --- | --- | --- |
| an HTML document | the document and every local `script`, `link`, image and asset reference it contains | the document plus the files behind those references |
| a JavaScript or TypeScript file | that file, plus its imports when bundling is on | the script, or a bundle when `--bundle` is used |
| a CSS file | the stylesheet, its `@import`s and its `url(...)` references | the stylesheet and any assets it points at |

An HTML entry point enables bundling when you do not say otherwise, because a
document describes a whole application. A JavaScript entry point does not. This
build transforms one module and leaves its imports alone:

```bash
npx guchho build src/main.js --outdir=dist
```

This one follows the imports and emits a bundle:

```bash
npx guchho build src/main.js --outdir=dist --bundle
```

A CSS entry point works without any JavaScript:

```bash
npx guchho build src/style.css --outdir=dist
```

That is useful when the output is consumed by another tool, or when only the
stylesheet needs processing. It is the reason Guchho can replace a single-purpose
CSS command as well as a full application bundler.

The starter produces a document, a script and a stylesheet. The names carry a
content hash so a changed file gets a new URL:

```text
dist/
├── assets/
│   ├── main-CV5SQGR2.js
│   └── style-EGOBBMO7.css
└── index.html
```

The hashes are not random. They are derived from the content of each output, so
an unchanged file keeps its name and a changed file gets a different one. The
HTML is rewritten to point at the emitted names:

```html
<link rel="stylesheet" href="./assets/style-EGOBBMO7.css">
<script type="module" src="./assets/main-CV5SQGR2.js" crossorigin=""></script>
```

That rewrite is why an HTML entry point works without a config file. The build
does not copy the document and leave its original `./style.css` reference
pointing at a file that was never emitted; it follows the reference, processes
the target, and writes the URL the browser must use.

There is one graph, not a list of HTML assets beside a list of JavaScript
imports. An image referenced from the document, a stylesheet imported by the
script and a font referenced from that stylesheet all go through the same
resolver and the same loader map. That is why changing the `.svg` loader affects
an SVG in the HTML just as it affects one imported from JavaScript.

Assets referenced from HTML therefore follow the same rules as any other asset:
the extension chooses the loader, `assetNames` chooses the output pattern, and
the URL is rewritten to the result. A `public` directory is a convention, not a
special case; if the document points at a file, that file is in the graph.

The build also follows references it finds inside JavaScript and CSS, not only
the ones written in the document. An `import './style.css'` in the script is
enough for the stylesheet to become part of the build even if the document does
not link it. Assets referenced from those files are copied or inlined according
to their loader.

A few everyday variations are worth knowing before reaching for a config file:

```bash
# An older language target, with source maps embedded in the output.
npx guchho build src/main.js --target=es2020 --sourcemap=inline

# Replace a compile-time identifier.
npx guchho build src/index.html --define:__DEV__=false

# Treat SVG files as data URLs instead of separate assets.
npx guchho build src/index.html --loader:.svg=dataurl

# Print a size report from the build's own dependency graph.
npx guchho build src/index.html --analyze
```

`--target` names the language level the output must stay within. `--sourcemap`
accepts `inline`, `external`, `linked` or `both`; a bare `--sourcemap` means a
linked map. `--define:` substitutes the identifier that follows it, which is a
cheap way to remove development-only branches. `--loader:` takes an extension
and a loader name, and `--analyze` prints a size report read from the build's
own dependency graph.

For a reusable library, the settings point away from the browser defaults:

```bash
npx guchho build src/index.ts \
  --format=esm \
  --platform=neutral \
  --bundle \
  --external:react \
  --outdir=dist
```

`--format=esm` emits modules a bundler can consume, `--platform=neutral` keeps
the output from assuming browser or Node globals, and `--external:react` leaves
React as an import instead of inlining it. `--external` only means something
when bundling, because without a graph there is nothing to leave out.

## Configure the build

Once the command line grows past a few flags, move the settings into the config
file. This example names the common groups and what each one changes:

```js
// guchho.config.js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    format: 'esm',
    target: 'es2020',
    bundle: true,
    splitting: true,
    minify: true,
    sourcemap: 'linked',
    define: {
      __DEV__: 'false',
      'process.env.NODE_ENV': '"production"',
    },
    loader: {
      '.svg': 'dataurl',
      '.png': 'file',
    },
    external: ['react', 'react-dom'],
    alias: {
      '@': './src',
    },
    entryNames: '[name]-[hash]',
    chunkNames: 'chunks/[name]-[hash]',
    assetNames: 'assets/[name]-[hash]',
  },
};
```

`entry`, `outdir`, `format`, `target` and `minify` are the same options the
flags spell. `bundle` inlines the module graph, and `splitting` extracts code
shared by several entry points into chunks that the entries import; splitting
requires ES module output. `define` values are the JavaScript expression to
substitute, written as a string, which is why the string value carries its own
quotes. `loader` is a map from extension to loader, so `.svg` becomes a data URL
while `.png` stays a separate file. `external` and `alias` act during
resolution and therefore only mean something when bundling.

The three naming templates use the `[dir]`, `[name]`, `[hash]` and `[ext]`
placeholders. They decide where each kind of output goes and whether its name
carries a content hash; every URL that points at a renamed file is rewritten to
match.

The same project can be described in JSON when a JavaScript runtime is not
available:

```json
{
  "build": {
    "entry": "src/index.html",
    "outdir": "dist",
    "format": "esm",
    "target": "es2020",
    "bundle": true,
    "minify": true,
    "sourcemap": "linked",
    "define": {
      "__DEV__": "false",
      "process.env.NODE_ENV": "\"production\""
    }
  }
}
```

Both formats are read by the same resolver and follow the same precedence:
command-line flag, then config file, then built-in default. The native config
parser understands the `build` fields listed in the
[configuration reference](../reference/config/build.md); an option that only
the JavaScript API or a flag understands is reported as a warning rather than
silently ignored, so a typo does not become a mystery.

A config file may describe more than one build. A root object produces one
build; a root array produces one build per element, each resolved from the same
defaults:

```js
export default [
  {
    build: {
      entry: 'src/app.js',
      outdir: 'dist/app',
      format: 'esm',
      bundle: true,
      minify: true,
    },
  },
  {
    build: {
      entry: 'src/library.js',
      outdir: 'dist/library',
      format: 'esm',
      bundle: true,
      external: ['react'],
    },
  },
];
```

This is useful when one repository publishes an application and a package from
the same source tree. Each element is a complete build, so the application can
be minified while the library keeps its dependencies external.

## Use the JavaScript API

The npm package exposes the same engine as a set of asynchronous functions. Use
the API when the build is part of a larger Node program - a release script, a
test harness, or a tool that wants the result as data:

```js
import { BuildFailure, build } from 'guchho';

try {
  const result = await build({
    entrypoints: ['src/index.html'],
    outdir: 'dist',
    bundle: true,
    minify: true,
    sourcemap: 'linked',
    metafile: true,
  });

  console.log(`Built ${result.outputs.length} files in ${result.duration} ms`);

  for (const output of result.outputs) {
    console.log(`${output.path} (${output.size} bytes)`);
  }

  for (const warning of result.warnings) {
    console.warn(`warning: ${warning.text}`);
  }
} catch (error) {
  if (!(error instanceof BuildFailure)) throw error;

  for (const message of error.errors) {
    const location = message.location;
    if (location) {
      console.error(
        `${location.file}:${location.line}:${location.column}: ${message.text}`,
      );
    } else {
      console.error(message.text);
    }
  }

  process.exitCode = 1;
}
```

The API spells the entry option `entrypoints`, while a config file uses the
singular `entry`; both reach the same setting. A successful build resolves with
`outputs`, `warnings`, `duration` and, when asked for, `metafile`. Warnings do
not reject the promise; a build with warnings is still a successful build. A
build that fails rejects with `BuildFailure`, whose `errors` array carries the
same diagnostics the command line prints, each with an optional location.

Passing `write: false` keeps the bytes in memory and returns them on
`outputFiles` instead of writing a directory, which is useful for a tool that
only wants to inspect or forward the result.

For one piece of source, use `transform` rather than a build:

```js
import { transform } from 'guchho';

const { code, warnings } = await transform(
  'const add = (a: number, b: number): number => a + b;',
  {
    loader: 'ts',
    target: 'es2020',
    minify: true,
  },
);

console.log(code);

for (const warning of warnings) {
  console.warn(warning.text);
}
```

`transform` has no entry points and does not walk the file system. It takes one
string or byte array, applies the loader and options, and answers with `code`,
`map`, `errors` and `warnings`. That makes it the right call for an editor
integration, a pre-commit hook, or a pipeline that already resolved its own
modules.

The same engine can also keep a build warm or serve it:

```js
import { serve } from 'guchho';

const server = await serve({
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
  },
  server: {
    host: 'localhost',
    port: 8080,
  },
});

console.log(`Listening on ${server.url}`);

process.on('SIGINT', async () => {
  await server.close();
  process.exit(0);
});
```

`serve()` resolves after the opening build has finished and the server is
accepting requests, so `server.url` is ready to use. `server.close()` stops the
server and releases the warm build behind it. For incremental rebuilds without a
server, [`context()`](../api/README.md#context) returns a `BuildContext` whose
`rebuild()` reuses what the first build learned; for change-driven rebuilds,
[`watch()`](../api/README.md#watch) returns a `Watcher` with `buildEnd` events.

A context is the lower-level version of the same idea. Use it when you want to
decide exactly when a build happens, rather than have a watcher or server drive
it:

```js
import { context } from 'guchho';

const ctx = await context({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  minify: true,
});

try {
  const first = await ctx.rebuild();
  console.log(`first build: ${first.outputs.length} files`);

  const second = await ctx.rebuild();
  console.log(`second build: ${second.outputs.length} files`);
} finally {
  await ctx.dispose();
}
```

`context()` prepares the build but does not write output by itself. Each
`rebuild()` runs a build and returns a result, reusing what the context already
resolved and parsed. `dispose()` is what releases the files the engine holds
open; call it even when a rebuild failed.

## Check, preview and package

Linting is a separate step from building, because a lint finding is reported as
data rather than a failed build:

```bash
npx guchho lint
npx guchho lint src/main.js --rule no-console:error
npx guchho lint --fix
```

With no paths, `lint` checks the project's configured source files. `--rule`
overrides one rule for the run, and `--fix` applies the safe fixes the rule set
offers. See [`guchho lint`](../reference/cli/lint.md) for the rule list,
configuration file and exit codes.

After a build, serve the output to check it exactly as a browser will receive
it:

```bash
npx guchho serve
npx guchho serve dist --port=8080 --open
```

`serve` is a static file server over an already-built directory. It does not
rebuild, so a change made after the build is not reflected; use `dev` for that.

Remove generated output with `clean`. It reads the same `outdir` resolution as a
build, so a config file and a command line agree on what is removed:

```bash
npx guchho clean --dry-run
npx guchho clean
```

Start with `--dry-run` in an unfamiliar project. It prints what would be removed
without deleting anything. Unlike a build, there is no diagnostic to inspect
afterwards if the directory turns out to be the wrong one.

Package the result when it is ready to move:

```bash
npx guchho pack dist -o release.zip
npx guchho pack dist --format=tar.gz -o release.tar.gz --level=9
npx guchho unpack release.zip -o release
```

`pack` stores every input under its own last path component, so `pack dist`
stores the tree under `dist/`. `unpack` refuses absolute paths and paths that
would escape the destination, and skips symlinks with a warning rather than
following them. Use `--allow-overwrite` on `pack` and `--overwrite` on `unpack`
when replacing an archive or an extraction directory is intended.

Add the common commands to `package.json` so they are one word away:

```json
{
  "scripts": {
    "dev": "guchho dev",
    "build": "guchho build",
    "preview": "guchho serve",
    "clean": "guchho clean",
    "lint": "guchho lint"
  }
}
```

## When the first build fails

Most first-run problems fall into a small number of categories.

- **"No entry points specified".** A build needs at least one entry unless the
  current directory has a config file that names one. Pass a path
  (`guchho build src/index.html`) or add `build.entry` to the config.
- **The config is not being found.** Guchho searches upward from the current
  directory and uses the nearest config. Run `guchho info` to see the directory
  and config it resolved. Starting the command in a subdirectory is fine if the
  config is above it; starting it in an unrelated directory is not.
- **A JavaScript config needs a JavaScript runtime.** Evaluating
  `guchho.config.js` requires Node (or the runtime named by `GUCHHO_NODE_BIN`).
  On a machine without one, write the same settings as `guchho.json` or pass
  them as flags.
- **A config option is reported as ignored.** The native config parser reads the
  subset documented in the [configuration reference](../reference/config/build.md).
  Options that belong only to the JavaScript API or a command-line flag must be
  given there instead. A warning is better than a setting that looks applied but
  is not.
- **The dev server returns `404` for every path.** Check the output directory
  and the URL. `guchho dev` serves the build; `guchho serve dist` serves the
  directory you named. A page at `/app` is not emitted just because the router
  understands it.
- **The page loads but the console shows missing files.** Open the network panel
  and compare the requested path with the output tree. A `publicPath`, a
  hand-written absolute URL or an HTML file that was never an entry point can
  all produce a path that the server does not have.

A failed build is not a partially working build that you should deploy. Read the
first diagnostic, fix it, and build again. Later errors are often consequences
of the first one.

## Next

- [Configuration](configuration.md) - configure a build once.
- [HTML pipeline](html-pipeline.md) - what an HTML entry point really does.
- [CLI overview](cli-overview.md) - every command.
- [JavaScript API](../api/README.md) - use the engine from Node.
- [Build a single-page app](../recipes/build-a-single-page-app.md) - an HTML
  entry from start to deployable output.
- [Build a library](../recipes/build-a-library.md) - publish ESM and CommonJS.
- [Deployment](deployment.md) - hashing, caching, source maps and hosting.
