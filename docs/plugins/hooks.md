---
title: Plugin hooks
description: Every plugin hook of a Guchho build, when it runs and what it answers with.
---

# Plugin hooks

Every hook is optional. A hook that can decline does so by returning `null` (or
leaving the return `undefined`), which says "not mine" rather than "stop". Hooks
may be `async`; the engine runs them one at a time while the build waits.

The signatures are in the [generated plugin types](../api/generated/plugin-types.md);
the order they run in is the lifecycle on the [overview page](README.md).

## buildStart

```ts
buildStart?(
  this: PluginContext,
  options: BuildOptions
): void | BuildStartResult | Promise<void | BuildStartResult>
```

Called once per build, before any file is read. The place to open a session,
read a manifest, or look at what build is about to run. It may return
`{ entries, virtualModules, watchFiles, watchDirs }` to add inputs and
dependencies; see [virtual modules](README.md#virtual-modules).

## resolveId

```ts
resolveId?(
  this: PluginContext,
  source: string,
  importer: string | undefined,
  options: ResolveIdOptions
): ResolvedId | string | false | null | undefined | Promise<…>
```

Asked where a specifier resolves to, for every import and entry point. Decide
three ways:

- Return a `ResolvedId` (or the path as a bare string) to claim the import and
  stop the search.
- Return `false` to mark the import external and leave the specifier as written
  in the output.
- Return `null` (or nothing) to decline and let the next plugin, then the
  resolver, have it.

`options` says where the question came from: the `importer` and its
`namespace`, the `kind` of import, any attributes from the import's `with`
clause, and whether the question came from a plugin's own `this.resolve`.

A plugin that rewrites imports but must not answer its own questions checks
`options.custom` and declines when it is `true`, because `this.resolve` sets it.

To claim a module has no side effects - so it can be dropped when nothing
imports from it - return it with `moduleSideEffects: false`. The default,
`true`, keeps the module even when it is unused. Despite the name, this field
follows Guchho's engine value rather than Rollup's inverted spelling. This is
the hook that [tree shaking](../guide/tree-shaking.md) points at.

## load

```ts
load?(
  this: PluginContext,
  id: string,
  info: LoadHookInfo
): LoadResult | string | null | undefined | Promise<…>
```

Asked for a module's contents, before the loader would read them. Return a
`LoadResult` (or the source as a string) to claim the module, or `null` to let
the next plugin and then the loader have it. `info` carries the module's
`namespace`, `suffix` and import `with` values, plus the `meta` left by the
matching `resolveId`.

`LoadResult` carries `code`, the `loader` to interpret it with, a `resolveDir`
for the module's own relative imports, a `meta` value ferried from the matching
`resolveId`, and watch files/directories the answer depends on.

Together with `resolveId`, this is how virtual modules work: `resolveId` answers
a synthetic id in a virtual namespace, `load` answers the code for it. An id in
a non-`file` namespace is only ever read through a plugin, never the file
system.

## transform

```ts
transform?(
  this: PluginContext,
  code: string,
  id: string,
  info: TransformHookInfo
): SourceDescription | string | null | undefined | Promise<…>
```

Transforms a module's source after it has been loaded and before it is parsed.
Return the transformed source (a string or `Uint8Array`) as a bare value or as a
`SourceDescription`, or `null` to decline. Every matching transform sees the
code the previous one produced, in plugin order, so a replacement is never
appended to the text the loader supplied. It is the place to strip a loader,
rewrite imports textually, or preprocess a module that is not a plain
JavaScript/TypeScript input. `copy` and `binary` modules are not transformed.

`info` carries the module's `namespace`, `suffix`, `loader` and import `with`
values, plus the `meta` left by `resolveId` or an earlier transform. A
`SourceDescription` may also carry a JSON `map`, `meta` for later hooks, and
`watchFiles`/`watchDirs`. With source maps enabled, a returned map becomes the
module's input map and is used in preference to a `sourceMappingURL` comment.
A thrown error fails the build with the hook's own message.

## buildEnd

```ts
buildEnd?(this: PluginContext, error?: Error): void | Promise<void>
```

Called at the end of the build, after every module is read. `error` is set when
the build failed, which makes this the place to clean up either way - a plugin
that opened something in `buildStart` closes it here.

## generateBundle

```ts
generateBundle?(
  this: PluginContext,
  options: NormalizedOutputOptions,
  bundle: OutputBundle
): void | Promise<void>
```

Called after the output has been produced but before it is written, with a
bundle the hook may mutate, add to, or delete from. This is where a plugin
rewrites generated code, emits a sibling file with `this.emitFile`, or drops an
output it wants kept out.

## writeBundle

```ts
writeBundle?(
  this: PluginContext,
  options: NormalizedOutputOptions,
  bundle: OutputBundle
): void | Promise<void>
```

Called after the output has been written, with the same bundle. This is where a
plugin reports on - or reads back - what actually landed on disk.

## closeBundle

```ts
closeBundle?(this: PluginContext): void | Promise<void>
```

Called when the context is torn down. The last hook a plugin sees; the
counterpart to `buildStart` when the plugin's life is the whole context rather
than a single build.

## See also

- [The hook context](context.md)
- [Generated plugin types](../api/generated/plugin-types.md)