---
title: The plugin context
description: What this is inside a plugin hook, and what a plugin can ask the build to do.
---

# The plugin context

Every hook is called with a `PluginContext` as `this` - the plugin's interface
to the build. Through it a plugin reports a message, fails the build, emits
asset files, registers virtual modules in `buildStart`, and resolves a path with
the build's own rules.

```js
const plugin = {
  name: 'virtual-readme',
  resolveId(source) {
    if (source === 'readme') return { id: 'readme', namespace: 'virtual' }
  },
  load(id) {
    if (id.startsWith('readme:')) return '# Virtual module'
  },
  async generateBundle(_, bundle) {
    for (const file of Object.values(bundle)) {
      if (!file.isEntry) continue
      this.emitFile({
        type: 'asset',
        fileName: 'manifest.json',
        source: JSON.stringify(file.modules, null, 2),
      })
      this.warn({ message: 'emitted manifest' })
    }
  },
}
```

## Properties

### `pluginName`

`readonly pluginName: string`

This plugin's name, as declared.

### `meta`

`readonly meta: { guchhoVersion: string; watchMode: boolean }`

Facts about the running build that do not come from the plugin itself.
`guchhoVersion` is reported rather than omitted, so a plugin that switches on it
is not reading `undefined` and taking the wrong branch silently. `watchMode` is
`true` while watch mode is enabled, including the opening build and every
rebuild.

## Methods

### resolve

```ts
resolve(
  source: string,
  importer?: string,
  options?: ResolveIdOptions
): Promise<ResolvedId | null>
```

Resolves a path with the build's own rules - resolve extensions, tsconfig
paths, package exports - **and the same chain of plugins**. This is how a
plugin asks where an import points without claiming it. Returns `null` when
nothing resolves it, and the answer has `id`, `namespace`, `suffix`,
`moduleSideEffects`, `external` and `meta` where they apply. The question may
name an `importer`, a `resolveDir`, a `namespace`, a `kind` and import `with`
values through `options`.

The call is a nested request on the same service connection, so it can be made
from inside a hook without waiting for that hook to finish. `custom: true` is
set on the question by default: the calling plugin is skipped, so a plugin that
rewrites imports cannot answer its own question and loop forever. Pass
`custom: false` when the plugin really should see its own question; other
plugins still see `options.custom` and can decline with
`if (options.custom) return null`.

### emitFile

```ts
emitFile(file: EmittedFile): void
```

Adds a file to the build's output. Assets only in this version; a chunk request
is refused with an explanation rather than silently dropped. The method returns
`void`.

There is no `getFileName` in this version. A reference id would have no use
without it, so `emitFile` does not return one either.

### addVirtualModule

```ts
addVirtualModule(module: VirtualModule): void
```

Registers a module for this build. It is available from `buildStart`; the same
shape can be returned in that hook's `virtualModules` array. `id` and `code` are
required, and `namespace`, `suffix`, `loader`, `resolveDir`, `meta`,
`watchFiles` and `watchDirs` are optional. See
[virtual modules](README.md#virtual-modules).

### warn

```ts
warn(message: string | GuchhoLog): void
```

Reports a warning against this plugin. Does not fail the build.

### error

```ts
error(message: string | GuchhoLog): never
```

Fails the build with this message. Never returns: the build is over.

## See also

- [Plugin hooks](hooks.md)
- [Generated plugin types](../api/generated/plugin-types.md)