---
title: The plugin context
description: What this is inside a plugin hook, and what a plugin can ask the build to do.
---

# The plugin context

Every hook is called with a `PluginContext` as `this` - the plugin's interface
to the build. Through it a plugin finds out where an import points, reports a
message, fails the build, and emits files of its own.

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
      const ref = this.emitFile({
        type: 'asset',
        fileName: 'manifest.json',
        source: JSON.stringify(file.modules, null, 2),
      })
      this.warn({ message: 'emitted manifest', id: this.getFileName(ref) })
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
`true` when the build is a watch rebuild.

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
nothing resolves it.

`custom: true` is set on the question the engine hands down the chain, so a
plugin that answers resolution can tell its own question from the engine's and
decline it with `if (options.custom) return null`.

### emitFile

```ts
emitFile(file: EmittedFile): string
```

Adds a file to the build's output. Accepts an asset (bytes to write) or a chunk
(a module id to bundle into a new output). Returns a reference id that names
the file for `getFileName`, and that can be handed to another `emitFile`.

### getFileName

```ts
getFileName(referenceId: string): string
```

The output path a reference id from `emitFile` resolves to. Useful inside
`generateBundle`/`writeBundle`, where the path is otherwise guessed.

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