---
title: guchho build
description: Reference for the production build command.
---

# `guchho build`

```text
Usage: guchho build [entry...] [options]
```

Production-oriented build. Processes HTML, JS, CSS and assets.

Every option below can also be set in `guchho.config.js`, `guchho.config.json`
or `guchho.json` next to the project. A flag on the command line wins over the
config file, and the config file wins over the built-in defaults.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--outdir=<dir>` | `dist` | Output directory. |
| `--outfile=<file>` | - | Output file. |
| `--minify` | off | Minify all output. |
| `--minify-whitespace` | - | Minify whitespace only. |
| `--minify-identifiers` | - | Mangle identifiers only. |
| `--minify-syntax` | - | Optimize syntax only. |
| `--minify-html` | off | Minify the markup. |
| `--pretty` | on | Indent the markup. |
| `--sourcemap` | - | Enable source maps. |
| `--target=<target>` | - | Target (`es2020`, `chrome80`, `node16`, …). |
| `--format=<format>` | - | `iife`, `cjs`, `esm`, `umd`, `amd`, `system`. |
| `--name=<name>` | - | Global namespace for `umd` and `iife` output. |
| `--exports=<value>` | - | Entry exports: `named`, `default`, `none`, `auto`. |
| `--platform=<p>` | - | Platform: `browser`, `node`, `neutral`. |
| `--splitting` | off | Enable code splitting. |
| `--tree-shaking` | off | Enable tree shaking. |
| `--bundle` | off | Bundle all imports. |
| `--external:<pkg>` | - | Mark a package as external. Repeatable. |
| `--define:<k>=<v>` | - | Replace an identifier with a value. Repeatable. |
| `--loader:<ext>=<l>` | - | Loader for a file extension. Repeatable. |
| `--watch` | off | Watch for changes. |
| `--metafile` | off | Write metafile JSON. |
| `--analyze` | off | Analyze bundle size. |
| `--jsx=<mode>` | - | `transform`, `preserve`, `automatic`. |
| `--tsconfig=<file>` | - | Path to `tsconfig.json`. |
| `--log-level=<level>` | `info` | `verbose`, `debug`, `info`, `warning`, `error`, `silent`. |
| `--color` / `--no-color` | auto | Force colour on or off. |
| `--quiet` | off | Minimal output. |
| `--verbose` | off | Detailed output. |

`--external`, `--define` and `--loader` take their value after a colon rather
than an equals sign, because they may be repeated and their value is itself a
name and a value.

## Examples

```bash
guchho build src/index.html --outdir=dist
guchho build src/app.js --minify --sourcemap
guchho build --watch
```

## See also

- [Configuration: build](../config/build.md)
- [`guchho lint`](lint.md)
- [Bundling & splitting](../../guide/bundling-and-splitting.md)
- [Minification](../../guide/minification.md)
