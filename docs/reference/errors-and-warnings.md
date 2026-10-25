---
title: Errors and warnings
description: The shape of a Guchho diagnostic.
---

# Errors and warnings

A diagnostic is a `Message`:

```ts
interface Message {
  id: string
  pluginName: string
  text: string
  location: Location | null
  notes: Note[]
  detail: any
}
```

```ts
interface Location {
  file: string
  namespace: string
  line: number        // 1-based
  column: number      // 0-based, in bytes
  length: number      // in bytes
  lineText: string
  suggestion: string
}

interface Note {
  text: string
  location: Location | null
}
```

`pluginName` is empty on a message the engine itself produced. `detail` is
whatever a plugin put there, passed through untouched.

## Where they appear

- A build result carries `errors` and `warnings` arrays.
- A failed build throws a [`BuildFailure`](../api/README.md), which carries the
  same two arrays.
- A failure to reach the service throws a [`ServiceError`](../api/README.md),
  which is about the installation rather than the code.

A build with warnings is a build that worked; `result.success` reflects the
errors list, not the warnings list.

## Lint diagnostics

Optional build-time lint checks are disabled by default and can be enabled
through `build.lint`. They report suspicious but syntactically valid code:

| ID | Default text |
| --- | --- |
| `undefined-variable` | `"<name>" is not defined` |
| `unused-variable` | `"<name>" is declared but never used` |
| `unused-parameter` | `"<name>" is declared but never used` |

Each ID can be silenced, promoted or demoted with the existing `log_override`
configuration. Undefined-name analysis is best-effort: it understands lexical
scopes, user defines and the configured output platform, and stays silent in
`with` scopes and files containing direct `eval`. Unused-parameter reporting is
off unless explicitly enabled. See the
[build options reference](config/build.md#optional-lint-checks).

The standalone [`guchho lint`](cli/lint.md) command reports a different set of
rule IDs (`no-undef`, `no-unused-vars`, `eqeqeq`, …) with its own configuration
and exit-code policy.

## See also

- [Exit codes](exit-codes.md)
- [JavaScript API](../api/README.md)
