---
title: JavaScript & TypeScript
description: Parsing, transforming and targeting JavaScript, TypeScript, JSX and TSX.
---

# JavaScript & TypeScript

Guchho parses JavaScript, TypeScript, JSX and TSX with its own native parser.
TypeScript annotations are erased; the runtime is plain JavaScript.

This page covers what happens to a script on its way through the engine: how a
loader chooses the dialect, what survives TypeScript erasure, how JSX is lowered,
how compile-time substitutions are applied, and which optional checks can run
during a build. The target system has its own page, because a target is a promise
about the emitted syntax rather than a property of one language.

## From source to output

Every JavaScript-family input passes through the same stages:

1. **Lex** - split the text into tokens, keeping comments and source locations.
2. **Parse** - build the syntax tree and resolve scopes and bindings.
3. **Transform** - erase types, lower syntax the target forbids, apply defines,
   and fold what can be folded.
4. **Print** - emit the final text, optionally minified, with a source map.

A syntax error stops at the parse stage and is reported with the file, line and
column. A later stage reports its own diagnostics rather than emitting a file
that happens to be broken. Because the parser and printer are the engine's own,
the same input gives the same output whether it arrives through `build`,
`guchho transform` or the [compiler API](../api/compiler.md).

TypeScript is parsed, not type-checked. Guchho can tell you that a type
annotation is malformed, but it does not verify that one type is assignable to
another and it does not emit declaration files. Run `tsc --noEmit` when type
correctness is what you need; run Guchho when you need the runtime file.

```ts
// input, loader: ts
type User = { name: string };

const greet = (user: User): string => `Hello, ${user.name}!`;
```

```js
// output, roughly
const greet = (user) => `Hello, ${user.name}!`;
```

The type alias and the annotations are gone. The template literal stays because
the default target does not lower it; adding `--target=es5` would turn it into
string concatenation. That separation matters: erasure is a TypeScript concern,
lowering is a target concern, and the two happen in that order.

## Loaders

A loader is what tells the engine which dialect a file is written in. The same
bytes are a syntax error under one loader and a valid module under another, so
choosing the loader is not cosmetic.

| Loader | Meaning |
| --- | --- |
| `js` | JavaScript |
| `jsx` | JavaScript containing markup |
| `ts` | Typed source; annotations erased |
| `tsx` | Typed source that also contains markup |
| `json` | JSON, parsed into an object with named imports |

See [Assets & loaders](assets-and-loaders.md) for the rest of the loader set.

### Default extensions

The extension picks the loader unless the loader map says otherwise:

| Extension | Loader |
| --- | --- |
| `.js`, `.mjs`, `.cjs` | `js` |
| `.jsx` | `jsx` |
| `.ts`, `.mts`, `.cts` | `ts` |
| `.tsx` | `tsx` |
| `.json` | `json` |

The type-annotated extensions are separate files, not a setting on `.js`. An
import written as `./util.js` is also allowed to resolve to `util.ts` or
`util.tsx`, which is what lets typed source sit behind a JavaScript specifier
without changing every import.

### Choosing a loader per extension

A build takes a map from extension to loader:

```js
build: {
  loader: { '.js': 'jsx', '.ts': 'ts' },
}
```

The command line spells the same map with repeatable flags:

```bash
guchho build src/index.html --loader:.js=jsx --loader:.ts=ts
```

A `transform` sees one input and no extension, so it takes a single loader name
instead:

```bash
echo 'const x: number = 1' | guchho transform --loader=ts
```

### JSX in a `.js` file

JSX is not part of the `js` grammar. A `.js` file that contains markup is a parse
error, and the diagnostic names the fix:

```text
entry.js: ERROR: The JSX syntax extension is not currently enabled
NOTE: The guchho loader for this file is currently set to "js" but it must be
set to "jsx" to be able to parse JSX syntax. You can use '--loader:.js=jsx' to
do that.
```

Either move the file to `.jsx` or opt the extension in:

```bash
guchho build src/app.js --loader:.js=jsx
```

The loader map is per extension, so turning JSX on for `.js` also applies to
every other `.js` file in the graph. That is usually what a project that writes
components in `.js` wants; a project that only has one stray file is better off
renaming it.

### JSON modules

The `json` loader parses the file and exposes the value. A default import gets
the whole value, and a top-level key may be imported by name:

```js
import pkg from './package.json';
import { version } from './package.json';

console.log(pkg.name, version);
```

Nested keys are not named exports. Invalid JSON is a parse diagnostic, not a
runtime surprise.

## TypeScript

TypeScript support is deliberately split between things that disappear and
things that become code. The split is worth knowing before reading the output of
a build, because only the second group changes the size of the file.

### What is erased

These constructs are understood by the parser and then removed:

- type aliases, interfaces and annotation-only declarations,
- type annotations on variables, parameters, fields and return values,
- generic type parameters and `implements` clauses,
- `as` and `satisfies` expressions, which keep their left-hand value,
- access modifiers and the `readonly` modifier,
- `declare` declarations and ambient blocks,
- `import type` / `export type` statements, and the `type` modifier on a named
  import or export.

Erasing an import is not the same as removing a statement that has side effects.
`import type` is a declaration that nothing at runtime can use; a plain import
that happens to be used only in a type position is removed when the compiler can
prove that the binding is unused.

```ts
// input
import type { Config } from './config';
import { version } from './package';

const config = { version } satisfies Config;

export type { Config };
```

```js
// output, roughly
import { version } from './package';

const config = { version };
```

The import of `./config` is gone because the only thing it provided was a type.
`import type` makes that intent explicit, which is why it is the safer spelling
in files that mix types and runtime values.

### What becomes runtime code

Some TypeScript constructs have runtime behaviour and therefore cannot simply
disappear:

- **Enums** become an object with a forward mapping from name to value and a
  reverse mapping from value to name. Constant members may be folded at their
  use sites.
- **Namespaces** become an immediately-invoked function that receives or creates
  the namespace object. The exact wrapper depends on the target.
- **Parameter properties** (`constructor(public x: number)`) become assignments
  in the constructor, with the corresponding field declarations.
- **Decorators** are preserved for the standard decorator syntax. Legacy
  TypeScript decorators, including parameter decorators, require
  `"experimentalDecorators": true` in `tsconfig.json`.
- **Class fields** follow `useDefineForClassFields` from `tsconfig.json`; the
  setting decides whether a field is defined with assignment or `define`
  semantics.

An enum is the clearest example:

```ts
// input
enum Color {
  Red,
  Green,
}

const chosen = Color.Green;
```

```js
// output, roughly
var Color = /* @__PURE__ */ ((Color) => {
  Color[(Color['Red'] = 0)] = 'Red';
  Color[(Color['Green'] = 1)] = 'Green';
  return Color;
})(Color || {});

const chosen = 1 /* Green */;
```

The object still exists for code that looks a member up by name at runtime. A
constant member whose value is known at compile time can be folded to a literal
at its use site, while the object remains available to anything the compiler
cannot see through.

A parameter property is smaller but follows the same rule:

```ts
// input
class Point {
  constructor(public x: number, private y: number) {}
}
```

```js
// output, roughly
class Point {
  constructor(x, y) {
    this.x = x;
    this.y = y;
  }
  x;
  y;
}
```

Nothing type-level survives, but both fields still exist because the
constructor wrote them.

### `tsconfig.json`

`tsconfig` names a compiler configuration file. When it is not set, Guchho also
reads a `tsconfig.json` it finds while resolving a directory, so a normal
project usually gets its settings without writing anything extra.

```bash
guchho build src/index.ts --tsconfig=tsconfig.build.json --outdir=dist
```

```js
await build({
  entrypoints: ['src/index.ts'],
  outdir: 'dist',
  tsconfig: 'tsconfig.build.json',
});
```

The settings that affect emitted code are:

| `compilerOptions` | Effect |
| --- | --- |
| `target` | Contributes a language level when no target was chosen another way. |
| `baseUrl`, `paths` | Module path mappings used during resolution. |
| `jsx` | Selects the JSX mode: `react`, `react-jsx`, `react-jsxdev`, `preserve` or `react-native`. |
| `jsxFactory`, `jsxFragmentFactory` | Classic JSX factory and fragment names. |
| `jsxImportSource` | Module the automatic runtime imports from. |
| `experimentalDecorators` | Accepts legacy TypeScript decorators and parameter decorators. |
| `useDefineForClassFields` | Chooses `define` or assignment semantics for class fields. |
| `importsNotUsedAsValues`, `preserveValueImports`, `verbatimModuleSyntax` | Control whether type-only imports are dropped, kept or required to be explicit. |
| `strict`, `alwaysStrict` | Contribute the strict-mode assumption used while parsing. |
| `extends` | Inherits settings from another configuration before applying this one. |

`tsconfig` is about syntax and resolution. It does not turn Guchho into a type
checker, and it does not make a missing runtime value appear. A path mapping
such as `"@/*": ["./src/*"]` changes what an import resolves to; it does not
rewrite the import text in a way that would work outside the build.

The target interaction is easy to miss. A `target` in `tsconfig.json` is a
language level like any other, and an explicit `--target` or API `target` takes
precedence. When a build seems to emit syntax you did not expect, check both
places before assuming the printer ignored the target.

### `.ts`, `.tsx`, `.mts` and `.cts`

The extension chooses what the parser expects:

- `.ts` is TypeScript without markup.
- `.tsx` is TypeScript with JSX. That also means a type assertion written with
  angle brackets is not available; use `as` instead.
- `.mts` and `.cts` are TypeScript with explicit module intent, matching `.mjs`
  and `.cjs`.

A file that contains both type annotations and markup must be `.tsx` or be given
the `tsx` loader. Putting markup in a `.ts` file is a parse error, not a silent
fallback.

## JSX

The `jsx` option selects how markup becomes calls. All three modes understand
the same markup; they differ in what they emit.

| Value | Output |
| --- | --- |
| `transform` (default) | Classic calls through `jsxFactory` / `jsxFragment`. |
| `automatic` | Imports `jsx`, `jsxs` and `Fragment` from `jsxImportSource`. |
| `preserve` | Leaves the markup untouched for a later tool. |

### Classic transform

```jsx
// input
const view = <Button label="Save" />;
```

```js
// output, roughly, with jsx: transform
const view = /* @__PURE__ */ React.createElement(Button, {
  label: 'Save',
});
```

`React.createElement` is the default factory and `React.Fragment` is the default
fragment. A project using a different runtime names its own:

```bash
guchho build src/app.jsx --loader:.jsx=jsx \
  --jsx-factory=h --jsx-fragment=Fragment
```

The pure annotation tells tree shaking that constructing the element has no side
effect beyond producing a value. That is what allows an unused element to be
removed.

### Automatic runtime

```jsx
// input
const view = <Button label="Save" />;
```

```js
// output, roughly, with jsx: automatic
import { jsx } from 'react/jsx-runtime';

const view = /* @__PURE__ */ jsx(Button, { label: 'Save' });
```

The automatic runtime does not need the factory in scope. It imports the helper
it needs, which is why a module that only contains JSX can be compiled without a
`React` import. The import source is configurable:

```bash
guchho build src/app.jsx --loader:.jsx=jsx \
  --jsx=automatic --jsx-import-source=preact
```

That produces an import from `preact/jsx-runtime`.

`jsxDev` selects the development runtime, which appends source information to
each element:

```bash
guchho build src/app.jsx --loader:.jsx=jsx --jsx=automatic --jsx-dev
```

The automatic runtime makes a file a module because it inserts an import. In a
file that otherwise looks like a script, that changes strict-mode behaviour; the
engine reports the usual strict-mode error with a note pointing at the JSX
element that caused the implicit import.

### Preserve

`preserve` leaves the markup as written. The output is not runnable JavaScript on
its own; it is input for another compiler, a framework loader, or a step that
runs after Guchho.

```bash
guchho build src/app.jsx --loader:.jsx=jsx --jsx=preserve
```

This is also the mode to use when the markup is data, not code, and the next
tool knows how to interpret it.

### Per-file pragmas

A file can override the project setting with a comment:

```jsx
// @jsxRuntime automatic
// @jsxImportSource preact

const view = <Button />;
```

The recognized pragmas are `@jsxRuntime`, `@jsxImportSource`, `@jsx` and
`@jsxFrag`. They are useful for a vendored component that expects a different
runtime than the rest of the project, or for a single file that must stay in
classic mode while the project moves to automatic.

The pragmas enforce their own consistency. `@jsx` and `@jsxFrag` are warnings
under the automatic runtime, and `@jsxImportSource` without `@jsxRuntime
automatic` is a warning rather than a silent no-op. The message names the
pragmas that conflict, so the fix is visible in the diagnostic.

### `jsxSideEffects`

An element that is never used is normally removable. `jsxSideEffects` says that
creating the element is itself observable, so the call must stay:

```bash
guchho build src/app.jsx --loader:.jsx=jsx --jsx-side-effects
```

Use it when the factory has a side effect the compiler cannot see. It makes the
output larger and tree shaking more conservative, so it is not a switch to leave
on.

### Which loader enables JSX?

The `jsx` option chooses the transform; it does not enable parsing. Parsing is
the loader's job, so a file must be `.jsx`, `.tsx`, or given a loader such as
`--loader:.js=jsx`. A `.ts` file cannot contain markup even when `jsx` is set.
The diagnostic for a `.js` file explains this explicitly, which is why the
loader map is the first thing to check when a build fails on a `<`.

## Targets and engines

`target` names a language edition (`es2020`, `esnext`, …); `engines` names the
browsers and runtimes you support and lets the engine compute the level. Syntax
or APIs above the target are lowered automatically. See
[Targets & browsers](targets-and-browsers.md).

A target is a promise about syntax, not a polyfill. It can turn an arrow into a
`function`, but it cannot add `Array.prototype.at` to a browser. If the output
uses an API the target runtime lacks, provide the polyfill separately.

```bash
guchho build src/app.ts --target=es2020 --outdir=dist
guchho build src/app.ts --target=chrome115,safari16.4 --outdir=dist
```

TypeScript erasure happens before lowering, so a type-only construct never
triggers a target diagnostic. A runtime construct such as an enum does, because
by the time lowering runs it is ordinary JavaScript.

## Compile-time substitution

`define` replaces references with an expression at build time. The key is the
name to replace; the value is JavaScript expression text:

```js
build: {
  define: {
    __DEV__: 'false',
    __VERSION__: '"1.2.3"',
    'process.env.NODE_ENV': '"production"',
  },
}
```

The quoting is part of the value. `__DEV__: 'false'` inserts the literal
`false`; `__VERSION__: '"1.2.3"'` inserts the string `"1.2.3"`. Writing
`__VERSION__: '1.2.3'` would insert the expression `1.2.3`, which is invalid
JavaScript. The same values are spelled as flags on the command line:

```bash
guchho build src/app.js --outdir=dist \
  --define:__DEV__=false \
  --define:__VERSION__=1.2.3
```

A dotted path is matched as a path, not as text. `process.env.NODE_ENV` replaces
that member expression, and `import.meta.env.MODE` works the same way. A more
specific key wins over a less specific one, and a prefix that is not a complete
path is not blindly string-replaced.

A define changes references, not declarations or property names. Defining
`config` does not rename a function called `config`, and defining `mode` does not
rewrite `settings.mode`. It also does not make an assignment meaningful:
assigning to a name configured as a constant is reported as suspicious, because
the value the assignment writes is about to be replaced again.

```js
// input
if (process.env.NODE_ENV === 'production') {
  enableAnalytics();
}
```

```js
// roughly, with the define above and minification
enableAnalytics();
```

The comparison is no longer a comparison, so the branch is statically true and
can be folded away. Tree shaking and syntax minification are what perform that
removal; `define` only supplies the constant. See
[Tree shaking](tree-shaking.md) and [Minification](minification.md).

`pure` lists functions whose calls may be dropped or reordered when the result is
unused, the programmatic form of a `/* @__PURE__ */` annotation:

```bash
guchho build src/app.js --bundle --pure:createConfig
```

`keepNames` preserves function and class names while minifying, which matters
when stack traces or reflection read them. It costs bytes and can conflict with
lowering on an old target; see [Minification](minification.md#keeping-names).

## Optional lint checks

Guchho can report two families of suspicious-but-valid code during a build:
references to names that are not defined (`undefined`) and declarations that are
never read (`unused`). Both are off by default, because a build should not start
complaining about code that used to compile.

```js
// guchho.config.js
export default {
  build: {
    lint: {
      undefined: 'warning',
      unused: 'error',
    },
  },
};
```

The `lint` field accepts several shapes:

| Value | Meaning |
| --- | --- |
| `true` / `false` | Enable every check at `warning`, or turn them all off. |
| `'undefined'` | Enable one check at `warning`. |
| `['undefined', 'unused']` | Enable the named checks at `warning`. |
| `{ undefined: 'error', unused: 'warning' }` | Set a level per check. |

The finer-grained names are `unused-variable` and `unused-parameter`, which
split `unused` into its two halves. A level is `off`, `warning` or `error`; an
error-level finding fails the build, while a warning is reported and the build
still succeeds.

```js
build: {
  lint: {
    undefined: 'error',
    'unused-parameter': 'warning',
  },
}
```

These are parser-level checks. They run on the syntax tree the build already
built, which is why they are cheap enough to leave off until wanted. They do not
apply autofixes and they do not discover files. For the full rule set, safe
fixes, include/exclude patterns and a dedicated configuration file, use
[`guchho lint`](../reference/cli/lint.md). The build-time fields are listed in
the [build options reference](../reference/config/build.md#optional-lint-checks).

## Compiler stages

The lexer, parser and printer are exposed through the
[JavaScript API](../api/compiler.md), so the engine's own stages can be embedded
in other tools. The AST never crosses the API boundary as a JavaScript object; a
parse returns a handle and a summary, and a print turns a handle back into text.
That keeps the parser native while still letting a caller inspect the tree.

For a one-off transformation, `guchho transform` is the same pipeline with a
single input and no output tree:

```bash
guchho transform --loader=ts --target=es2019 < src/index.ts
```

## Common pitfalls

- **Expecting type checking.** Guchho erases types and reports syntax errors. It
  does not compare types or emit `.d.ts` files.
- **Putting JSX in a `.js` or `.ts` file.** The `jsx` option selects a transform,
  not a parser. Change the extension or add a loader entry.
- **Forgetting the quotes in a define value.** The value is expression text, so a
  string needs quotes inside the string.
- **Assuming a target polyfills APIs.** Targets lower syntax. A missing runtime
  API needs a polyfill.
- **Mixing JSX runtimes.** Classic factories and the automatic runtime are
  different contracts; `@jsxImportSource` without `@jsxRuntime automatic` is
  reported for that reason.
- **Ignoring `tsconfig` precedence.** An explicit `target` wins over a
  `tsconfig` target, and a nearby `tsconfig.json` can contribute settings you
  did not write yourself.

## See also

- [Targets & browsers](targets-and-browsers.md) - lowering and feature support.
- [Assets & loaders](assets-and-loaders.md) - the full loader table.
- [Tree shaking](tree-shaking.md) - removing unused code, and `pure`.
- [Minification](minification.md) - names, syntax and property mangling.
- [Configuration](configuration.md) - `define`, `lint` and config file shapes.
- [Compiler API](../api/compiler.md) - lex, parse and print directly.
- [`guchho lint`](../reference/cli/lint.md) - the standalone linter.
