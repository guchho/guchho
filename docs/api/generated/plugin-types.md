---
title: Plugin types
description: The types of the plugin API of the guchho package.
---

<!-- GENERATED from package/npm/guchho/index.d.ts. Do not edit by hand. -->

# Plugin types

The hooks, the context every hook is called with, and the values they trade.

In this page:

- [`PluginOrder`](#pluginorder)
- [`ResolveIdOptions`](#resolveidoptions)
- [`ResolvedId`](#resolvedid)
- [`LoadResult`](#loadresult)
- [`SourceDescription`](#sourcedescription)
- [`OutputChunk`](#outputchunk)
- [`OutputAsset`](#outputasset)
- [`OutputBundle`](#outputbundle)
- [`NormalizedOutputOptions`](#normalizedoutputoptions)
- [`EmittedFile`](#emittedfile)
- [`GuchhoLog`](#guchholog)
- [`PluginContext`](#plugincontext)
- [`Plugin`](#plugin)

## PluginOrder

The order a plugin runs in relative to the rest.
`"pre"` plugins run before the default order, `"post"` after it, and the relative order of plugins with the same `order` is the order they appear in `plugins`. This is `order`, and it is honoured per hook.

```ts
export type PluginOrder = 'pre' | 'post'
```

## ResolveIdOptions

Where a plugin's `resolveId` was asked from.

```ts
export interface ResolveIdOptions {
  /** The file or module that contains the import. Absent for an entry point. */
  importer?: string
  /** The namespace the importer lives in. Absent means "file". */
  namespace?: string
  /**
   * The kind of import: an entry point, a static or dynamic import, a
   * `require`, and so on. Left out when the caller does not know.
   */
  kind?: string
  /**
   * Values passed by the import's attributes or `with` clause, if any. A plugin
   * that does not care can ignore this.
   */
  with?: Record<string, string>
  /**
   * True when this question came from a plugin calling `this.resolve` rather
   * than from the engine walking an import. A plugin that rewrites imports but
   * must not answer its own questions checks this.
   */
  custom?: boolean
}
```

## ResolvedId

A `resolveId` answer.
Returning a `ResolvedId` (or its path as a bare string) claims the import and stops the search. Returning `false` marks the import external and leaves the specifier as written in the output. Returning `null` declines.

```ts
export interface ResolvedId {
  /** The resolved path. A path in the "file" namespace must be absolute. */
  id: string
  /** True to leave the import in the output rather than bundle it. */
  external?: boolean
  /**
   * True to promise the module has no side effects, so it can be dropped when
   * nothing imports from it. Only meaningful as `true`.
   */
  moduleSideEffects?: boolean
  /** A virtual filesystem the id belongs to. "file" by default. */
  namespace?: string
  /** A value carried to this module's `load`, so the two halves can pass state. */
  meta?: unknown
}
```

## LoadResult

A `load` answer that supplies a module's contents.

```ts
export interface LoadResult {
  /** The module's source, instead of what the file system holds. */
  code: string | Uint8Array
  /** How to interpret `code`. Defaults to `"js"`. */
  loader?: Loader
  /**
   * The directory this module's own relative imports resolve against. Left out,
   * the directory the id is in.
   */
  resolveDir?: string
  /** A value carried from the matching `resolveId`. */
  meta?: unknown
  /** Extra paths this answer depends on, so a change triggers a rebuild. */
  watchFiles?: string[]
  /** Extra directories this answer depends on. */
  watchDirs?: string[]
}
```

## SourceDescription

What a `transform` hook returns.

```ts
export interface SourceDescription {
  /** The transformed source. */
  code: string
  /**
   * A sourcemap for the transform, as JSON text. Optional; without one the
   * engine maps positions back to the original where it can.
   */
  map?: string | null
  /** Extra paths this transform depends on. */
  watchFiles?: string[]
  watchDirs?: string[]
}
```

## OutputChunk

One output file, as a `generateBundle`/`writeBundle` hook sees it.

```ts
export interface OutputChunk {
  type: 'chunk'
  /** The path the file is written to, relative to the output directory. */
  fileName: string
  /** The generated source. Mutating this changes what is written. */
  code: string
  /** True when this chunk is an entry point. */
  isEntry: boolean
  /** The exports this chunk publishes. */
  exports: string[]
  /** The module ids this chunk was built from. */
  modules: string[]
}
```

## OutputAsset

A non-code output, as a `generateBundle`/`writeBundle` hook sees it.

```ts
export interface OutputAsset {
  type: 'asset'
  /** The path the file is written to, relative to the output directory. */
  fileName: string
  /** The asset's bytes. Mutating this changes what is written. */
  source: string | Uint8Array
}
```

## OutputBundle

Every output, keyed by file name.

```ts
export interface OutputBundle {
  [fileName: string]: OutputChunk | OutputAsset
}
```

## NormalizedOutputOptions

The output options a `generateBundle`/`writeBundle` hook is handed.

```ts
export interface NormalizedOutputOptions {
  /** The output format. */
  format: Format
  /** The output directory, when one was given. */
  dir?: string
  /** The single output file, when one was given. */
  file?: string
  /** Whether the outputs are being written to disk. */
  write: boolean
}
```

## EmittedFile

A file a plugin asks the build to emit with `this.emitFile`.

```ts
export type EmittedFile =
  | { type: 'asset'; name?: string; fileName?: string; source: string | Uint8Array }
```

## GuchhoLog

A diagnostic a plugin reports through `this.warn` or `this.error`.

```ts
export interface GuchhoLog {
  message: string
  /** The file the message blames. */
  id?: string
  /** A code for the message, for a caller filtering them. */
  code?: string
  /** A 1-based line. */
  line?: number
  /** A 0-based column. */
  column?: number
}
```

## PluginContext

What every hook is called with as `this`.
The context is the plugin's interface to the build: it can resolve a path with the build's own rules, report a warning, fail the build, and emit a file of its own.

```ts
export interface PluginContext {
  /** This plugin's name, as declared. */
  readonly pluginName: string
  /**
   * Facts about the running build that do not come from the plugin itself.
   *
   * `guchhoVersion` is reported rather than omitted, because a plugin that
   * switches on it is otherwise reading undefined and taking the wrong branch
   * silently. `watchMode` is true when the build is a watch rebuild.
   */
  readonly meta: {
    readonly guchhoVersion: string
    readonly watchMode: boolean
  }
  /**
   * Resolves a path with the build's own rules and the same chain of plugins,
   * then hands back the answer. This is how a plugin asks where an import
   * points without claiming it. Returns `null` when nothing resolves it.
   *
   * `custom: true` is set on the question so a plugin that answers resolution
   * can tell its own question from the engine's and decline it.
   */
  resolve(
    source: string,
    importer?: string,
    options?: ResolveIdOptions
  ): Promise<ResolvedId | null>
  /**
   * Adds a file to the build's output. Returns a reference id that names the
   * file for `getFileName`, and that can be handed to another `emitFile`.
   */
  emitFile(file: EmittedFile): string
  /** The output path a reference id from `emitFile` resolves to. */
  getFileName(referenceId: string): string
  /** Reports a warning against this plugin. Does not fail the build. */
  warn(message: string | GuchhoLog): void
  /** Fails the build with this message. Never returns. */
  error(message: string | GuchhoLog): never
}
```

## Plugin

A hook the engine calls while a build runs.
Every hook is optional, and every hook that can decline does so by returning `null` or leaving the return undefined.

```ts
export interface Plugin {
  /** This plugin's name. Not optional: it appears in the diagnostics it causes. */
  name: string
  /** When this plugin runs relative to the others. */
  order?: PluginOrder
  /** Called once per build, before any file is read. */
  buildStart?(
    this: PluginContext,
    options: BuildOptions
  ): void | Promise<void>
  /**
   * Asked where a specifier resolves to, for every import and entry point.
   *
   * Return a `ResolvedId` or a path string to claim it, `false` to mark it
   * external, or `null` to decline.
   */
  resolveId?(
    this: PluginContext,
    source: string,
    importer: string | undefined,
    options: ResolveIdOptions
  ): ResolvedId | string | false | null | undefined | Promise<
    ResolvedId | string | false | null | undefined
  >
  /**
   * Asked for a module's contents, before the loader would read them.
   *
   * Return `LoadResult` or a source string to claim the module, or `null` to
   * let the next plugin and then the loader have it.
   */
  load?(
    this: PluginContext,
    id: string
  ): LoadResult | string | null | undefined | Promise<
    LoadResult | string | null | undefined
  >
  /**
   * Transforms a module's source after it has been loaded and before it is
   * parsed. `code` is the text the previous step produced.
   */
  transform?(
    this: PluginContext,
    code: string,
    id: string
  ): SourceDescription | string | null | undefined | Promise<
    SourceDescription | string | null | undefined
  >
  /**
   * Called at the end of the build, after every module is read. `error` is set
   * when the build failed, which makes this the place to clean up either way.
   */
  buildEnd?(
    this: PluginContext,
    error?: Error
  ): void | Promise<void>
  /**
   * Called after the output has been produced but before it is written, with a
   * bundle the hook may mutate, add to, or delete from. This is where a plugin
   * rewrites generated code or emits a sibling file.
   */
  generateBundle?(
    this: PluginContext,
    options: NormalizedOutputOptions,
    bundle: OutputBundle
  ): void | Promise<void>
  /**
   * Called after the output has been written, with the same bundle. This is
   * where a plugin reports on, or reads back, what was written.
   */
  writeBundle?(
    this: PluginContext,
    options: NormalizedOutputOptions,
    bundle: OutputBundle
  ): void | Promise<void>
  /** Called when the context is torn down - the last hook a plugin sees. */
  closeBundle?(this: PluginContext): void | Promise<void>
}
```

