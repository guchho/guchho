---
title: guchho lint
description: Reference for the standalone JavaScript and TypeScript linter.
---

# `guchho lint`

```text
Usage: guchho lint [files...] [options]
```

Lint JavaScript and TypeScript without producing a build. With no paths, every
supported source file under the working directory is linted, subject to the
configured include and exclude patterns.

`guchho lint` is separate from the optional checks enabled by `build.lint`.
Build linting reports parser-level `undefined` and `unused` findings; this
command runs the rule set below, applies safe fixes, and has its own
configuration file.

The old `--lint` and `--lint-level` build flags have been removed. Use the
dedicated command instead:

```bash
# before
guchho build --lint=undefined,unused

# after
guchho lint
guchho lint --rule no-undef:error --rule no-unused-vars:warning
```

Build-time `build.lint` configuration is unchanged.

## Options

| Option | Meaning |
| --- | --- |
| `--fix` | Apply safe fixes to the files on disk. |
| `--fix-dry-run` | Report the fixes that would be applied without writing them. |
| `--watch` | Re-run when source or configuration files change. |
| `--quiet` | Print errors only. Warning counts are still reported and still count towards `--max-warnings`. |
| `--format=<format>` | `human` (default), `stylish`, or `json`. |
| `--json` | Alias for `--format=json`. |
| `--max-warnings=<n>` | Fail when there are more than `n` warnings. |
| `--rule <name>:<level>` | Override one rule. Repeatable. `--rule=<name>:<level>` is also accepted. |
| `--config <file>` | Use an explicit configuration file. |
| `--` | Treat every following argument as a path. |
| `--help` | Print command help without reading a project or config. |

A rule level is `off`, `warning` (or `warn`), or `error`; the numbers `0`, `1`
and `2` are accepted too. A missing value, an unknown rule, an invalid level, or
an invalid format is a usage error.

## Configuration

Configuration is read from the nearest of these files, searching upward from
the working directory:

1. `guchho.lint.config.js`
2. `guchho.lint.config.mjs`
3. `guchho.lint.config.cjs`
4. `guchho.lint.config.json`

JavaScript configuration files are evaluated by Node. Set `GUCHHO_NODE_BIN` to
use a specific Node binary.

```js
// guchho.lint.config.mjs
export default {
  include: ['src/**/*.{js,jsx,ts,tsx}'],
  exclude: ['**/*.test.js'],
  platform: 'browser',
  rules: {
    'no-console': 'error',
    'no-unused-vars': 'warn',
  },
  globals: {
    console: 'readonly',
  },
}
```

The JSON form uses the same field names.

| Field | Meaning |
| --- | --- |
| `include`, `files` | Patterns to lint. Aliases. |
| `exclude`, `ignores` | Patterns to skip. Aliases. |
| `extends` | A config path or array of paths loaded before this one. Later files win. |
| `rules` | Rule name to level. A rule may also be written as `[level]`. |
| `globals` | Names known to the runtime. Values are `true`/`"writable"` or `false`/`"readonly"`/`"off"`. |
| `platform` | `browser` (default), `node`, or `neutral`. |
| `languageOptions.sourceType` | `module` only; `script` and `commonjs` are rejected because module kind is inferred from the file extension. |
| `languageOptions.ecmaVersion` | `"latest"` only. |
| `reportUnusedDisableDirectives` | `false` is accepted; `true` is rejected because disable comments are not interpreted. |

Precedence is built-in defaults, then `extends` from the base outward, then the
current file, then `--rule` overrides from the command line.

Rules are matched against paths relative to the working directory. `*` matches
within one path segment, `**` matches across segments, `?` matches one
character, `[abc]` and `[a-z]` are character classes, and `{a,b}` expands to
alternatives. Unsupported glob syntax is an error, not a literal path.

The parser has no `overrides` form; use `include`/`exclude` on separate configs
or path patterns instead. Unknown fields and invalid values are reported with
the config path and are never silently ignored.

## Rules

| Rule | Default | Safe fix | Reports |
| --- | --- | --- | --- |
| `no-undef` | `error` | | References with no visible binding and no configured global. |
| `no-unused-vars` | `warning` | | Unused local variables and parameters. |
| `no-redeclare` | `error` | | Conflicting declarations in the same scope. |
| `no-use-before-define` | `off` | | Reads that precede the declaration that resolves them. |
| `no-shadow` | `off` | | Declarations that shadow an enclosing declaration. |
| `no-unreachable` | `error` | | Statements that cannot be reached after a terminator. |
| `no-duplicate-imports` | `error` | | The same module imported more than once. |
| `no-constant-condition` | `warning` | | Conditions whose truth value is constant. |
| `valid-typeof` | `error` | | `typeof` compared with an unknown type string. |
| `no-console` | `off` | | Calls and constructions of `console`. |
| `eqeqeq` | `warning` | Yes | `==` and `!=` comparisons. |
| `no-debugger` | `error` | Yes | `debugger` statements. |
| `no-duplicate-case` | `error` | | Duplicate `switch` cases. |
| `no-empty` | `warning` | | Empty blocks and statements where they are likely a mistake. |
| `import/no-unresolved` | `error` | | Imports that the resolver cannot find. |

Safe fixes are limited to `eqeqeq` and `no-debugger`. `eqeqeq` only offers a
fix when both sides are literals of the same type or one side is `typeof`, so
the change cannot alter coercion behaviour. Other `eqeqeq` findings are
reported without a fix.

## Output

Human output prints one line per diagnostic:

```text
src/app.js:3:7: error "missing" is not defined (no-undef)
```

The summary reports problem, error, warning, and fix counts. `--quiet` hides
warning lines but keeps their counts. JSON output contains a `files` array with
per-file `messages` and counts, a flat `messages` array, and top-level
`errorCount`, `warningCount`, `fixCount`, and `filesWithProblems` fields. Each
message also carries its own `fixCount`. JSON mode writes no banners or human
summaries to stdout.

Paths are printed relative to the working directory with forward slashes, and
files and diagnostics are sorted deterministically.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | No errors and no warning threshold exceeded. |
| `1` | At least one lint error, or warnings greater than `--max-warnings`. |
| `2` | Usage, configuration, or operational failure. |
| `130` | Interrupted in watch mode. |

Source files are never modified unless `--fix` is passed. `--fix-dry-run` never
writes.

## Limitations

- TypeScript types are erased by the parser; rules analyse the resulting
  JavaScript and do not perform type checking.
- `no-use-before-define` and `no-shadow` are conservative and report only cases
  that scope analysis can establish reliably.
- Module resolution uses Guchho's resolver and does not emit build artifacts or
  require network access.
- Module kind is inferred from the file extension and parsing always uses the
  parser's latest ECMAScript support. Values that would require different
  behaviour (`sourceType: "script"`/`"commonjs"` or a numeric `ecmaVersion`) are
  rejected instead of being silently ignored.
- Disable comments are not interpreted. `reportUnusedDisableDirectives: false`
  is accepted; `true` is rejected rather than silently ignored.

## See also

- [CLI reference](README.md)
- [Configuration guide](../../guide/configuration.md)
- [Build lint checks](../config/build.md#optional-lint-checks)
