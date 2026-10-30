# Contributing to Guchho

This guide explains how to build **Guchho** from source, run the test suite, and produce npm binaries. It covers the per-OS toolchain requirements, how to install them, and every build script - so you can build everything on your platform.


## 1. Project Overview

**Guchho** is a fast, native C++ build system and bundler for modern web applications, written in C++20 and built with **CMake**. It ships both as a source project and as platform-specific prebuilt binaries published to npm.

| Property            | Value                                            |
| ------------------- | ------------------------------------------------ |
| Language            | C++20 (C standard: C17)                          |
| Build system        | CMake (minimum version **3.25**)                 |
| Compilers           | MSVC, GCC, Clang                                 |
| Generators          | Ninja, Visual Studio (presets are generator-aware)|
| Testing             | CTest + header-only framework in `include/test`  |
| Packaging           | CPack (ZIP + NSIS on Windows, TGZ on macOS/Linux)|
| Current version     | 1.0.9                                            |
| Distribution        | npm (`guchho` + `@guchho/<platform>` binaries)   |

### Supported platforms

| Platform         | Native build | CMake presets                                                        |
| ---------------- | :----------: | -------------------------------------------------------------------- |
| Windows x64      | ✅           | `*-win32-x64` (Visual Studio 18 2026), `*-win32-x64-ninja` (Ninja)   |
| Linux x64        | ✅           | `*-linux-x64` (Ninja) + `asan-linux-x64`, `ubsan-linux-x64`          |
| Linux ARM64      | ✅           | `*-linux-arm64` (Ninja)                                              |
| macOS ARM64      | ✅           | `*-darwin-arm64` (Ninja)                                              |
| macOS x64        | ✅           | `*-darwin-x64` (Ninja)                                                |
| CI build         | ✅           | `ci` (inherits `release-win32-x64`, strict warnings)                  |

The npm distribution additionally ships prebuilt binary packages for many platforms
(AIX, Android, BSDs, Solaris, OpenHarmony, and all Linux/Windows architectures).
See [Section 5](#5-building-for-other-npm-distribution-platforms).


## 2. Common Requirements (All Operating Systems)

The following are needed on every OS:

- A C++20-capable compiler (MSVC, GCC, or Clang).
- **CMake ≥ 3.25** (includes `ctest` and `cpack`).
- A shell capable of running Bash scripts (`scripts/*.sh`). On Windows this means **Git Bash** or MSYS2. Alternatively run the raw `cmake`/`ctest` commands listed in [Section 7](#7-manual-cmake-commands).
- **Git** to clone the repository.

> Optional: **Ninja** is only required if you use a `*-ninja` preset or the Linux/macOS presets. It is auto-detected by CMake if the Ninja generator is selected.


## 3. Requirements by Operating System

### 3.1 Windows (x64)

#### Requirements

| Requirement     | Details                                                        |
| --------------- | -------------------------------------------------------------- |
| OS              | Windows 10 / 11 (64-bit)                                        |
| Shell           | Git Bash or MSYS2 (to run `scripts/*.sh` and `bash`)            |
| CMake           | ≥ 3.25 (`winget install Kitware.CMake`)                         |
| Compiler (pick) | **MSVC** via Visual Studio, or **MinGW-w64 GCC**                |
| Ninja           | Only for `*-ninja` presets (`winget install Ninja-build.Ninja`) |
| NSIS (optional) | Only needed to produce the Windows installer via CPack          |

#### Installing on Windows

Install CMake, Git, and Ninja with winget:

```bat
winget install Kitware.CMake
winget install Git.Git
winget install Ninja-build.Ninja
```

Install MSVC (recommended; supports the `Visual Studio 18 2026` generator used by the `*-win32-x64` presets):

```bat
winget install Microsoft.VisualStudio.2022.BuildTools
rem or full IDE:
winget install Microsoft.VisualStudio
```

> After installing Visual Studio Build Tools, open **"Developer Command Prompt for VS"** or run the CMake/`msbuild` commands from a prompt where `vcvarsall.bat` has been sourced so CMake can find MSVC.

Alternative - install MinGW-w64 GCC (used with the Ninja presets):

```bat
winget install mingw-win64.mingw64  --source winget
```

MinGW-w64 must provide a C++20-capable GCC (`g++`), which is satisfied by recent MinGW-w64 releases (GCC 11+).

#### Building on Windows

```bash
# Visual Studio generator (default for build.sh)
bash scripts/build.sh --preset release-win32-x64

# Ninja generator
bash scripts/build.sh --preset release-win32-x64-ninja
```

Binary output: `build/<preset>/bin/guchho.exe`


### 3.2 Linux (x64 and ARM64)

#### Requirements

| Requirement | Details                                                                 |
| ----------- | ----------------------------------------------------------------------- |
| OS          | Any modern Linux (Ubuntu/Debian tested via CI)                           |
| CMake       | ≥ 3.25 (`sudo apt install cmake`)                                        |
| Compiler    | **GCC** (`g++`, C++20) or **Clang** (`clang++`)                          |
| Ninja       | `sudo apt install ninja-build` (all Linux presets use Ninja)             |
| Git         | `sudo apt install git`                                                   |
| Make        | `build-essential` (provides GCC, `make`, and general build tools)         |

For Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git g++ clang
```

On other distros use the equivalent package manager:

```bash
# Fedora/RHEL
sudo dnf install -y gcc-c++ make cmake ninja-build git clang

# Arch
sudo pacman -S --needed base-devel cmake ninja git clang
```

#### Cross-compilation requirements (Linux variants)

Cross-building other Linux architectures requires the matching GCC cross toolchain. Packages below are for Debian/Ubuntu, as used in the CI workflow.

| Target platform | Cross toolchain packages                       |
| --------------- | ---------------------------------------------- |
| linux-arm       | `gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf` |
| linux-arm64     | `gcc-aarch64-linux-gnu g++-aarch64-linux-gnu`  |
| linux-ia32      | `gcc-multilib g++-multilib` (compile with `-m32`) |
| linux-s390x     | `gcc-s390x-linux-gnu g++-s390x-linux-gnu`      |
| linux-ppc64     | `gcc-powerpc64le-linux-gnu g++-powerpc64le-linux-gnu` |
| linux-riscv64   | `gcc-riscv64-linux-gnu g++-riscv64-linux-gnu`  |
| linux-mips64el  | `gcc-mips64el-linux-gnuabi64 g++-mips64el-linux-gnuabi64` |
| linux-loong64   | No GCC cross-compiler → use Docker + QEMU (image `ghcr.io/loong64/loongnix:25`, platform `linux/loong64`) |

#### Building on Linux

```bash
# x64
bash scripts/build.sh --preset release-linux-x64

# ARM64
bash scripts/build.sh --preset release-linux-arm64

# Sanitizers (AddressSanitizer / UndefinedBehaviorSanitizer)
bash scripts/build.sh --preset asan-linux-x64
bash scripts/build.sh --preset ubsan-linux-x64
```

Binary output: `build/<preset>/bin/guchho`


### 3.3 macOS (Apple Silicon ARM64 and Intel x64)

#### Requirements

| Requirement       | Details                                                      |
| ----------------- | ------------------------------------------------------------ |
| OS                | macOS 11+ (Apple Silicon ARM64 or Intel x64)                 |
| Xcode CLT         | `xcode-select --install` (provides Clang)                    |
| CMake             | ≥ 3.25 (`brew install cmake`)                                |
| Ninja             | `brew install ninja` (all macOS presets use Ninja)           |
| Git               | Bundled with Xcode CLT (or `brew install git`)               |
| Homebrew (optional)| Recommended package manager for cmake/ninja                 |

Installing:

```bash
xcode-select --install        # macOS developer tools (Clang)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake ninja git
```

> Note: On macOS the ARM64 and x64 presets explicitly set `CMAKE_OSX_ARCHITECTURES`, so an Intel Mac can only build the `x64` presets and an Apple Silicon Mac the `arm64` presets (no x64-on-arm emulation needed).

#### Building on macOS

```bash
# Apple Silicon (M1/M2/M3/M4)
bash scripts/build.sh --preset release-darwin-arm64

# Intel
bash scripts/build.sh --preset release-darwin-x64
```

Binary output: `build/<preset>/bin/guchho`


### 3.4 FreeBSD / OpenBSD / NetBSD / Solaris (SunOS) / AIX

FreeBSD has a `release-freebsd-x64` preset and can be built natively with:

```bash
bash scripts/build-npm.sh
```

OpenBSD, NetBSD, Solaris (SunOS) and AIX are not covered by CMake presets.
They are built ad-hoc with CMake and are consumed through the npm `@guchho/*`
prebuilt packages. Building from source requires:

- A C++20 compiler (Clang is the default on the BSDs).
- CMake ≥ 3.25.
- Ninja (or a Unix Makefiles generator).

```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_C_EXTENSIONS=OFF -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build build/release --parallel
```

Binary output: `build/release/bin/guchho`


## 4. Install the Toolchain - Quick Script

The quickest per-OS install one-liners:

```bash
# ----------------------------------------
# Windows (run in PowerShell)
# ----------------------------------------
winget install Kitware.CMake Git.Git Ninja-build.Ninja

# ----------------------------------------
# Debian / Ubuntu
# ----------------------------------------
sudo apt update && sudo apt install -y build-essential cmake ninja-build git g++ clang

# ----------------------------------------
# macOS
# ----------------------------------------
xcode-select --install
brew install cmake ninja git
```

Verify the toolchain:

```bash
cmake --version     # >= 3.25
ctest --version
ninja --version     # if using Ninja presets
g++ --version       # or clang++ --version
```


## 5. Building for Other / npm-Distribution Platforms

The npm packages (`@guchho/<platform>`) are produced by the CI workflow `.github/workflows/npm-publish.yml`, which supports multiple build methods. Most run on hosted Ubuntu runners; AIX builds on a self-hosted runner:

### 5.1 Android (NDK cross-compilation)

Requirements on the build host (Linux):

- Android **NDK r26b** (`nttld/setup-ndk` action uses this version).
- CMake ≥ 3.25.

| Platform        | ANDROID_ABI      |
| --------------- | ---------------- |
| android-arm     | `armeabi-v7a`    |
| android-arm64   | `arm64-v8a`      |
| android-x64     | `x86_64`         |

All use `ANDROID_PLATFORM=android-24`, `ANDROID_STL=c++_static`, and the NDK's `android.toolchain.cmake`.

API level 24 (Android 7.0) is the floor because the dev server lists the
addresses a browser can reach with `getifaddrs()`, which Android did not grow
until API level 24. `src/api/api_serve.cpp` says so with an `#error` when it is
built for anything older.

The C++ runtime is linked statically. With `c++_shared` the binary needs
`libc++_shared.so` at run time, and the NDK's CMake toolchain neither puts
that library next to the binary nor records a run path to it, so a packaged
binary would stop at startup on a device. A command line tool has no other
shared library to share with, so the runtime belongs inside the binary.

```bash
cmake -B build/release-android-arm64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build build/release-android-arm64 --parallel
```

### 5.2 Windows MSVC cross-compilation (ia32 / arm64)

Build on a Windows runner with Visual Studio, using the `Visual Studio 18 2026` generator and arch `Win32` or `ARM64`:

```bash
cmake -B build/release-win32-arm64 \
  -G "Visual Studio 18 2026" -A ARM64 \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build build/release-win32-arm64 --config Release --parallel
```

### 5.3 GCC cross-compilation (Linux variants)

See the toolchain package table in [Section 3.2](#322-cross-compilation-requirements-linux-variants). Example:

```bash
cmake -B build/release-linux-arm \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
  -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++ \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DBUILD_TESTING=OFF
cmake --build build/release-linux-arm --parallel
```

### 5.4 Docker + QEMU (loong64)

For `linux-loong64` there is no GCC cross-compiler; use the LoongArch container image with QEMU:

```bash
docker run --rm --platform linux/loong64 \
  -v "$PWD:/workspace" -w /workspace ghcr.io/loong64/loongnix:25 \
  bash -c "apt-get update && apt-get install -y cmake ninja-build g++ pkg-config && \
    cmake -B build/release-linux-loong64 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
      -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON \
      -DBUILD_TESTING=OFF && \
    cmake --build build/release-linux-loong64 --parallel"
```

### 5.5 FreeBSD (QEMU VM)

GitHub Actions has no FreeBSD runner, so `.github/workflows/npm-publish.yml`
builds `freebsd-x64` inside a real FreeBSD VM with
[`vmactions/freebsd-vm`](https://github.com/vmactions/freebsd-vm), booted on an
Ubuntu runner. The workspace is synced into the VM, the project is built with
the `release-freebsd-x64` preset, and the binary is synced back out for
packaging. Install the toolchain inside the VM with:

```sh
pkg install -y cmake ninja
cmake --preset release-freebsd-x64 --fresh
cmake --build --preset release-freebsd-x64 --parallel
ctest --preset release-freebsd-x64 --output-on-failure --no-tests=error
```

The same preset works on a native FreeBSD host, where `bash
scripts/build-npm.sh` builds and stages the binary in
`package/npm/@guchho/freebsd-x64/bin/`.

### 5.6 AIX PPC64 (self-hosted runner)

AIX PPC64 is a platform combination of IBM's Unix operating system and the
64-bit PowerPC processor architecture. GitHub has no hosted AIX runner, and
there is no public Linux cross toolchain for AIX, so the `aix-ppc64` build job
targets a self-hosted runner labelled `self-hosted`, `aix`, and `ppc64`.

The runner needs CMake ≥ 3.25, Ninja, Bash, and a C++20 compiler (IBM XL C/C++
or clang) already installed; the workflow does not install them because
`actions/setup-*` publishes no AIX builds. Build and test on the AIX host with:

```sh
cmake --preset release-aix-ppc64 --fresh
cmake --build --preset release-aix-ppc64 --parallel
ctest --preset release-aix-ppc64 --output-on-failure --no-tests=error
```

The same preset works with `bash scripts/build-npm.sh`, which stages the binary
in `package/npm/@guchho/aix-ppc64/bin/`.

### 5.7 Installing a prebuilt npm binary (no build required)

If you just want to use guchho, install the prebuilt npm package instead of building:

```bash
npm install guchho
```

The `install.js` postinstall hook copies the correct `@guchho/<platform>` binary into `package/npm/guchho/bin/`.


## 6. Build Scripts (Reference)

All scripts live in `scripts/` and are Bash scripts run as `bash scripts/<name>.sh`.

### 6.1 `scripts/build.sh` - Build everything (configure + build + tests)

The **main build entry point**. Runs CMake configure, build, and CTest.

```
Usage: bash scripts/build.sh [options]
```

| Option                | Description                                                |
| --------------------- | ---------------------------------------------------------- |
| `--preset <preset>`   | CMake preset to use. **Default:** `release-win32-x64`      |
| `--target <target>`   | Build only a specific target (e.g. `guchho`, `test_assertions`) |
| `--filter <regex>`    | Run only tests matching the regex (CTest `-R`)             |
| `--timeout <seconds>` | Per-test timeout for CTest                                  |
| `--log <file>`        | Save full build/test output to a file                       |
| `--verbose`, `-v`     | Verbose test output                                         |
| `--all-errors`        | Show all errors. Enables `--no-stop` + verbose build/tests  |
| `--no-stop`           | Do not stop CTest after the first failure                   |
| `--build-verbose`     | Show full compiler/linker commands                          |
| `--test-verbose`      | Show full CTest output                                      |
| `-h`, `--help`        | Show help                                                  |

Examples:

```bash
# Default build (Windows: release-win32-x64)
bash scripts/build.sh

# Explicit preset (e.g. on Linux / macOS)
bash scripts/build.sh --preset release-linux-x64
bash scripts/build.sh --preset release-darwin-arm64

# Build everything and capture a full diagnostic log
bash scripts/build.sh --all-errors --log build-errors.log

# Run only a specific test
bash scripts/build.sh --filter "Assertions.TestName" --all-errors

# Build only a target + log
bash scripts/build.sh --target guchho --log build.log
```

### 6.2 `scripts/test.sh` - Run tests only

Runs CTest against an already-configured build directory. **Default preset:** `release-win32-x64-ninja`.

```
Usage: bash scripts/test.sh [options]
```

Options: `--preset <preset>`, `--filter <regex>`, `--timeout <seconds>`, `--log <file>`, `--verbose`/`-v`, `--all-errors`, `--no-stop`, `--test-verbose`, `-h`/`--help`.

```bash
bash scripts/test.sh                                          # all tests
bash scripts/test.sh --filter "Assertions.*"                   # one suite
bash scripts/test.sh --test-verbose --log test.log            # verbose + log
```

> Requires the project to be built first (`bash scripts/build.sh`), since it needs `<build>/CTestTestfile.cmake`.

### 6.3 `scripts/build-npm.sh` - Native binary → npm package

Builds the current platform's native binary and copies it into `package/npm/@guchho/<platform>/bin/`. Supported platforms: `win32-x64`, `linux-x64`, `darwin-arm64`, `freebsd-x64`, `aix-ppc64`.

```bash
bash scripts/build-npm.sh
```

Automatically detects the platform, maps it to a preset
(`win32-x64` → `release-win32-x64-ninja`, `linux-x64` → `release-linux-x64`,
`darwin-arm64` → `release-darwin-arm64`, `aix-ppc64` → `release-aix-ppc64`),
builds, strips (Unix), and copies the binary.

### 6.4 `scripts/compile-commands.sh` - Regenerate `compile_commands.json`

Configures, builds, then copies `build/<preset>/compile_commands.json` to the project root for clangd / tooling.

```bash
bash scripts/compile-commands.sh
```

### 6.5 `scripts/bump-version.sh` - Version bump

Updates the version in `CMakeLists.txt`, `package/npm/guchho/package.json`, and every `package/npm/@guchho/*/package.json`.

```bash
bash scripts/bump-version.sh 1.2.3
```

### 6.6 `scripts/build-chocolatey.sh` - Build the Chocolatey package (.nupkg)

Downloads the official `win32-x64` release archive from GitHub, extracts `guchho.exe` **and its three MinGW runtime DLLs** and embeds them into `package/chocolatey/tools/`, syncs the version and SHA256 checksums in `guchho.nuspec` + `VERIFICATION.txt`, and runs `choco pack`.

**Requirements:** Chocolatey (`choco`), Node.js, `curl`, `unzip`.

```bash
bash scripts/build-chocolatey.sh
```

- Fetches `https://github.com/guchho/guchho/releases/download/v<version>/guchho-<version>-win32-x64.zip`, so the `v<version>` release must exist first (see [Downloadable Windows x64 Release Archive](#downloadable-windows-x64-release-archive)); the script fails with the URL if it does not.
- Every embedded file is byte-identical to the corresponding file under the release archive's `bin/`, and the script writes each file's SHA256 into `VERIFICATION.txt` so a moderator can confirm the match. It fails if the archive's `bin/` lacks `guchho.exe` or any runtime DLL (an archive built before the DLLs were packaged).
- The package carries no `chocolateyInstall.ps1` / `chocolateyUninstall.ps1`: Chocolatey shims the embedded `tools/guchho.exe` and removes it on uninstall on its own, so both scripts would be no-ops.
- Output: `package/chocolatey/guchho.<version>.nupkg` (gitignored).

Test the package locally:

```bash
choco install guchho --source 'package/chocolatey'
```

### 6.7 `scripts/publish-chocolatey.sh` - Publish to the Chocolatey community feed

Pushes the built `.nupkg` to `https://push.chocolatey.org/`.

```bash
bash scripts/publish-chocolatey.sh              # publish
bash scripts/publish-chocolatey.sh --dry-run    # simulate without pushing
```

**Requirements:** Chocolatey, Node.js, and a Chocolatey API key. Provide the key via the `CHOCO_API_KEY` environment variable, or configure it once:

```bash
choco apikey add -s https://push.chocolatey.org/ -k <API_KEY>
```

Requires the package to exist first (`bash scripts/build-chocolatey.sh`). Users install it with `choco install guchho`.

### 6.8 `scripts/build-winget.sh` - Generate WinGet manifests

Downloads the same official `win32-x64` release archive, inspects its layout, and writes the three WinGet manifests under `dist/winget/manifests/g/Guchho/Guchho/<version>/`. It uploads nothing: WinGet packages are submitted by pull request to [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).

**Requirements:** Node.js, `curl`, `unzip`, and a SHA256 tool (`sha256sum` or `shasum`). `winget` is used for validation when it is installed.

```bash
bash scripts/build-winget.sh
bash scripts/build-winget.sh --archive dist/guchho-1.0.9-win32-x64.zip
bash scripts/build-winget.sh --output /tmp/winget --no-validate
```

- Fetches `https://github.com/guchho/guchho/releases/download/v<version>/guchho-<version>-win32-x64.zip`, so the `v<version>` release must exist first (see [Downloadable Windows x64 Release Archive](#downloadable-windows-x64-release-archive)). Use `--archive` to point at a copy downloaded from the workflow instead.
- Fails if the archive has no `guchho.exe`, more than one, or no MinGW runtime DLL beside it.
- Uses an `InstallerType: zip` installer with a nested portable `guchho.exe`; `ArchiveBinariesDependOnPath: true` keeps the runtime DLLs resolvable.
- Test with `winget install --manifest <dir>`, then copy the version directory to `manifests/g/Guchho/Guchho/<version>/` in a fork of `microsoft/winget-pkgs` and open a pull request.

Users install it with `winget install Guchho.Guchho` once the submission is merged.

### 6.9 `scripts/publish-winget.sh` - Submit the WinGet manifests

Submits the manifests produced by `scripts/build-winget.sh` to [`microsoft/winget-pkgs`](https://github.com/microsoft/winget-pkgs) with `wingetcreate submit`. WinGet has no feed to push to: "publishing" opens a pull request.

```bash
bash scripts/publish-winget.sh              # submit
bash scripts/publish-winget.sh --dry-run    # validate and print the command only
```

**Requirements:** the generated manifests, `wingetcreate` (`winget install Microsoft.WingetCreate`), and a GitHub token. Prefer the environment variable, which `wingetcreate` reads itself and which keeps the token out of logs:

```bash
export WINGET_CREATE_GITHUB_TOKEN=<token>
bash scripts/publish-winget.sh
```

- Validates the manifest directory with `winget validate` first when winget is installed (`--no-validate` to skip).
- Fails if the version's manifest directory is missing; run `bash scripts/build-winget.sh` first.
- Uses `--token` only when passed explicitly; the dry run prints it as `***`. An environment token is left for `wingetcreate` to pick up.
- Optional `--pr-title <title>` overrides the pull request title; otherwise wingetcreate chooses one.

### 6.10 `scripts/build_brew.sh` - Generate the Homebrew formula

Downloads the official `v<version>` source tarball, computes its SHA256, and writes a source-build formula to `dist/homebrew/Formula/guchho.rb`. The recipe builds with CMake, so no macOS binary has to be published first: Homebrew supplies CMake and compiles the tag itself.

**Requirements:** Node.js, `curl`, and a SHA256 tool (`sha256sum` or `shasum`). Ruby is used for the `ruby -c` syntax check when it is installed.

```bash
bash scripts/build_brew.sh
bash scripts/build_brew.sh --tarball /tmp/guchho-1.0.9.tar.gz
bash scripts/build_brew.sh --output /tmp/homebrew --no-validate
```

- Fetches `https://github.com/guchho/guchho/archive/refs/tags/v<version>.tar.gz`, so the `v<version>` tag must exist first. Use `--tarball` to point at a local copy instead.
- Fails if the download is not a gzip archive (a missing tag returns HTML) or if the tarball is empty.
- The formula declares `depends_on "cmake" => :build`, runs a `Release` CMake build with `BUILD_TESTING=OFF`, and installs with `std_cmake_args`.
- Test the formula locally with `brew install --build-from-source dist/homebrew/Formula/guchho.rb`.

### 6.11 `scripts/publish_brew.sh` - Publish the formula to a tap

Homebrew has no registry to push to: publishing means committing the formula to a tap repository. The script clones the tap, installs `Formula/guchho.rb`, commits, and pushes to the tap's default branch.

```bash
bash scripts/publish_brew.sh                 # publish
bash scripts/publish_brew.sh --dry-run       # clone and show the diff only
```

**Requirements:** the generated formula, `gh` (authenticated with `gh auth login`), and `git`.

- Defaults to the `guchho/homebrew-tap` tap; override it with `--tap <owner/repo>` or `GUCHHO_HOMEBREW_TAP`.
- A missing tap is only created with `--create-tap`; otherwise the script stops and prints the `gh repo create` command.
- Skips the commit when the tap already has an identical formula, and shows the staged diff before pushing (or before stopping in `--dry-run`).
- Users install with `brew install guchho/homebrew-tap/guchho` once published.


## 7. Manual CMake Commands

Equivalent to the scripts, in case you do not want to use Bash (e.g. on Windows PowerShell):

```bash
# 1. Configure
cmake --preset <preset>

# 2. Build
cmake --build --preset <preset> --parallel

# 3. Test
ctest --preset <preset> --output-on-failure

# 4. (Optional) Install into a prefix
cmake --install build/<preset> --prefix <install-dir>

# 5. (Optional) Package (ZIP/NSIS on Windows, TGZ elsewhere)
cpack --config build/<preset>/CPackConfig.cmake
```

The presets also define **workflows** that run configure + build + test in one command:

```bash
cmake --workflow --preset release-win32-x64          # Windows
cmake --workflow --preset release-win32-x64-ninja    # Windows (Ninja)
cmake --workflow --preset release-linux-x64          # Linux x64
cmake --workflow --preset release-darwin-arm64       # macOS ARM64
cmake --workflow --preset ci                         # CI (strict)
```

Available single presets for each platform and build type:

| Build type    | Win (VS) / Win (Ninja)        | Linux x64 / arm64          | macOS arm64 / x64      |
| ------------- | ------------------------------ | -------------------------- | ---------------------- |
| Debug         | `debug-win32-x64`             | `debug-linux-x64`          | `debug-darwin-arm64`   |
|               | `debug-win32-x64-ninja`       | `debug-linux-arm64`        | `debug-darwin-x64`     |
| Release       | `release-win32-x64`           | `release-linux-x64`        | `release-darwin-arm64` |
|               | `release-win32-x64-ninja`     | `release-linux-arm64`      | `release-darwin-x64`   |
| RelWithDebInfo| `relwithdebinfo-win32-x64`    | `relwithdebinfo-linux-x64` | `relwithdebinfo-darwin-arm64` |
|               |                                | `relwithdebinfo-linux-arm64`| `relwithdebinfo-darwin-x64` |
| MinSizeRel    | `minsizerel-win32-x64`        | `minsizerel-linux-x64`     | `minsizerel-darwin-arm64` |
|               |                                | `minsizerel-linux-arm64`   | `minsizerel-darwin-x64` |

Special presets:

| Preset              | Purpose                                            |
| ------------------- | -------------------------------------------------- |
| `asan-linux-x64`    | Debug build with AddressSanitizer (GCC/Clang)      |
| `ubsan-linux-x64`   | Debug build with UndefinedBehaviorSanitizer        |
| `ci`                | Release Windows x64 + strict warnings + warnings-as-errors |

> Note: UBSan is ignored under MSVC (unsupported); ASan works on MSVC, GCC, and Clang.


## 8. Build Outputs & Packaging

| Artifact                          | Location                                        |
| --------------------------------- | ----------------------------------------------- |
| Binary (Windows)                  | `build/<preset>/bin/guchho.exe`                 |
| Binary (Linux/macOS/Unix)         | `build/<preset>/bin/guchho`                     |
| Test executables                  | `build/<preset>/test/`                          |
| Compile database (Ninja presets)  | `build/<preset>/compile_commands.json`          |
| Install location                  | `cmake --install` (default `<prefix>/bin`)      |
| npm platform binary               | `package/npm/@guchho/<platform>/bin/guchho[.exe]`|
| Chocolatey package                | `package/chocolatey/guchho/guchho-<version>.nupkg`|
| Chocolatey embedded binary        | `package/chocolatey/guchho/tools/guchho.exe`       |
| WinGet manifests                  | `dist/winget/manifests/g/Guchho/Guchho/<version>/` |
| Homebrew formula                  | `dist/homebrew/Formula/guchho.rb`               |

CPack generators:

- **Windows:** `ZIP;NSIS` → `guchho-<version>-win32-x64.zip` + `.exe` installer (NSIS must be installed).
- **macOS / Linux / Unix:** `TGZ` → `guchho-<version>-<platform>.tar.gz`.

Package file naming: `guchho-<version>-<platform>` (e.g. `guchho-1.0.9-linux-x64`).

Chocolatey distribution is handled separately by `scripts/build-chocolatey.sh` and `scripts/publish-chocolatey.sh` (see [Sections 6.6/6.7](#66-scriptsbuild-chocolateysh---build-the-chocolatey-package-nupkg)); it downloads `guchho-<version>-win32-x64.zip` from the GitHub release and embeds its `bin/guchho.exe` at package-build time rather than downloading at install time.

WinGet manifests are generated by `scripts/build-winget.sh` (see [Section 6.8](#68-scriptsbuild-wingetsh---generate-winget-manifests)) from the same release archive and submitted to `microsoft/winget-pkgs` by pull request with `scripts/publish-winget.sh` (see [Section 6.9](#69-scriptspublish-wingetsh---submit-the-winget-manifests)).

Homebrew distribution is handled separately by `scripts/build_brew.sh` and `scripts/publish_brew.sh` (see [Sections 6.10/6.11](#610-scriptsbuild_brewsh---generate-the-homebrew-formula)). Unlike Chocolatey and WinGet, the formula builds `guchho` from the `v<version>` source tarball and is published by committing it to a tap.


## 9. Quick Reference - "Build Everything"

One command per platform (assumes [Section 4](#4-install-the-toolchain--quick-script) was done):

```bash
# ---- Windows ----
bash scripts/build.sh --preset release-win32-x64-ninja   # or release-win32-x64 (VS)

# ---- Linux x64 ----
bash scripts/build.sh --preset release-linux-x64

# ---- Linux ARM64 ----
bash scripts/build.sh --preset release-linux-arm64

# ---- macOS Apple Silicon ----
bash scripts/build.sh --preset release-darwin-arm64

# ---- macOS Intel ----
bash scripts/build.sh --preset release-darwin-x64
```

For a **full build + tests + full error log** on any OS, use:

```bash
bash scripts/build.sh --preset <your-platform-preset> --all-errors --log build-errors.log
```

If the toolchain check fails, run `bash scripts/build.sh --help` and verify `cmake --version` is ≥ 3.25.


## Manual npm Publish

The `npm-publish.yml` workflow is **manual-only**.

Inputs:

| Input      | Default | Meaning                                                   |
| ---------- | ------- | --------------------------------------------------------- |
| `type`     | `all`   | What to publish: one platform, `guchho`, or `all`.        |
| `dry_run`  | `true`  | Build and validate without publishing to npm.             |
| `test_run` | `true`  | Run the CTest suite after each native build.              |

Both flags only publish/skip when set to an explicit `false`/`true`; a missing
or unrecognized value falls back to the safe default (`dry_run` stays on,
`test_run` stays on). A test failure fails the build job, which skips every
publishing job — nothing ships with failing tests unless `test_run=false`.

### Dry run

Test each platform without publishing to npm:

```bash
gh workflow run npm-publish.yml -f type=linux-x64 -f dry_run=true
```

```bash
gh workflow run npm-publish.yml -f type=win32-x64 -f dry_run=true
```

```bash
gh workflow run npm-publish.yml -f type=darwin-arm64 -f dry_run=true
```

Test all packages:

```bash
gh workflow run npm-publish.yml -f type=all -f dry_run=true
```

Test the main `guchho` package only:

```bash
gh workflow run npm-publish.yml -f type=guchho -f dry_run=true
```

### Publish

Publish a single platform package:

```bash
gh workflow run npm-publish.yml -f type=linux-x64 -f dry_run=false test_run=true
```

```bash
gh workflow run npm-publish.yml -f type=win32-x64 -f dry_run=false test_run=true
```

```bash
gh workflow run npm-publish.yml -f type=darwin-arm64 -f dry_run=false test_run=true
```

```bash
gh workflow run npm-publish.yml -f type=android-arm64 -f dry_run=false test_run=true
```


```bash
gh workflow run npm-publish.yml -f type=win32-ia32 -f dry_run=false dry_run=true
```

```bash
gh workflow run npm-publish.yml -f type=aix-ppc64 -f dry_run=false -f test_run=true
```

Publish only the main `guchho` package:

```bash
gh workflow run npm-publish.yml -f type=guchho -f dry_run=false dry_run=true
```

Publish all platform packages and the main `guchho` package:

```bash
gh workflow run npm-publish.yml -f type=all -f dry_run=false
```

### Package types

| `type`         | Packages                         |
| -------------- | -------------------------------- |
| `linux-x64`    | `@guchho/linux-x64`              |
| `win32-x64`    | `@guchho/win32-x64`              |
| `darwin-arm64` | `@guchho/darwin-arm64`           |
| `aix-ppc64`    | `@guchho/aix-ppc64`              |
| `guchho`       | `guchho`                         |
| `all`          | All platform packages + `guchho` |

`dry_run=true` builds and validates packages without publishing. `dry_run=false` performs the actual npm publish.

`test_run=true` (the default) runs `ctest --preset <preset> --output-on-failure --no-tests=error` on each selected native platform after the build; a failure blocks all publishing jobs. `test_run=false` skips the suite — use it only when the tests were already run on the same commit.

### Downloadable Windows x64 release archive

For any run whose `type` includes `win32-x64` — that is `type=win32-x64` or
`type=all`, in either dry-run or publish mode — the workflow also assembles the
portable release archive with CPack and uploads it as the `guchho-win32-x64`
workflow artifact. It contains a single file named
`guchho-<version>-win32-x64.zip` (the version from
`package/npm/guchho/package.json`), built from the same `guchho.exe` the
platform package uses. This is the archive the Chocolatey verification links
to at
`https://github.com/guchho/guchho/releases/download/v<version>/guchho-<version>-win32-x64.zip`,
and the one `scripts/build-chocolatey.sh` downloads from that URL once the
archive is attached to the `v<version>` GitHub release — so the release must
be published before the Chocolatey package is built.

Download it from the workflow run that produced it:

```bash
gh run list --workflow npm-publish.yml -L 1
gh run download <RUN_ID> --name guchho-win32-x64 -D ./dist
# → ./dist/guchho-win32-x64/guchho-<version>-win32-x64.zip
```

Only the `win32-x64` platform uploads this archive; the other platforms put
only their `package-<platform>` staging directory on the run. The artifact is
kept for one day, like the package staging artifacts.


# Downloadable Windows x64 Release Archive

## 1. Run the workflow

```bash
gh workflow run npm-publish.yml -f type=win32-x64 -f dry_run=false test_run=true
```

## 2. Find the run ID

```bash
gh run list --workflow npm-publish.yml -L 1
```

## 3. Download the ZIP

Replace `<RUN_ID>` with the actual GitHub Actions run ID:

```bash
gh run download <RUN_ID> --name guchho-win32-x64 -D ./dist
```

## Download location

After the download completes, the archive should be located at:

```text
dist/
└── guchho-win32-x64/
    └── guchho-<version>-win32-x64.zip
```

Replace `<version>` with the project's actual version number.

The archive's `guchho-<version>-win32-x64/bin/` carries `guchho.exe` together
with its three MinGW runtime DLLs (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`,
`libwinpthread-1.dll`); the Chocolatey package is built from exactly those
files, and its `VERIFICATION.txt` lists their SHA256 checksums. An archive that
contains only `guchho.exe` is stale — rebuild it with this workflow.

**Note:** `dry_run=false` may publish the package to npm according to the workflow's existing behavior. To test the workflow without publishing, use `-f dry_run=true` instead.
