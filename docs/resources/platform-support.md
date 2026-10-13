---
title: Platform support
description: The operating systems and architectures that ship a prebuilt Guchho binary.
---

# Platform support

Guchho is distributed as an npm package that depends on one prebuilt binary
package per platform. Installing `guchho` installs only the binary for the
machine you are on; there is no compile step.

| Operating system | Architectures |
| --- | --- |
| Linux | `x64`, `arm64`, `arm`, `ia32`, `ppc64`, `s390x`, `riscv64`, `loong64`, `mips64el` |
| macOS (darwin) | `x64`, `arm64` |
| Windows | `x64`, `arm64`, `ia32` |
| Android | `arm64`, `arm`, `x64` |
| FreeBSD | `x64`, `arm64` |
| OpenBSD | `x64`, `arm64` |
| NetBSD | `x64`, `arm64` |
| OpenHarmony | `arm64` |
| AIX | `ppc64` |
| SunOS | `x64` |

The npm packages are named `@guchho/<os>-<arch>` - for example
`@guchho/linux-x64` or `@guchho/darwin-arm64`. All of them are versioned in lock
step with the main package.

## Requirements

- Node **18 or newer**, for the npm package and its launcher. The engine binary
  itself is self-contained.
- A platform without a prebuilt binary can be supported by building from source;
  see
  [CONTRIBUTING.md](https://github.com/guchho/guchho/blob/main/CONTRIBUTING.md).

## Checking your install

```bash
guchho --version
guchho info
```

`guchho info` reports the version and environment, which is what a bug report
should include when the problem may be platform-specific.
