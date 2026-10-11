---
title: Recipes
description: Short, complete patterns for common Guchho tasks.
---

# Recipes

Each page here is one task, solved end to end: the command or configuration,
the reasoning behind it, and the parts that surprise people. They are meant to
be copied and adapted rather than read in order.

For the exhaustive list of what a flag or a field does, follow the link on each
page into the [reference](../reference/README.md); these pages are about *when*
to reach for it.

| Recipe | You want to |
| --- | --- |
| [Scaffold a project](scaffold-a-project.md) | start from a working tree instead of an empty directory. |
| [Build a single-page app](build-a-single-page-app.md) | go from `index.html` to a deployable `dist/`. |
| [Build a library](build-a-library.md) | publish ESM and CommonJS with the dependencies left external. |
| [Serve and watch](serve-and-watch.md) | edit a file and see the page update, with the right command of the three. |
| [Analyze bundle size](analyze-bundle-size.md) | see what is big and why, without a second tool. |
| [Inject configuration at build time](inject-config-at-build-time.md) | replace an identifier such as `process.env.NODE_ENV` in the output. |
| [Mangle property names](mangle-property-names.md) | shorten `obj.longPropertyName` safely and keep it stable between builds. |
| [Transform single files](transform-single-files.md) | turn one source through stdin into stdout. |
| [Pack a distribution](pack-a-distribution.md) | produce a versioned archive of the output. |

## See also

- [Getting started](../guide/getting-started.md) for the guided path.
- [CLI reference](../reference/cli/README.md) and
  [configuration](../reference/config/README.md) for every flag and field.
