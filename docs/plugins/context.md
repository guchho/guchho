---
title: The plugin context
description: What this is inside a plugin hook, and what a plugin can ask the build to do.
---

# The plugin context

Every hook is called with a `PluginContext` as `this` - the plugin's interface
to the build. Through it a plugin reports a message, fails the build, and emits
asset files of its own. Resolution through `this.resolve` is declared but not
wired yet in this version.

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

**Not wired yet in this version.** The method always rejects with a sentence
naming the limitation rather than resolving to a wrong answer.

`custom: true` is set on the question the engine hands down the chain, so a
plugin that answers resolution can tell its own question from the engine's and
decline it with `if (options.custom) return null`.

### emitFile

```ts
emitFile(file: EmittedFile): void
```

Adds a file to the build's output. Assets only in this version; a chunk request
is refused with an explanation rather than silently dropped. The method returns
`void`.

There is no `getFileName` in this version. A reference id would have no use
without it, so `emitFile` does not return one either.

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