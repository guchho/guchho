---
title: Minification
description: Whitespace, identifier and syntax minification, plus property mangling.
---

# Minification

Minification is a choice about the bytes a build leaves behind, and a person who
has not made it should get output they can still read. All minification is off
by default.

That default is worth keeping in mind while reading this page. Turning on
minification changes names, removes comments and rewrites expressions; every
switch below is opt-in so a build does not make those changes on your behalf.

## The three switches

They are independent, so `minifyWhitespace` alone is a safe way to shrink a file
for debugging while keeping names intact.

| Option | What it does |
| --- | --- |
| `minifyWhitespace` | Remove insignificant whitespace |
| `minifyIdentifiers` | Shorten local identifiers |
| `minifySyntax` | Apply size-reducing rewrites |

`--minify` on the command line is shorthand for all three:

```bash
guchho build src/app.js --minify
guchho build src/app.js --minify-whitespace   # only one
```

The same three switches through the API:

```js
import { build } from 'guchho';

await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  minifyWhitespace: true,
  minifyIdentifiers: true,
  minifySyntax: true,
});
```

`minify: true` is the shorthand in a config file as well.

A config file can also name the passes individually, under `minify`:

```js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    minify: {
      whitespace: true,
      identifiers: true,
      syntax: true,
    },
  },
};
```

`--minify` covers the JavaScript and CSS passes. Markup has its own switch,
`minifyHtml`, because whitespace in HTML is not always insignificant.

### Whitespace

Whitespace minification removes spaces and newlines that the language does not
need:

```js
// input
function add(a, b) {
  return a + b;
}

console.log(add(1, 2));
```

```js
// roughly, with --minify-whitespace
function add(a,b){return a+b}console.log(add(1,2));
```

Comments that are not legal comments are removed as part of the same pass.
Names are untouched, so this is the least surprising switch to turn on while
debugging a size problem.

### Identifiers

Identifier minification renames local variables, parameters and other names
whose scope is inside the file:

```js
// input
function calculateTotal(price, quantity) {
  const subtotal = price * quantity;
  return subtotal;
}
```

```js
// roughly, with --minify-identifiers
function calculateTotal(n, t) {
  const r = n * t;
  return r;
}
```

In a non-bundled transform, top-level names are kept because the file may be
consumed later. A bundle can rename a top-level name that nothing outside the
bundle can observe:

```js
// roughly, with --bundle --minify-identifiers
function c(o, t) {
  const l = o * t;
  return l;
}

console.log(c(2, 3));
```

Exported names, property names and names a consumer can observe are not changed
by this pass.

Identifier minification does not require whitespace minification. The output
above is still indented because the whitespace pass was not enabled.

### Syntax

Syntax minification applies rewrites that make a reachable expression or
statement smaller without changing its observable result:

```js
// input
const flag = 1 + 2;
if (true) {
  start();
}
```

```js
// roughly, with --minify-syntax
const flag = 3;
start();
```

It also folds constants, simplifies boolean expressions and removes branches
that cannot be taken. It is the pass most likely to change the *shape* of the
output, so it is the one to compare carefully when a build behaves differently
after minification.

Syntax minification is not tree shaking. It works inside code the build already
kept; it does not decide that an unused module can be dropped. See
[Tree shaking](tree-shaking.md) for that decision.

## Dropping code

Two options remove code that is useful while developing and usually unwanted in
a shipped file.

### `drop`

`drop` removes calls to `console` methods and `debugger` statements:

```js
import { build } from 'guchho';

await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  minify: true,
  drop: ['console', 'debugger'],
});
```

```js
// input
console.log('starting');
debugger;
run();
```

```js
// roughly, with drop: ['console', 'debugger']
run();
```

On the command line the list is spelled as repeatable flags:

```bash
guchho build src/app.js --outdir=dist --drop:console --drop:debugger
```

Only `console` and `debugger` are valid values. A different name is reported
with a note listing the two values that are accepted, rather than being silently
ignored.

### `dropLabels`

`dropLabels` removes a labelled statement outright. It is the classic way of
fencing off development-only code:

```js
// src/app.js
DEBUG: {
  installDevTools();
  showDebugPanel();
}

start();
```

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  dropLabels: ['DEBUG'],
});
```

The whole labelled block is removed:

```js
// roughly
start();
```

A label on a loop or a block works the same way:

```js
DEV_LOOP: for (const item of items) {
  log(item);
}
```

With `dropLabels: ['DEV_LOOP']`, the loop is removed. The label must match
exactly; labels are case-sensitive.

The command-line spelling separates several labels with commas:

```bash
guchho build src/app.js --outdir=dist --drop-labels=DEBUG,DEV_LOOP
```

`dropLabels` is not the same as `define`. A `define` substitutes an identifier
and lets the minifier remove a branch that becomes statically false. A label
names a statement and removes it directly, even when no identifier is involved.

## Keeping names

`keepNames` preserves function and class names while minifying, which matters
when stack traces or reflection read them.

```js
// src/app.js
function calculateTotal(price) {
  return price * 1.2;
}

console.log(calculateTotal.name);
```

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  bundle: true,
  minify: true,
  keepNames: true,
});
```

Without `keepNames`, identifier minification may rename the function and its
observed `.name` changes with it. With `keepNames`, the name a stack trace or
`calculateTotal.name` reports stays `calculateTotal`.

```bash
guchho build src/app.js --outdir=dist --bundle --minify --keep-names
```

The cost is bytes: preserving a name may require a helper or prevent a rename.
Turn it on when names are part of the contract - error reporting, dependency
injection, reflection or a plugin that looks functions up by name.

## Property mangling

Property mangling renames object properties. It is separate from identifier
minification because property names are observable from outside the module, so
the decision has to be scoped and deliberate.

| Option | Meaning |
| --- | --- |
| `mangleProps` | Regex matching property names to shorten |
| `reserveProps` | Regex of names to protect |
| `mangleQuoted` | Also shorten `obj["name"]` string keys |
| `mangleCache` | Carry name decisions between builds |

A typical setup mangles a private-looking prefix and protects a reserved one:

```bash
guchho build src/app.js --outdir=dist --minify \
  --mangle-props='^_' \
  --reserve-props='^__'
```

```js
// input
const state = {
  _internalId: 1,
  _internalName: 'Ada',
  __protoSafe: true,
};

console.log(state._internalId, state._internalName, state.__protoSafe);
```

```js
// roughly, after mangling
const state = {
  a: 1,
  b: 'Ada',
  __protoSafe: true,
};

console.log(state.a, state.b, state.__protoSafe);
```

`^_` matches `_internalId` and `_internalName`, while `^__` protects
`__protoSafe` because it starts with two underscores. The reserve pattern wins
even though the mangle pattern also matched it.

The same options through the API:

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  minify: true,
  mangleProps: '^_',
  reserveProps: '^__',
});
```

### Quoted properties

By default, a quoted access is left alone even when the name matches the
pattern:

```js
state['_internalId'];
```

`mangleQuoted: true` also shortens string keys:

```bash
guchho build src/app.js --outdir=dist --minify \
  --mangle-props='^_' --mangle-quoted
```

```js
// roughly
state['a'];
```

This is only safe when every access uses a literal string that the engine can
see. A name assembled at runtime - `state['_' + key]` - is not rewritten, and
neither is data read from JSON. If a property name crosses a boundary as a
string, either add its pattern to `reserveProps` or leave quoting alone.

### Keeping decisions stable

`mangleCache` keeps a project's shortened property names stable across builds,
so a hot-reload session does not invalidate cached files.

The command line stores the mapping in a JSON file:

```bash
guchho build src/app.js --outdir=dist --minify \
  --mangle-props='^_' \
  --mangle-cache=.guchho-mangle.json
```

The file is written after a successful build and read at the start of the next
one. Keep it under version control, or persist it in CI, so two builds of the
same code produce the same names. If the cache is lost, a property may receive a
different short name and a client holding a cached copy of an older file can
break even though the source did not change.

The API takes a record instead of a path and returns the updated record:

```js
const result = await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  minify: true,
  mangleProps: '^_',
  mangleCache: previousCache,
});

if (result.mangleCache) {
  await fs.writeFile(
    '.guchho-mangle.json',
    JSON.stringify(result.mangleCache, null, 2),
  );
}
```

`result.mangleCache` is present when the build recorded property-name
decisions; if the pattern matched nothing, it is absent. Feed the record back as
`mangleCache` on the next build to keep the same names.

### When property mangling is unsafe

Property mangling changes names that code outside the bundle may read. Do not
apply it to:

- a library's public API (`mangleProps` should match private names only),
- object keys that are serialized to JSON and read elsewhere,
- names read through `Object.keys`, `for...in` or reflection by another system,
- DOM properties or third-party objects whose names you do not own.

The pattern is the safety boundary. A broad `mangleProps: '.*'` will rename
almost everything that can be reached as a property, including names you did not
intend to change.

## Other size and output controls

| Option | Default | Effect |
| --- | --- | --- |
| `lineLimit` | `0` (no limit) | Keep generated lines from growing past this many bytes by inserting breaks where the printer can. |
| `charset` | `ascii` | Escape non-ASCII as `\uXXXX`; `utf8` emits the characters when the target allows it. |
| `legalComments` | `default` | `none`, `inline`, `eof`, `linked` or `external` placement for `/*! ... */` comments. |
| `ignoreAnnotations` | `false` | Treat `/* @__PURE__ */` and similar markers as ordinary comments. |
| `keepNames` | `false` | Preserve function and class names, including `.name` reads. |
| `minifyHtml` | `false` | Collapse whitespace in the emitted markup. |
| `pretty` | `true` for HTML | Indent the emitted markup. |

`lineLimit` is for consumers that dislike very long lines - a log viewer, an
old editor, a tool that diffs line by line. It is not a hard maximum: the
printer inserts a break where it can, and a single indivisible token can still
be longer than the limit. `0` disables the behaviour.

```bash
guchho build src/app.js --outdir=dist --minify --line-limit=500
```

`charset` interacts with minification because a shortened identifier may still
contain non-ASCII characters. Under `ascii`, a name that cannot be escaped is a
diagnostic rather than an unreadable file. See
[Targets & browsers](targets-and-browsers.md#charset) for the full behaviour.

`legalComments` decides what happens to license banners. The default keeps them
at the end of a bundle and inline in a single-file transform; the other modes
move them to a separate file or drop them. See
[Legal comments](legal-comments.md).

`ignoreAnnotations` turns off the meaning of `/* @__PURE__ */` and
`/* @__NO_SIDE_EFFECTS__ */`. With it on, an annotated call is no longer
removable, and tree shaking becomes more conservative. It is mainly a debugging
tool for a bad annotation in a dependency.

`minifyHtml` and `pretty` pull in opposite directions. `pretty` indents the
emitted document so it can be read; `minifyHtml` collapses insignificant
whitespace. Markup where whitespace is meaningful - `pre`, `textarea`, inline
formatting contexts - is handled carefully by both. Pick the one that matches
what the output is for.

## A recommended progression

Minification is easier to adopt one switch at a time:

1. **Start with whitespace.** `minifyWhitespace: true` is easy to verify and
   rarely changes behaviour.
2. **Add identifiers.** Compare stack traces and any code that reads function
   names. Add `keepNames` if names matter.
3. **Add syntax.** Run the test suite; this is the pass that folds branches and
   changes expression shape.
4. **Add property mangling only with a narrow pattern.** Decide which names are
   private, reserve the rest, and commit the cache.
5. **Add `drop` and `dropLabels` last.** They remove code, so they are the most
   visible change to a developer reading the output.

A production build that follows that order might look like:

```js
import { build } from 'guchho';

await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  bundle: true,
  minifyWhitespace: true,
  minifyIdentifiers: true,
  minifySyntax: true,
  drop: ['console', 'debugger'],
  keepNames: true,
  sourcemap: 'linked',
});
```

The equivalent command line spells the same choices as flags:

```bash
guchho build src/index.html --outdir=dist --bundle --minify \
  --drop:console --drop:debugger --keep-names --sourcemap=linked
```

## Checking what changed

Two comparisons catch most surprises:

```bash
# Size without minification, then with it
guchho build src/app.js --bundle --outdir=dist/full
guchho build src/app.js --bundle --minify --outdir=dist/min

# Keep a map so the minified output is still readable
guchho build src/app.js --bundle --minify --sourcemap=linked --outdir=dist/min
```

If a bug appears only after minification, rebuild with `minifySyntax` off but
the other passes on. If the bug disappears, it is a syntax rewrite; if it stays,
it is likely a name change or a `drop`/`dropLabels` removal. Building with
`--sourcemap` keeps the original source available while you narrow it down.

## See also

- [Tree shaking](tree-shaking.md) - removing code that is not reachable.
- [Targets & browsers](targets-and-browsers.md) - lowering syntax for older
  runtimes.
- [Source maps](source-maps.md) - keeping minified output debuggable.
- [Legal comments](legal-comments.md) - banners and licence headers.
- [Mangle property names](../recipes/mangle-property-names.md) - a focused
  recipe for `mangleProps` and `mangleCache`.
- [`guchho build`](../reference/cli/build.md) - every command-line flag.
