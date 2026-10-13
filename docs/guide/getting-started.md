---
title: Getting started
description: Install Guchho and produce your first build.
---

# Getting started

## Install

Guchho ships as a platform-specific prebuilt binary behind the `guchho` npm
package. The postinstall step copies the correct binary for your platform into
place.

```bash
npm install -g guchho
```

Other distributions:

```bash
choco install guchho     # Windows (Chocolatey)

# From source (any OS with CMake >= 3.25 and a C++20 compiler)
bash scripts/build.sh --preset release-linux-x64
```

See [Platform support](../resources/platform-support.md) for the full list.

## Create a project

```bash
npx guchho init -y
```

This writes a starter project - `src/index.html`, `src/main.js`, `src/style.css`
and `guchho.config.js`. Choose a different shape with `--type` and `--template`:

```bash
guchho init --type=lib --template=ts
guchho init --type=plugin --template=ts
```

## Develop

```bash
npx guchho dev
```

A development server with file watching and automatic rebuilds.

## Build

```bash
npx guchho build
```

Production build into `./dist`. Without a config file you can name everything on
the command line:

```bash
guchho build src/index.html --outdir=dist --format=esm --minify --sourcemap
```

Add scripts to `package.json`:

```json
{
  "scripts": {
    "dev": "guchho dev",
    "build": "guchho build",
    "preview": "guchho serve",
    "clean": "guchho clean"
  }
}
```

## Preview and package

```bash
npx guchho serve              # serve the built output
npx guchho pack dist -o release.zip
```

## Next

- [Configuration](configuration.md) - configure a build once.
- [CLI overview](cli-overview.md) - every command.
- [JavaScript API](../api/README.md) - use the engine from Node.
