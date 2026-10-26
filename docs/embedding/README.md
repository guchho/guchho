---
title: Embedding Guchho in C++
description: The guchho/api.hpp and guchho/cli.hpp surfaces, from linking to lifetime rules.
---

# Embedding Guchho in C++

Guchho is a C++ program with two public headers. The one most embedders want
is `guchho/api.hpp`: it is the whole engine as a library, and the shipped
`guchho` executable is just a small argument parser on top of it. The other,
`guchho/cli.hpp`, is the command line as a library - a way to run, or to
inspect, a complete command line without forking a process.

This page is the reasoning; the exact spelling of every declaration is
generated from the headers themselves in the
[Generated embedding reference](generated/README.md), so it can never drift.

## The three surfaces, and which one to use

- **`guchho::api`** - build, transform, watch, serve. This is `--outdir`, the
  incremental context, the dev server, all of it, reachable from your
  process. If your program wants to bundle, this is the surface.
- **`guchho::cli`** - the command line as a library.
  `Run(const std::vector<std::string>& os_args)` prints usage and diagnostics
  like the program would and returns the exit code; the
  `Parse*Options` functions share the flag grammar but do no I/O, so you can
  find out what a command line would mean without running it.
- **The service** - what the npm package actually runs. The binary speaks a
  framed protocol on stdin/stdout, and Node turns each build request into a
  response that carries outputs and diagnostics as data. You do not need this
  unless you are a host process that cannot link the library; it is spelled
  out in [`RunService` and the service calls](generated/cli.md#runservice) in
  the generated reference.

## Compiling and linking

The library is C++20 (`cxx_std_20`). Add the repository's `include/`
directory to your include path and link the static libraries:

| Library | holds | links |
| --- | --- | --- |
| `guchho_api` | the whole engine: `guchho::api` | `guchho_common`, `guchho_bundler` |
| `guchho_cli` | the command line surface: `guchho::cli` | `guchho_api` |

On Windows the API target also pulls in `ws2_32` (sockets for the dev server)
and `iphlpapi` (the IP Helper used to enumerate local addresses); on POSIX a
threads library is enough. The CMake targets carry all of that as `PUBLIC`
dependencies, so linking one target is all you need to write:

```cmake
target_link_libraries(my_tool PRIVATE guchho_api)
```

## The shape of the surface

The API is unusually declarative in a way that makes embedding it small:

- **Vocabulary is scoped enums.** `Loader`, `Format`, `Platform`, `SourceMap`,
  `TreeShaking`, `LogLevel` - every decision a build can make is an
  enumerator, and option structs reference them by name, so a build described
  in code reads like a build described on the command line.
- **Configuration is input-only plain structs.** Every member of
  `BuildOptions` and `TransformOptions` has a default. You name the fields you
  care about and leave the rest alone; there is no builder, no fluent chain,
  no copy that the engine mutates.
- **Failure is data, not exceptions.** A missing entry point is not thrown at
  you: it arrives as a `Message` in `BuildResult.errors`, with a `Location`
  pointing at the offending path, and the diagnostics render with
  `FormatMessages` if you want the printed form. Exceptions are reserved for
  programming errors and for things like calling `Serve` on a disposed
  context.

## A first build

```cpp
#include <guchho/api.hpp>

using namespace guchho::api;

int main() {
    BuildOptions opts;
    opts.entry_points = { "src/index.js" };
    opts.bundle = true;
    opts.format = Format::kESModule;
    opts.write = true;
    opts.outdir = "dist";

    BuildResult result = Build(opts);

    if (!result.errors.empty()) {
        // result.errors[i] is a Message; FormatMessages renders them.
        return 1;
    }
    return 0;
}
```

That is the whole one-shot path: resolve, walk the graph, compile, print. It
tears the session down again afterwards. When a caller intends to build more
than once, `Context()`, `BuildContext::Rebuild()` and `BuildContext::Dispose()`
do the same work while keeping the caches alive and letting a watcher or a
server own the rebuilds - which is also the shape that can be cancelled. See
[`BuildContext`](generated/context.md) in the generated reference.

## Lifetime rules

The surface has a small set of rules that hold everywhere:

- Nothing is started implicitly. A watcher, a server, a context - you create
  it, you destroy it. `Watcher::Start()` and `Watcher::Stop()` are explicit.
- A disposed `BuildContext` is inert, not dangerous: its methods return empty
  results or a `std::runtime_error` rather than crashing a late caller from
  another thread.
- Concurrent `Rebuild()` calls are fine - the second caller waits and receives
  the same result instead of starting a competing build.
- Work is synchronous unless an object owns it. There is no hidden thread pool
  doing jobs you did not ask for.

Read the full declarations in the
[Generated embedding reference](generated/README.md) - every type and function
is transcribed from `include/guchho/api.hpp` and `include/guchho/cli.hpp`, so
what you look up is what the headers say.