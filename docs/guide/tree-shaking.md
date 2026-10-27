---
title: Tree shaking
description: Removing unused exports and unreachable statements.
---

# Tree shaking

Tree shaking removes unused exports and unreachable statements. It is a
graph-wide decision: a declaration is kept because something reachable uses it,
not because it happens to be exported from a file that was read.

Two things make that possible:

- The linker knows which parts of a module are imported by which other modules.
- The parser marks which statements have observable effects, so a module with
  no live imports can still be kept when loading it would do something.

The result is smaller output, but the safety rule matters more than the size
win: code is only removed when removing it cannot change what the program does.

## When it is on

`treeShaking` defaults to **enabled** when bundling or when the output is an
IIFE, because in both cases there is no way for a consumer to concatenate more
code onto the end of the file and rely on the parts that were dropped.

Otherwise it is **disabled**, so a file being rewritten still behaves like the
original. A plain ESM transform is the important case: a later consumer may
still `import { unused } from './file.js'`, so Guchho cannot know that the
declaration is dead.

| Value | Meaning |
| --- | --- |
| `default` | The rule above. |
| `false` | Never remove anything. |
| `true` | Always remove; safe even for a non-bundled file. |

```bash
guchho build src/app.js --bundle --tree-shaking
```

The flag is only needed to force a non-default choice. With `--bundle`, tree
shaking is already on. With a plain `--format=iife` build it is on as well. A
non-bundled ESM transform keeps everything unless `--tree-shaking` is passed.

The same choice in a config file:

```js
export default {
  build: {
    entry: 'src/app.js',
    outdir: 'dist',
    bundle: true,
    treeShaking: false,
  },
};
```

## A first example

Here is a module with two exports:

```js
// src/math.js
export const used = (a, b) => a + b;
export const unused = (a, b) => a * b;
```

Only one of them is imported:

```js
// src/app.js
import { used } from './math.js';

console.log(used(2, 3));
```

With tree shaking on, `unused` is not in the output. With it off, both functions
survive, because the output might be concatenated with code that still calls
`unused`:

```bash
guchho build src/app.js --bundle --outdir=dist
guchho build src/app.js --bundle --tree-shaking=false --outdir=dist
```

The first build writes a bundle containing `used`. The second keeps `unused` as
well. Both builds are correct; they make different promises about the output.

### What counts as used

An export is used when a reachable module imports it, re-exports it, or reads
the module namespace in a way that could reach it. The entry point itself is
always reachable.

```js
// src/barrel.js
export { a } from './a.js';
export { b } from './b.js';
```

```js
// src/app.js
import { a } from './barrel.js';

console.log(a);
```

If nothing imports `b` from the barrel, and nothing else references the
re-export, the `b` part can be dropped even though it is written in a file that
was read. The barrel's *side effects* are still preserved.

The exception is an ESM transform with tree shaking explicitly turned off, or a
non-bundled file where the default keeps the export. In those modes Guchho does
not assume who will consume the file.

## Side effects

Tree shaking can only drop a module when loading it has no observable effect. A
module that only declares functions is easy to drop. A module that writes to the
DOM, mutates a shared object, throws, or starts a timer is not.

```js
// src/analytics.js
console.log('analytics loaded');

export const track = (event) => {
  // ...
};
```

If nothing imports `track`, the module still cannot be removed, because loading
it would print to the console. The `console.log` is a side effect.

```js
// src/helpers.js
export const clamp = (n, min, max) => Math.min(max, Math.max(min, n));
```

Nothing happens when this module loads. If nothing imports `clamp`, the whole
module can go.

An `onResolve` hook can answer the side-effect question with a
`moduleSideEffects` value:

- `true` (default) - keep the module even if unused.
- `false` - it is side-effect free; drop it when nothing uses it.

Only mark a module side-effect free when loading it genuinely has no observable
effect, because getting it wrong deletes code that was doing work.

A plugin can make that claim for a virtual module:

```js
const generatedHelpers = {
  name: 'generated-helpers',
  resolveId(source) {
    if (source === 'virtual:helpers') {
      return { id: 'virtual:helpers', moduleSideEffects: false };
    }
    return null;
  },
  load(id) {
    if (id === 'virtual:helpers') {
      return 'export const answer = () => 42;';
    }
    return null;
  },
};

await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  bundle: true,
  plugins: [generatedHelpers],
});
```

The virtual module is dropped when nothing imports `answer`. If the same module
also registered a global, marking it side-effect free would be a bug.

### `sideEffects` in `package.json`

Dependencies can make the same claim in their package metadata. The resolver
reads the `sideEffects` field, so a published package does not need a plugin to
be tree-shakeable:

```json
{
  "name": "tiny-ui",
  "sideEffects": false
}
```

A package that is mostly pure but ships a few files with setup work can list
just those files:

```json
{
  "name": "tiny-ui",
  "sideEffects": ["./src/setup.js", "**/*.css"]
}
```

Read the array as "these files have side effects"; everything else may be
dropped when unused. The patterns follow the usual `package.json` glob rules.
If the metadata is wrong, the bug is in the package's manifest, not in the
consumer's build.

Guchho can explain an exclusion with a warning that names the `sideEffects`
claim, so a surprise deletion can be traced back to the field that caused it.

## Purity annotations

`/* @__PURE__ */` marks a call as removable when its result is unused.

```js
import { createStore } from './store.js';

const unused = /* @__PURE__ */ createStore();
```

Because the call is marked pure, the unused store can be removed. Without the
annotation the call is assumed to have an effect and is kept:

```js
const unused = createStore();
```

The annotation goes immediately before the call or `new` expression it
describes:

```js
const schema = /* @__PURE__ */ makeSchema({
  type: 'object',
});
```

It also works on a constructor:

```js
const cache = /* @__PURE__ */ new Map();
```

The annotation is a promise, not a hint: it says the call has no observable
effect other than producing its return value. Marking a call that writes a file
or registers a listener as pure is how a build ends up deleting real work.

### Marking a function instead of every call

`/* @__NO_SIDE_EFFECTS__ */` marks a function declaration as pure. Calls to that
function are then treated as pure without repeating the annotation at each call
site:

```js
/* @__NO_SIDE_EFFECTS__ */
export function createConfig(name) {
  return { name, createdAt: 0 };
}

const unused = createConfig('test');
```

The annotation is useful when the function is local and its calls are easy to
recognize. For a function that crosses a package boundary, the `pure` option is
more explicit:

```bash
guchho build src/app.js --bundle --pure:createConfig
```

The `pure` list names functions that may be treated the same way as
`/* @__PURE__ */` calls. On the command line it is spelled as a repeatable
`--pure:<name>` flag.

`ignoreAnnotations` makes the engine treat the comment as ordinary text:

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  bundle: true,
  ignoreAnnotations: true,
});
```

With that setting, a `/* @__PURE__ */` call is no longer removable. That is
occasionally useful when a third-party annotation is known to be wrong.

## What is kept even when unused

Tree shaking is conservative. These are the common reasons code survives:

| Reason | Example |
| --- | --- |
| A top-level side effect | `console.log(...)`, `document.body.append(...)` |
| A mutation | `globalThis.flag = true`, `registry.set(...)` |
| A thrown error | `throw new Error(...)` at module scope |
| Top-level `await` | `await loadConfig()` |
| A dynamic import | `import('./optional.js')` |
| A module marked side-effectful | no `sideEffects: false`, or the file is in the array |
| Tree shaking disabled | a plain ESM transform with the default setting |

CommonJS is less precise than ESM. `require` is a runtime call, and a property
read on `module.exports` is harder to prove dead, so a CommonJS module can keep
more code than the equivalent ESM module.

## How it interacts with minification

Tree shaking and `minifySyntax` both remove code, but they answer different
questions:

- Tree shaking asks whether a declaration or module is reachable from an entry.
- `minifySyntax` asks whether an expression or branch inside a reachable function
  can be simplified.

A dead branch created by a `define` is often removed by the minifier even when
tree shaking is off:

```js
if (false) {
  console.log('development only');
}
```

A declaration exported from an unused module needs tree shaking to disappear:

```js
export function neverImported() {}
```

Turning on `--minify` without `--bundle` does not turn on tree shaking. They are
separate decisions, and the defaults reflect that.

## Debugging an unexpected result

When something disappears that should not have, start with the graph:

```bash
guchho build src/index.html --outdir=dist --metafile=dist/meta.json --analyze
```

The metafile lists which inputs contributed to each output and which outputs
import which others. A module that is absent from every output's contribution
list was never reachable.

The next questions usually identify the cause:

1. **Is the module reachable at all?** Search the graph for the file that
   imports it. A reference that only exists in a comment is not an import.
2. **Is the call annotated as pure when it is not?** Check for
   `/* @__PURE__ */`, `/* @__NO_SIDE_EFFECTS__ */` and the `pure` option.
3. **Does the package claim `sideEffects: false`?** Check its `package.json`.
   A warning usually names the claim that excluded the module.
4. **Is the build actually bundling?** Without `--bundle`, the default keeps
   exports because the output may be consumed later.
5. **Is the code CommonJS?** `require` is harder to analyze than `import`, and a
   runtime property access may keep the module alive.

To compare the two behaviours, build once with tree shaking and once without and
diff the outputs:

```bash
guchho build src/app.js --bundle --outdir=dist/shaken
guchho build src/app.js --bundle --tree-shaking=false --outdir=dist/full
```

If a file is missing from `dist/shaken` but present in `dist/full`, the
difference is the set of declarations the linker considered dead.

## See also

- [Writing plugins](../plugins/README.md) - the hook that declares side effects.
- [Bundling & splitting](bundling-and-splitting.md) - how the graph is built.
- [Minification](minification.md) - syntax rewrites and dead branches.
- [Metafile](../reference/metafile.md) - inspect the graph a build produced.
