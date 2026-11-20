---
title: Writing plugins
description: Extending a Guchho build with resolveId, load, transform and bundle hooks.
banner: /plugins-og.png
bannerAlt: Guchho Plugins — extend the build with hooks, loaders, and reporters
bannerWidth: 1200
bannerHeight: 630
---

# Writing plugins

A plugin is a plain object with a `name` and any of the [hooks](hooks.md), run
in the host process while the build runs. It is the way to add resolution rules,
virtual modules, source transforms and output rewrites without forking the
engine.

```js
import { build } from 'guchho'

const env = {
  name: 'env-define',
  buildStart(options) {
    // inspect or validate options before the build starts
  },
}

await build({ entrypoints: ['app.js'], plugins: [env] })
```

Pass a single plugin or an array through the `plugins` option; the
configuration file nests it under a top-level `plugins` key.

When the config root is an array of configurations, each element may carry its
own `plugins`. Since v1.0.6 the plugins on an element run only for that
element's build, so a plugin named by two elements is instantiated once per
build and sees only its own build. A single-configuration root and a flat
top-level `plugins` list apply to every build in the run.

## The shape

- `name` - required, and it appears in every diagnostic the plugin causes.
- `order` - `'pre'` or `'post'`, where the plugin runs relative to the default
  order. Plugins with the same `order` run in the order they appear.
- Hooks - every hook is optional, and every hook that can decline does so by
  returning `null` (or leaving the return `undefined`), which says "not mine"
  rather than "stop".

Every hook can be `async`. The engine runs the plugins one at a time and waits;
a hook that resolves is a hook the engine continues from.

## The lifecycle

1. `buildStart` - once per build, before any file is read. It may contribute
   entries, virtual modules and extra watch paths.
2. `resolveId` - asked where a specifier points, for every import and entry
   point.
3. `load` - asked for a module's contents, before the loader would read them.
4. `transform` - rewrites a module's source after it was loaded and before it
   is parsed.
5. `buildEnd` - after every module is read; `error` is set when the build
   failed.
6. `generateBundle` / `writeBundle` - with the finished output, before and
   after it is written.
7. `closeBundle` - when the context is torn down. The last hook a plugin sees.

## Hooks and context

- [Hooks](hooks.md) - each hook in detail: signature, when it runs, what it
  answers with.
- [Context](context.md) - what `this` is inside a hook, and what a plugin can
  ask the build to do.

## Types

The exact type declarations live in the
[generated plugin types](../api/generated/plugin-types.md), produced from
`index.d.ts`. The short version:

| Type | Means |
| --- | --- |
| `Plugin` | A plugin: `name`, `order`, and the hooks. |
| `PluginContext` | `this` inside a hook. |
| `PluginOrder` | `'pre' \| 'post'`. |
| `ResolveIdOptions` | Where a `resolveId` was asked from. |
| `ResolvedId` | A `resolveId` answer: `id`, `external`, `moduleSideEffects`, `namespace`, `suffix`, `meta`. |
| `LoadHookInfo` | Extra context for `load`: `namespace`, `suffix`, `with`, `meta`. |
| `LoadResult` | A `load` answer: `code`, `loader`, `resolveDir`, `meta`. |
| `TransformHookInfo` | Extra context for `transform`: `namespace`, `suffix`, `loader`, `with`, `meta`. |
| `SourceDescription` | A `transform` answer: `code`, `map`, `meta` and watch paths. |
| `VirtualModule` | A module registered from `buildStart`, with `id`, `code` and optional metadata. |
| `BuildStartResult` | What `buildStart` may return: `entries`, `virtualModules`, `watchFiles`, `watchDirs`. |
| `OutputChunk` / `OutputAsset` | One file in the bundle a bundle hook sees. |
| `OutputBundle` | The whole bundle, keyed by file name. |
| `NormalizedOutputOptions` | The output options a bundle hook is handed. |
| `EmittedFile` | What `this.emitFile` puts in the output. |
| `GuchhoLog` | The shaped object `this.warn` / `this.error` accept. |

## Virtual modules

A virtual module is source the plugin supplies rather than a file the engine
reads. The classic spelling is a pair of hooks: `resolveId` claims an import in
a non-`file` namespace and `load` answers its code.

`buildStart` can instead register the whole module at once, either by returning
`virtualModules` or by calling `this.addVirtualModule()`:

```js
const virtual = {
  name: 'generated-config',
  buildStart() {
    this.addVirtualModule({
      id: 'virtual:config',
      namespace: 'virtual',
      suffix: '?raw',
      loader: 'js',
      resolveDir: 'src',
      code: 'export const value = 1',
    })
  },
}
```

`id` and `code` are required. `code` may be a string or `Uint8Array`;
`namespace` defaults to `"virtual"`, `loader` to `"js"`, and `resolveDir` is the
base for the module's own relative imports. `suffix` must start with `?` or `#`
and is part of the module's identity without being part of its name. `meta` is
carried to the module's hooks, and `watchFiles`/`watchDirs` add dependencies.

A registered module resolves when an import uses its `id` or `id + suffix`, and
its contents come from the registration. An empty id, a malformed suffix, or
two modules with the same namespace, id and suffix fail the build with the
plugin names in the message. `buildStart` may also return `entries` to make a
virtual module an entry point, so nothing has to be written to disk.

## What a plugin cannot do

The engine is a native service; a plugin cannot hook into its internals, only
into the calls listed above. There is no plugin that runs on the compiler's
side of the wire, and no hook past what the engine implements is declared -
every hook in the type has a runtime implementation behind it.

`this.emitFile` accepts assets only; a chunk request is refused with an
explanation. It returns `void`, and `getFileName` is deliberately not declared.

## See also

- [Config: `plugins`](../reference/config/build.md#sections)
- [Tree shaking and side effects](../guide/tree-shaking.md)