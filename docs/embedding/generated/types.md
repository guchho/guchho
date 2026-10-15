---
title: Result and shared types
description: The types diagnostics, files, target strings and results are expressed in.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp. Do not edit by hand. -->

# Result and shared types

Failure is data, not an exception: anything that can go wrong while building arrives as Message entries in a result struct.

In this page:

- [`Engine`](#engine)
- [`TargetSpec`](#targetspec)
- [`Location`](#location)
- [`Note`](#note)
- [`Message`](#message)
- [`OutputFile`](#outputfile)
- [`EntryPoint`](#entrypoint)
- [`StdinOptions`](#stdinoptions)
- [`ExplicitlySet`](#explicitlyset)
- [`BuildResult`](#buildresult)
- [`TransformResult`](#transformresult)
- [`EffectiveBuildConfigs`](#effectivebuildconfigs)
- [`ServeOnRequestArgs`](#serveonrequestargs)
- [`ServeResult`](#serveresult)

## Engine

One engine and the version the caller intends to support. The version is read
as "major[.minor[.patch]]" with an optional pre-release suffix and compared
component by component, so "100.0.0" is newer than "90.0.0" and "12.0.0" is
newer than "9.9.9".

Input:  engines = { { kFirefox, "115" }, { kSafari, "16.4" } }
Output: the build is only allowed to use what both versions support, and
        the pair is reported back in the summary as a target string such as
        "firefox115,safari16.4". A version that does not parse - "latest",
        for instance - becomes a message in result.errors rather than a
        silent guess.

```cpp
struct Engine {
    EngineName name{};
    std::string version;
};
```

## TargetSpec

A target string, already understood: a language level, any number of engines
with their versions, or both. A person writes both in one list, in either
order, and neither kind depends on the other.

```cpp
struct TargetSpec {
    Target              target{Target::kDefault};
    std::vector<Engine> engines;
};
```

## Location

Where a problem was found. Line and column follow the same convention the
editor integrations expect: lines count from 1, columns count from 0 in
bytes, and length is a byte count, so a multi-byte character advances the
column by its encoded size.

```cpp
struct Location {
    std::string file;
    std::string namespace_;
    int line{};
    int column{};
    int length{};
    std::string line_text;
    std::string suggestion;
};
```

## Note

An extra paragraph attached to a Message. Warnings often come with a hint
("did you mean ...?") or a list of related files, and a Note is how that
extra context is carried. A note may have no location at all, in which case
it is a plain remark.

```cpp
struct Note {
    std::string text;
    std::optional<Location> location;
};
```

## Message

A single problem found while parsing, resolving or printing. Messages are
values, not exceptions: a build collects all of them and hands them back, so
one pass over a large graph can report every bad import at once instead of
stopping at the first one.

```cpp
struct Message {
    std::string id;          // stable machine-readable code, e.g. "import-is-undefined"
    std::string plugin_name; // set when a plugin produced this message
    std::string text;        // the human-readable sentence
    std::optional<Location> location;
    std::vector<Note> notes;

    // Opaque payload supplied by whoever created the message, passed through
    // untouched so an editor integration can recover the AST node, the original
    // exception or whatever else it stashed there.
    std::any detail;
};
```

## OutputFile

One artefact a build produced. Contents are bytes rather than text because
the loader set includes binary assets, and a caller should be able to treat
every output the same way.

```cpp
struct OutputFile {
    std::string path;               // absolute path, or "<stdout>"
    std::vector<uint8_t> contents;  // the file, exactly as it would be written

    // A short digest of the contents. Comparing hashes tells you whether a
    // file really changed without reading both copies, which is how the
    // incremental path and the writer both avoid pointless disk traffic.
    std::string hash;
};
```

## EntryPoint

One input file paired with the output path it should produce. Use these when
the mapping from input to output cannot be expressed by the naming templates
alone.

```cpp
struct EntryPoint {
    std::string input_path;
    std::string output_path;
};
```

## StdinOptions

Text handed to the build in place of a file on disk. This is how a language
server feeds the buffer the user is editing into the bundler, so the graph
sees the unsaved version of a file rather than the last one written.

```cpp
struct StdinOptions {
    std::string contents;    // the source text itself
    std::string resolve_dir; // directory that relative imports resolve against
    std::string sourcefile;  // name to blame in diagnostics, e.g. "<stdin>"
    Loader loader{Loader::kNone};
};
```

## ExplicitlySet

The names of the build options that somebody actually asked for.

Most option defaults are the value a field holds when nobody has an opinion
("false" for minify, "" for outdir), so an unset field and a field explicitly
set to that same value look identical. Where a default is the other way round
- pretty printing is on unless you ask for it not to be - that difference
decides whether a config file or a command line still gets a say, so the parser
records what it saw and the resolution consults this instead of guessing.

A caller assembling BuildOptions by hand does not need it: leave
BuildOptions::explicit_set null and every value in the struct counts as
deliberate. This exists for the one case that cannot be expressed any other
way, which is "turn this off" when the default is "on", and for the switches
whose bare form means the same thing as "--flag=false" but still has to
outrank a project that turned the other way.

```cpp
struct ExplicitlySet {
    std::unordered_set<std::string_view> keys;

    bool Has(std::string_view key) const
    {
        return keys.find(key) != keys.end();
    }
};
```

## BuildResult

What a build produced. On failure "errors" is non-empty and the output
fields stay empty; a build with only warnings is still a successful build.

```cpp
struct BuildResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::vector<OutputFile> output_files;
    std::string metafile;                              // JSON, when metafile is set
    std::unordered_map<std::string, bool> mangle_cache; // carry into the next build
};
```

## TransformResult

The transformed source plus whatever else the transform produced. The
vectors are bytes so a source map, which is JSON but never really text to be
read by a human, needs no special case.

```cpp
struct TransformResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::vector<uint8_t> code;            // the transformed source
    std::vector<uint8_t> map;             // the map itself, when one was asked for
    std::vector<uint8_t> legal_comments; // extracted banners, if any were pulled out

    std::unordered_map<std::string, bool> mangle_cache; // pass into the next call
};
```

## EffectiveBuildConfigs

Every build a project's configuration resolves to, after the four steps
above. A config file whose root is an object produces one entry; an array
root produces one entry per element, each resolved independently from the
same explicit options and the same built-in defaults.

```cpp
struct EffectiveBuildConfigs {
    std::vector<BuildOptions> builds;   // each ready to hand to Build()
    std::string root;                   // absolute directory resolution started from
    std::string config_path;            // the config file that won, or "" for none
    std::string config_dir;             // its directory, or "" for none
    bool        config_found{false};    // a config file was read
    bool        config_invalid{false};  // one was found and cannot be built as written
};
```

## ServeOnRequestArgs

The facts about one served request, handed to ServeOptions::on_request.
"time_in_ms" measures how long the response took to generate; it says
nothing about how long the client took to download it, which the server
cannot observe.

```cpp
struct ServeOnRequestArgs {
    std::string remote_address;
    std::string method;
    std::string path;
    int status{};
    int time_in_ms{};
};
```

## ServeResult

The running server, plus the only way to stop it. Call "stop" exactly once;
it blocks until the listening socket is closed and every in-flight request
has been answered, so it is safe to use during shutdown.

```cpp
struct ServeResult {
    uint16_t port{};             // the port actually bound, which may differ
    std::vector<std::string> hosts; // URLs to print, one per reachable address

    std::function<void()> stop;

    // Tells the connected live-reload clients that a rebuild just finished,
    // diffing this result against the previous one and pushing a "change"
    // event only when the output actually differs. This is how a rebuild that
    // the server itself did not run - a watch session, most importantly -
    // reaches the browser: the server rebuilds on demand for each request, so
    // anything that builds off the request path has to knock. Calling it is
    // always safe and does nothing useful when live_reload is off or no
    // client is connected. The handler it names outlives stop().
    std::function<void(const BuildResult&)> notify;
};
```
