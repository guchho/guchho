---
title: The C++ plugin API
description: The structs and callbacks through which a C++ embedder extends a build.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp by scripts/gen-docs-embedding.mjs. Do not edit by hand. -->

# The C++ plugin API

A plugin is a struct with a name and a setup function. Setup runs once, when the context is created; the hooks it registers then run for every build of that context.

In this page:

- [`Plugin`](#plugin)
- [`PluginBuild`](#pluginbuild)
- [`ResolveResult`](#resolveresult)
- [`OnStartResult`](#onstartresult)
- [`OnEndResult`](#onendresult)
- [`OnResolveArgs`](#onresolveargs)
- [`OnResolveResult`](#onresolveresult)
- [`OnLoadArgs`](#onloadargs)
- [`OnLoadResult`](#onloadresult)

## Plugin

One plugin. Add instances to BuildOptions::plugins. The name is not optional:
it appears in the file names and error messages of everything the plugin
does, and a plugin with an empty name is rejected. Setup is a chance to
inspect the options and register hooks, not to do the work; whatever the
plugin builds or caches is then reused across rebuilds.

```cpp
struct Plugin {
    std::string name;
    std::function<void(struct PluginBuild&)> setup;
};
```

## PluginBuild

Everything a plugin is given during setup. The function members are filled in
by the engine before setup() is called; a plugin that does not need one can
simply leave it alone.

```cpp
struct PluginBuild {

    // The options the caller passed to Build() or Context(). Reading them lets
    // a plugin adapt to the build it is part of — to honour the outdir, to see
    // which loaders the caller configured, or to know whether the caller
    // wanted files written at all. The pointer is valid for as long as the
    // build lives; it is const, because a plugin changes what a build does
    // through its hooks rather than by editing the options behind the engine's
    // back.
    const BuildOptions* initial_options{};

    // Resolves a path immediately, from inside a hook, using the build's own
    // resolution rules and the same chain of resolve callbacks. This is the
    // escape hatch for a plugin that needs to know where an import points but
    // must not answer for it: ask the engine, then adjust the answer.
    //
    // Two rules make it safe to call: it cannot be used during setup, because
    // the build's options are not wired up yet, and "kind" must be stated
    // explicitly, because the call site is not an import statement.
    //
    // Input:  resolve("./helper.js", { importer = "src/index.js",
    //                                 namespace_ = "file",
    //                                 resolve_dir = "src",
    //                                 kind = kJSImportStatement })
    // Output: a ResolveResult whose "path" is the absolute resolved file and
    //         whose "plugin_data" is whatever that module's own resolve step
    //         stored. When nothing can resolve the path, the result carries an
    //         error whose notes list the candidates that came closest.
    std::function<ResolveResult(const std::string& path, const ResolveOptions& options)> resolve;

    // Registers a callback that runs at the very start of each build, before
    // any file has been read. All start callbacks run at the same time, each
    // on its own thread, and the build waits for every one of them before it
    // begins scanning, so they must not depend on each other's order or
    // results. An error returned from one aborts the build before any work is
    // done, which makes this the cheap place to reject a configuration.
    //
    // Input:  build.on_start([&]{ return OnStartResult{}; });
    // Output: nothing; the callback fires at the start of this build and of
    //         every later rebuild of the same context.
    std::function<void(std::function<OnStartResult()> callback)> on_start;

    // Registers a callback that runs at the end of each build, after the output
    // files have been produced but before they are written or returned. This is
    // where a plugin inspects the finished bundle, adds a warning about it, or
    // appends an extra output file of its own. End callbacks run one after
    // another in registration order, and each sees the changes the earlier ones
    // made to the result.
    //
    // Input:  build.on_end([&](BuildResult& r) {
    //             r.warnings.push_back(...);
    //             return OnEndResult{};
    //         });
    // Output: nothing; the BuildResult the caller receives carries whatever the
    //         callbacks added.
    std::function<void(std::function<OnEndResult(BuildResult&)> callback)> on_end;

    // Registers a callback that runs after the output files have been produced
    // but before they are written. This is the hook a plugin uses to change what
    // is written: the BuildResult it is handed holds "output_files", and the
    // callback may rewrite a file's contents, drop one, or append one of its
    // own. Callbacks run in registration order, each seeing what the earlier
    // ones changed, and the files as they stand after the last one are what the
    // write step puts on disk.
    //
    // This is deliberately not "on_end". End callbacks run after the write, when
    // a change to a file is a change to a result that is already on disk and no
    // longer reachable from the files. Generate callbacks run before it, which
    // is the only place a hook can affect the bytes.
    //
    // Input:  build.on_generate_bundle([&](BuildResult& r) {
    //             for (auto& f : r.output_files) f.contents.push_back('\n');
    //             return OnEndResult{};
    //         });
    // Output: nothing; the bundle the build writes carries the change.
    std::function<void(std::function<OnEndResult(BuildResult&)> callback)> on_generate_bundle;

    // Registers a callback that runs after the output files have been written,
    // with the same bundle the write step used. It is the mirror of
    // on_generate_bundle and exists so that a plugin can report on, or read
    // back, what was written without having to write it itself.
    //
    // Input:  build.on_write_bundle([&](BuildResult& r) { return OnEndResult{}; });
    // Output: nothing.
    std::function<void(std::function<OnEndResult(BuildResult&)> callback)> on_write_bundle;

    // Registers a resolve callback for import paths matching "filter" — a
    // pattern such as "*.png", a path prefix, or "" for everything — within an
    // optional namespace. Every matching callback is tried in the order the
    // plugins were registered, before any built-in resolution rule, and the
    // first one that returns a path settles the question for that import. The
    // remaining callbacks are not called.
    //
    // Two ways to get in trouble are worth knowing. Returning a path in the
    // "file" namespace that is not absolute is an error, not a hint, because
    // later stages need a complete path. Returning any error stops resolution
    // for that import outright; built-in rules are not consulted as a fallback.
    //
    // Input:  registered with filter = "virtual:*" and asked about
    //         "virtual:button" from "src/app.js"
    // Output: a result whose path = "/abs/generated/button.js" and whose
    //         plugin_data holds a token; that token is handed to the load step
    //         for the same path. An empty path declines, and normal resolution
    //         takes over. An empty path with external = true marks the specifier
    //         itself external, leaving the import statement untouched.
    std::function<void(const OnResolveOptions& options, std::function<OnResolveResult(const OnResolveArgs&)> callback)> on_resolve;

    // Registers a load callback for resolved paths matching "filter". This is
    // the step that produces the text the compiler sees, so it is where a
    // plugin substitutes content for a file, changes the loader, or changes
    // the directory that relative imports inside that file resolve against.
    //
    // Callbacks are tried in the same order as resolve, and the first one that
    // returns contents wins. Returning no contents declines. When every plugin
    // declines, a path in the "file" namespace is read from the file system as
    // usual; any other namespace has no contents and fails with "Do not know
    // how to load path", because only a plugin can say what such a path means.
    //
    // Input:  registered with filter = "/abs/generated/*.js" and asked about
    //         "/abs/generated/button.js" with plugin_data = 42
    // Output: contents = "export const Button = () => null;", loader = kJS;
    //         the engine parses that string instead of opening the path. A
    //         result that leaves the loader as kNone is loaded as kJS, and one
    //         that leaves abs_resolve_dir empty resolves relative imports
    //         against the file's own directory.
    std::function<void(const OnLoadOptions& options, std::function<OnLoadResult(const OnLoadArgs&)> callback)> on_load;

    // Registers a callback for when the build is torn down — the plugin's
    // chance to close files, flush caches or join helper threads. It runs
    // during Dispose(), which is the last thing that happens to a context, so
    // no build is in flight and no other hook can follow.
    //
    // Input:  build.on_dispose([]{ cache.Close(); });
    // Output: nothing; the callback runs once, when the context is disposed.
    std::function<void(std::function<void()> callback)> on_dispose;
};
```

## ResolveResult

The answer to a resolve request. Leaving "path" empty declines the request,
which lets the next plugin — and finally the built-in rules — have a go;
filling in "errors" instead fails the build with those messages. Setting
"external" ends the search and leaves the specifier as written in the output,
which is how a plugin says "this import is somebody else's problem".

```cpp
struct ResolveResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::string path;      // the resolved path, absolute
    bool external{};       // leave the import alone in the output
    bool side_effects{};   // true unless something vouched that the module is side-effect free
    std::string namespace_;
    std::string suffix;    // part of the module's identity but not its name, e.g. "?raw"
    std::any plugin_data;  // payload handed to the matching load hook
};
```

## OnStartResult

What a plugin may report from its on_start callback. Adding an error here
aborts the build before any file is read, which makes it the place to reject
a whole configuration early and cheaply.

```cpp
struct OnStartResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;
};
```

## OnEndResult

What a plugin may report from its on_end callback. The BuildResult handed to
the callback is still being assembled, so a plugin can add messages, and may
also inspect the output files that were produced.

```cpp
struct OnEndResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;
};
```

## OnResolveArgs

The arguments of a single resolve callback invocation.

```cpp
struct OnResolveArgs {
    std::string path;      // the specifier as written in the source
    std::string importer;  // the file containing the import
    std::string namespace_;
    std::string resolve_dir;
    ResolveKind kind{ResolveKind::kNone};
    std::any plugin_data;
    std::unordered_map<std::string, std::string> with;
};
```

## OnResolveResult

What a resolve callback returns. Filling in "path" claims the request and
stops the search; leaving everything empty declines it and lets the next
plugin try. "watch_files" and "watch_dirs" add paths that this answer
depends on, so a change to them triggers a rebuild even though no module
imports them.

A few combinations are worth spelling out. "suffix" must start with "?" or
"#", and it is part of the module's identity without being part of its name,
so "img.png?v=2" can be a different module from "img.png?v=1" that resolves
to the same file. "side_effects" only has an effect when it is kFalse, which
promises that nothing is lost by dropping the module when no export of it is
used. And anything returned alongside an empty path — namespace, suffix,
plugin data, watch paths — is reported as a warning, because a claim about a
path that was never claimed cannot be honoured.

```cpp
struct OnResolveResult {
    std::string plugin_name; // set automatically to the owning plugin

    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::string path;
    bool external{};
    SideEffects side_effects{SideEffects::kTrue};
    std::string namespace_;
    std::string suffix;
    std::any plugin_data;

    std::vector<std::string> watch_files;
    std::vector<std::string> watch_dirs;
};
```

## OnLoadArgs

The arguments of a single load callback invocation. "plugin_data" is
whatever the matching resolve step left behind, which is how the two halves
of a plugin pass state: the resolve step stores a token, and the load step
recognises it.

```cpp
struct OnLoadArgs {
    std::string path;
    std::string namespace_;
    std::string suffix;
    std::any plugin_data;
    std::unordered_map<std::string, std::string> with;
};
```

## OnLoadResult

What a load callback returns. Setting "contents" means the engine uses that
string instead of reading the path, so a plugin can synthesise a module that
never existed on disk; leaving it unset declines, and a declined load falls
back to the file system. "resolve_dir" becomes the base for that module's
own relative imports, and an empty one means "the directory the path is in".
As with resolve, returning an error fails the load instead of declining it.

```cpp
struct OnLoadResult {
    std::string plugin_name; // set automatically to the owning plugin

    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::optional<std::string> contents;
    std::string resolve_dir;
    Loader loader{Loader::kNone};
    std::any plugin_data;

    std::vector<std::string> watch_files;
    std::vector<std::string> watch_dirs;
};
```
