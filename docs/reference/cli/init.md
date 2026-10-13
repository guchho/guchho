---
title: guchho init
description: Reference for the project scaffolding command.
---

# `guchho init`

```text
Usage: guchho init [options]
```

Create a new Guchho project in the current directory. The command has no
positional argument: it writes into the working directory.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `-t`, `--template=<name>` | `basic` | Starter template. |
| `--type=<type>` | `app` | Project type. |
| `-y`, `--yes` | off | Use default options without prompting. |
| `-f`, `--force` | off | Overwrite existing scaffold files. |
| `-h`, `--help` | - | Show help. |

## Project types

| Type | Description |
| --- | --- |
| `app` | Web application |
| `lib` | Reusable JavaScript/TypeScript library |
| `plugin` | Guchho build-system plugin |

## Templates

| Template | Description |
| --- | --- |
| `basic` | JavaScript + CSS |
| `ts` | TypeScript + CSS |
| `jsx` | JavaScript + JSX + CSS |
| `tsx` | TypeScript + JSX + CSS |

Each template produces a single source file with the matching extension. The
project type and the template are independent choices.

## Examples

```bash
guchho init
guchho init --template=ts
guchho init --type=lib --template=ts
guchho init --type=plugin --template=ts
```

## Behaviour

- A target that does not exist is created.
- A target that exists and holds nothing this scaffold would have written is
  refused, unless `--force` is given.
- A file that is already there is left alone, unless `--force` is given.
- Nothing outside the generated set is ever removed.
