---
title: JavaScript / TypeScript API
description: The programmatic surface of the guchho package.
---

# JavaScript / TypeScript API

```js
import { build, transform, pack, unpack, serve, watch } from 'guchho'
```

The package runs the same native engine as the command line, over a long-lived
service process. Everything is asynchronous; there are deliberately no `…Sync`
functions, because each would have to block the thread until a child process
answers.

## Functions

| Function | Page | Does |
| --- | --- | --- |
| `build(options)` | [#build](#build) | Builds a project. |
| `transform(code, options)` | [#transform](#transform) | Transforms one piece of source. |
| `pack(options)` | [#pack](#pack) | Writes an archive from files on disk. |
| `unpack(options)` | [#unpack](#unpack) | Extracts an archive to disk. |
| `context(options)` | [#context](#context) | Sets up a build that can be repeated. |
| `analyze(options)` | [#analyze](#analyze) | Analyses a build without writing it. |
| `analyzeMetafile(metafile)` | [#analyzemetafile](#analyzemetafile) | Pretty-prints a metafile. |
| `watch(options)` | [#watch](#watch) | Starts watching a build. |
| `serve(options)` | [#serve](#serve) | Starts a dev server on a build. |
| `stop()` | [#stop](#stop) | Ends the service process. |
| `version` | [#version](#version) | The package version, as a string. |

## The compiler API

| Function | Does |
| --- | --- |
| [`lexer(input, options)`](compiler.md#lexer) | Runs the engine's lexer. |
| [`parse(input, options)`](compiler.md#parse) | Runs the engine's parser, returning a handle. |
| [`print(ast, options)`](compiler.md#print) | Prints a parsed tree back to source. |

## Classes

| Class | Page |
| --- | --- |
| [`BuildFailure`](classes.md#buildfailure) | A build that failed. |
| [`ServiceError`](classes.md#serviceerror) | The service could not be reached or answered. |
| [`BuildContext`](classes.md#buildcontext) | A repeatable build. |
| [`Watcher`](classes.md#watcher) | A running watcher. |
| [`DevServer`](classes.md#devserver) | A running dev server. |

## build

```ts
function build(options: BuildOptions): Promise<BuildResult>
function build(options: BuildOptions[]): Promise<BuildResult[]>
```

An array builds each configuration and answers with one result per
configuration, in the order given. Throws a [`BuildFailure`](classes.md#buildfailure)
when a build failed.

## transform

```ts
function transform(
  code: string | Uint8Array,
  options?: TransformOptions
): Promise<TransformResult>
```

## pack

```ts
function pack(options: PackOptions): Promise<PackResult>
```

Everything the command line's `guchho pack` does, reached as a function: the
same entry naming, symlink policy and collision refusal - but a path and a size
come back as data rather than a line on a terminal.

## unpack

```ts
function unpack(options: UnpackOptions): Promise<UnpackResult>
```

The other half of `pack()`, and the same shape: the archive to read, where to
extract it, and the format if the extension and magic bytes are not enough. The
answer is the directory that was written and the files inside it. Absolute paths
and paths that would escape the destination are refused; symlinks, hardlinks and
special files are skipped with a warning, as they are when packing.

`outDir` defaults to the archive's name without its extension, `format` is
detected when it is left out, and `overwrite` and `stripComponents` match the
command line's flags.

`unpack` is native-only. The browser entry exports the name and throws: the
WebAssembly engine has no host filesystem to extract into.

## context

```ts
function context(options: BuildOptions): Promise<BuildContext>
```

Holds everything the engine learned reading the project, which makes the second
build cheaper than the first.

## analyze

```ts
function analyze(options?: BuildOptions | BuildOptionsNested): Promise<AnalysisReport>
```

Runs the build with `write: false` and leaves the project as it found it. The
numbers come from the metafile the engine already writes, so this is the
bundler's own graph rather than a second opinion from a scanner.

## analyzeMetafile

```ts
function analyzeMetafile(metafile: Metafile | string): Promise<string>
```

## watch

```ts
function watch(options?: BuildOptions): Promise<Watcher>
```

The opening build has already run by the time this resolves. The events are not
a push: a changed file is not reported until something asks for a rebuild. See
[the Watcher class](classes.md#watcher).

## serve

```ts
function serve(options?: BuildOptions): Promise<DevServer>
```

Resolves once the server is listening over a finished build. Uses the engine's
own HTTP server - the one `guchho serve` gets.

## stop

```ts
function stop(): Promise<void>
```

Await this before removing a build's output directory: while the process is
alive it has those files open, and on Windows removing them fails.

## version

```ts
const version: string
```

Read once from the manifest the package was installed as.

## Generated reference

The [generated reference](generated/README.md) is produced from
`package/npm/guchho/index.d.ts` and covers every option and type exactly.

## See also

- [Writing plugins](../plugins/README.md)
- [Configuration reference](../reference/config/README.md)
