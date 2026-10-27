---
title: Targets & browsers
description: Choosing the language level the output must stay within.
---

# Targets & browsers

The output must stay within a language level. Syntax above it is lowered where
the engine knows how to lower it; everything else passes through untouched, so
the target doubles as a promise about the emitted file.

The promise is about **syntax**, not about runtime APIs. A target can turn an
arrow function into a `function`, but it cannot add `Array.prototype.at` to a
browser that does not have it. If the output uses an API the target runtime
lacks, that API still needs a polyfill or a different implementation.

## Language target

```bash
guchho build src/app.js --target=es2020
```

`target` names a single edition: `es5`, `es2015` … `es2025`, or `esnext` for
everything the parser understands. `default` means no lowering based on a
language level.

The edition names are the ECMAScript years. `es6` is accepted as another
spelling of `es2015`, because it is what many people still type.

| Target | What it commonly means |
| --- | --- |
| `default` | No language level was requested; only explicit settings lower syntax. |
| `es5` | No `let`/`const`, arrow functions, classes, template literals or modules in the output. |
| `es2015` / `es6` | Those ES2015 constructs are allowed; newer syntax is lowered. |
| `es2016` … `es2021` | That edition's syntax is allowed; newer syntax is lowered. |
| `es2022` | Class fields, private members, static blocks and top-level `await` are allowed. |
| `es2023` … `es2025` | The newer editions are allowed. Much of what they add is library API rather than syntax, and a target does not polyfill libraries. |
| `esnext` | Everything the parser understands; no lowering for an older edition. |

`esnext` and `default` can look similar because both leave modern syntax alone.
They are still different statements: `esnext` says "the newest edition is the
target", while `default` says "no language-level target was chosen". The
distinction matters when another option, such as `tsconfig`, contributes a
language level of its own.

### What lowering looks like

An arrow function and a template literal are ES2015 syntax. Under `es5` they
must become a function and string concatenation:

```js
// input
const greet = (name) => `Hello, ${name}!`;
```

```js
// roughly, with --target=es5
var greet = function (name) {
  return "Hello, " + name + "!";
};
```

Optional chaining is ES2020. Under `es2019` it becomes a chain of null checks:

```js
// input
const city = user?.address?.city;
```

```js
// roughly, with --target=es2019
var _user$address;
const city =
  user === null || user === void 0
    ? void 0
    : (_user$address = user.address) === null || _user$address === void 0
      ? void 0
      : _user$address.city;
```

Class fields are ES2022. Under `es2021` a field becomes an assignment in the
constructor:

```js
// input
class Counter {
  count = 0;
}
```

```js
// roughly, with --target=es2021
class Counter {
  constructor() {
    this.count = 0;
  }
}
```

The exact output is the printer's business, and it may be more compact than the
sketches above. What matters is the contract: after the build, the output does
not contain syntax newer than the target.

Not every feature can be lowered. When a construct has no equivalent at the
requested level, the build reports a diagnostic instead of emitting syntax the
target cannot parse.

## Engines

Instead of naming an edition, name the engines you actually test on and let the
engine compute the level:

```bash
guchho build src/app.js --target=firefox115,safari16.4
```

Each entry pairs an engine with a version. A feature is allowed only when
**every** listed engine supports it, so the target describes the common
capability of the whole list. Adding a browser narrows the output; it never
widens it.

Supported engine names: `chrome`, `deno`, `edge`, `firefox`, `hermes`, `ie`,
`ios`, `node`, `opera`, `rhino`, `safari`.

```bash
# One engine
guchho build src/app.js --target=chrome80

# A desktop and a mobile engine
guchho build src/app.js --target=chrome115,ios16

# A Node service
guchho build src/server.js --target=node18 --platform=node

# Mix a language floor with engines
guchho build src/app.js --target=es2019,chrome80,safari14
```

A version is read as `major[.minor[.patch]]` with an optional pre-release suffix
and compared component by component, so `100.0.0` is newer than `90.0.0`. A
version that does not parse - `latest`, for instance - becomes an error rather
than a silent guess. A missing version after an engine name is an error too:

```bash
guchho build src/app.js --target=firefox      # error: no version
guchho build src/app.js --target=firefox115   # ok
```

Engine names are matched without regard to ASCII case, so `Firefox115` and
`firefox115` name the same target.

### Why a version, not a name

Browser feature support changes over time, and an engine name alone says
nothing about which features exist. `safari16.4` is a different target from
`safari17`. The version is what lets the compatibility table answer the only
question that matters: does this specific release support the construct the
source uses?

If you do not know the exact version, pick the oldest version you intend to
support. That is the version your users may still have.

## Mixing language levels and engines

A target string may mix language levels and engines in one comma-separated list:

```bash
guchho build src/app.js --target=es2020,firefox115
```

The language level and the engines are combined. A feature is allowed only if
both the edition and every named engine allow it. That is useful when you have a
policy ("never emit anything newer than ES2020") and a practical matrix ("and it
must run on these two browsers").

In the JavaScript API, an array is the equivalent spelling:

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  target: ['es2020', 'firefox115', 'safari16.4'],
});
```

## The escape hatch

`supported` turns one specific feature on or off regardless of `target` and
`engines`:

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  supported: { 'async-await': false },
});
```

A `true` value says "the target may use this feature even if the table would
have forbidden it". A `false` value says "treat this feature as unsupported and
lower it, even if the target would have allowed it".

This is for the cases a version table cannot know about: an engine with a
backported feature, a project that compiles for a runtime with a known gap, or a
test that wants to force a lowering path.

```js
await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  target: 'es2019',
  supported: {
    'optional-chain': true, // the runtime has it despite ES2019
    'class-field': false,   // force constructor assignments
    nesting: false,         // lower CSS nesting as well
  },
});
```

On the command line each claim is a repeatable flag:

```bash
guchho build src/index.html --outdir=dist --target=es2019 \
  --supported:optional-chain=true \
  --supported:class-field=false \
  --supported:nesting=false
```

Feature names are lowercase and hyphenated. They cover both JavaScript and CSS.
A sample of the JavaScript names:

| Name | Feature |
| --- | --- |
| `arrow` | Arrow functions |
| `async-await` | `async` / `await` |
| `class` | Class declarations and expressions |
| `class-field` | Class fields |
| `class-private-field` | `#private` fields |
| `class-static-blocks` | `static { ... }` blocks |
| `const-and-let` | `const` and `let` |
| `destructuring` | Array and object destructuring |
| `for-of` | `for...of` loops |
| `generator` | Generator functions |
| `import-meta` | `import.meta` |
| `logical-assignment` | `&&=`, `||=`, `??=` |
| `nullish-coalescing` | `??` |
| `object-rest-spread` | `{ ...rest }` and object spread |
| `optional-chain` | `?.` |
| `optional-catch-binding` | `catch {}` without a binding |
| `regexp-lookbehind-assertions` | Regular-expression lookbehind |
| `regexp-named-capture-groups` | Named capture groups |
| `template-literal` | Template literals |
| `top-level-await` | `await` at module scope |

The CSS table includes names such as `nesting`, `is-pseudo-class`,
`color-functions`, `modern-rgb-hsl`, `hwb`, `hex-rgba` and `inset-property`.

A name that matches neither table is reported as an error rather than ignored,
because it can only be a typo:

```text
"optional-chian" is not a valid feature name for the "supported" setting
```

## CSS targets

The same target applies to CSS. A stylesheet using nesting, `@container`,
modern colour syntax or other newer features is lowered or reported according to
the target in the same way JavaScript is:

```bash
guchho build src/style.css --target=safari15 --outdir=dist
```

A stylesheet-only build is useful when the output is consumed by another tool or
when only the CSS needs processing. The target still describes what syntax the
result may contain.

## Charset

`charset` decides how non-ASCII characters are escaped. `default` resolves to
`ascii`, which escapes every non-ASCII character as `\uXXXX` so the file
survives any encoding chain. `utf8` emits them as-is when the output charset
allows it.

```js
// input
const city = 'Zürich';
```

With the default `ascii` charset:

```js
var city = "Z\u00fcrich";
```

With `charset: 'utf8'`:

```js
var city = "Zürich";
```

The ASCII form is the default because it is safe in every transport, editor and
server configuration: a file that is pure ASCII cannot be mis-decoded. The UTF-8
form is smaller and easier to read when the whole chain is known to be UTF-8.

```bash
guchho build src/app.js --charset=utf8
```

Some identifiers cannot be represented in ASCII at all. In that case the build
reports that the name cannot be escaped and points at `charset: 'utf8'` as the
solution:

```js
const café = 1;
```

With `ascii`, this is a diagnostic; with `utf8`, the identifier is written as
written. The same setting applies to CSS output.

## Practical recipes

### A modern application

```bash
guchho build src/index.html --outdir=dist --target=es2020
```

`es2020` is a useful floor for evergreen browsers: optional chaining, nullish
coalescing and dynamic `import` are allowed, while newer syntax is lowered.

### A specific browser matrix

```bash
guchho build src/index.html --outdir=dist \
  --target=chrome115,firefox115,safari16.4
```

Name the versions you test. The build will not emit syntax that any of them
lacks, and the output is a direct expression of the support policy.

### A library

```bash
guchho build src/index.ts --outdir=dist --format=esm --target=es2020
```

A library can usually choose a newer floor than an application, because its
consumers run their own build. A lower floor is safer for a package that will be
used by projects with older targets.

### Node

```bash
guchho build src/server.js --outdir=dist --platform=node --target=node18
```

The `node` engine affects feature support, and `--platform=node` affects how
imports and globals are treated. They are separate choices: a Node platform can
still target an older Node syntax level.

### Force a lowering path

```bash
guchho build src/app.js --outdir=dist \
  --target=esnext \
  --supported:optional-chain=false
```

This says "the output may use the newest syntax, except that optional chaining
must be lowered". It is a way to test a transform or to work around a runtime
bug without lowering the whole language level.

## Limits and diagnostics

The target system has three boundaries worth knowing:

- **It lowers syntax, not behaviour.** A polyfill is still required for a
  missing runtime API. `target` cannot make `Promise.withResolvers` exist in an
  old browser.
- **It uses the support table the engine ships with.** A browser that adds a
  feature early, or a runtime with a vendor-specific gap, may need
  `supported`.
- **It reports what it cannot lower.** A successful build means the output is
  within the target; a construct with no lower form is an error rather than a
  file that claims to be compatible and is not.

If a build succeeds but the output still contains syntax you did not expect,
check for a second setting that widened the target: `tsconfig`, a config file
loaded from a parent directory, or a `--target` flag that overrode the one you
meant to use. `guchho info` prints the configuration in effect.

See the [configuration reference](../reference/config/build.md) for the full
list.

## See also

- [Minification](minification.md) - size changes that are independent of the
  target.
- [Source maps](source-maps.md) - keeping the original source visible after
  lowering.
- [Deployment](deployment.md) - which target choices belong in a production
  build.
- [Platforms](../reference/platforms.md) - `browser`, `node` and `neutral`.
