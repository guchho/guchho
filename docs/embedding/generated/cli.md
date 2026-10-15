---
title: The command line surface
description: The declarations of include/guchho/cli.hpp.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp. Do not edit by hand. -->

# The command line surface

The boundary between Guchho as a program and Guchho as a library: running a command line, reading arguments without running anything, and the exit codes.

In this page:

- [`Run`](#run)
- [`RunWithPlugins`](#runwithplugins)
- [`RunService`](#runservice)
- [`RunServiceRequest`](#runservicerequest)
- [`RunServiceCompileRequest`](#runservicecompilerequest)
- [`IsServiceCompileCommand`](#isservicecompilecommand)
- [`ParseBuildOptions`](#parsebuildoptions)
- [`ParseTransformOptions`](#parsetransformoptions)
- [`ParseLoader`](#parseloader)
- [`MakeErrorWithNote`](#makeerrorwithnote)
- [`parseMangleCache`](#parsemanglecache)
- [`printMangleCache`](#printmanglecache)
- [`ApplyHtmlBundleDefault`](#applyhtmlbundledefault)
- [`ExitCode`](#exitcode)
- [`Command`](#command)
- [`ParseOptionsKind`](#parseoptionskind)
- [`AnalyzeMode`](#analyzemode)

## Run

Runs a command line and returns the code the process should exit with.
The arguments are the ones that follow the executable name, in the order
they were given; nothing is appended, reordered or quietly defaulted
behind the caller's back.

With no plugins attached a run is self contained: it reads what it needs,
writes what was asked for, and prints a summary. Anything that only
makes sense to a host program - reading a flag before the arguments are
parsed, or reacting to the finished run - is done by passing plugins
instead.

The exit code is the useful output: 0 for success and for the forms of
help that are not failures, 1 when a build ran and failed, 2 when the
command line itself was rejected, and 130 when the run was interrupted.

Input:  Run({ "build", "src/index.js", "--outdir=dist" })
Output: a banner, a short summary of the files written, and 0.

Input:  Run({ "build", "--nope" })
Output: "Invalid build flag: \"--nope\"" on the error stream and 2, with
        nothing read from the file system.

Input:  Run({})
Output: the usage text and 0, so a bare invocation in a script is not
        treated as a failure.

```cpp
int Run(const std::vector<std::string>& os_args);
```

## RunWithPlugins

The same thing with plugins attached, which is the only difference
between the two entry points. A plugin can add flags, inspect each parsed
build, run before and after the build itself, and see the paths the build
read; the engine never calls a plugin it was not handed here.

The list is borrowed for the duration of the call. A plugin that outlives
the run must not still be reachable from a background thread once the
call has returned.

Input:  RunWithPlugins({ "build", "src/index.js", "--outdir=dist" },
                        { size_report_plugin })
Output: the same build, its summary, and 0, with the plugin's hooks
        called at the points api::Plugin describes.

```cpp
int RunWithPlugins(const std::vector<std::string>& os_args,
                       const std::vector<api::Plugin>& plugins);
```

## RunService

The service

The same engine reached over a pipe rather than over a command line, which
is what lets the npm package get a build's outputs and its diagnostics as
data instead of as files and text. Run() is a one-shot; this runs until its
input ends, answering every request as it is read.

It is started by "--service=<version>" rather than by being called from
Node, because the binary that already exists is the one that has the
engine in it, and a second shared library to reach the same code from
Node would be a second copy of it on the machine.

Input:  the version frame, then length-prefixed requests, on stdin
Output: the version frame, then one response per request, on stdout, and
        nothing else - a print here would land inside a frame

The protocol is guchho/service.hpp. Nothing here reaches the console, and
no plugin callback crosses the wire, which is why RunWithPlugins has no
counterpart here yet.

```cpp
int RunService();
```

## RunServiceRequest

Answers one service request, in this process.

RunService is a loop around this: it decodes a frame, calls this, and
encodes the answer. Splitting it out is what makes any of the protocol
testable without a pipe, and it is why the tests can check ten commands in
the time a process would take to start once.

Input:  a request whose "command" names a command (guchho/service.hpp)
Output: the response for it, which is what RunService would have framed

```cpp
service::Value RunServiceRequest(const service::Value& request);
```

## RunServiceCompileRequest

The compiler half of the service, in one file.

The commands above build; these twelve reach a single lexer, a single
parser or a single printer, once, over source the request carried. They
are the same engines a build uses, reached without a module graph, a
resolver or a linker around them, and a command here is answered by the
same code that answers a build's.

They are split into their own entry point rather than added to
RunServiceRequest because the trees they parse are held between requests.
A build is finished when it answers; a parse leaves an AST behind for a
print to come back for, which means the handler owns state that outlives
the call. Keeping that state in one file keeps it in one place.

The twelve names are "<stage>-<language>" - lex-html, parse-html,
print-html, transform-html, and the same four for css and js. A tree does
not cross the wire: a parse answers with a number, and a print or a
transform answers with that number.

Input:  a request whose "command" is one of the twelve above
Output: the response for it, in the same shape a build answer is in

```cpp
service::Value RunServiceCompileRequest(const std::string& name, const service::Value& request);
```

## IsServiceCompileCommand

Whether "name" is one of the twelve commands above, so that the main
dispatch can route to this table without also having to know that it is a
different table. A name this returns false for is a build command, a name
this returns true for is not.

```cpp
bool IsServiceCompileCommand(const std::string& name);
```

## ParseBuildOptions

Parses build flags.

A bare path becomes an entry point; the same path written as "out=in"
becomes an advanced entry point, which is how one specific output file is
requested. --minify is a shorthand that sets all three minify switches at
once. A bare --sourcemap with no output location named anywhere in the
same command line means the map is inlined, since there would otherwise
be no file to put it in.

Input:  ParseBuildOptions({ "--bundle", "--minify", "--outdir=dist",
                            "src/index.js" })
Output: api::BuildOptions with bundling on, all three minify switches on,
        outdir = "dist" and one entry point, plus an empty optional.

Input:  ParseBuildOptions({ "dist/app.js=src/index.js", "--metafile" })
Output: one advanced entry point with output "dist/app.js" and input
        "src/index.js", a metafile requested, plus an empty optional.

Input:  ParseBuildOptions({ "--outfil=out.js" })
Output: the default options, and the message
        "Invalid build flag: \"--outfil=out.js\"" as the second half of
        the pair. The suggestion of the correct spelling is a note, and
        notes are not part of this result.

```cpp
std::pair<api::BuildOptions, std::optional<std::string>>
    ParseBuildOptions(const std::vector<std::string>& os_args);
```

## ParseTransformOptions

Parses transform flags, which are the same flags minus everything that
describes a build rather than a single file. There are no entry points,
no output location and no watching here, so a bare path is a mistake
rather than an entry point.

Input:  ParseTransformOptions({ "--minify" })
Output: api::TransformOptions with minify_syntax, minify_whitespace and
        minify_identifiers all set, plus an empty optional.

Input:  ParseTransformOptions({ "--minify-identifiers", "src/index.js" })
Output: the default transform options, and the message
        "Invalid transform flag: \"src/index.js\"" as the second half of
        the pair.

```cpp
std::pair<api::TransformOptions, std::optional<std::string>>
    ParseTransformOptions(const std::vector<std::string>& os_args);
```

## ParseLoader

Translates one loader name into the value the engine uses, for the flag
that sets every file at once and for the form that names a single file.

"out" is written only when the name is known, so a caller that gets a
message back can keep whatever it had stored there. The table is also the
single source of truth for spelling: the note attached to a bad name is
produced from it, so a name that is accepted and a name that is offered
as a suggestion can never drift apart.

Input:  ParseLoader("json", out)
Output: out = api::Loader::kJSON and an empty optional, meaning success.

Input:  ParseLoader("jsonn", out)
Output: out untouched, and the message
        "Invalid loader \"jsonn\"", which the caller turns into an error
        with the accepted names attached as its note.

```cpp
std::optional<std::string> ParseLoader(const std::string& value, api::Loader& out);
```

## MakeErrorWithNote

The one place a pair like that is built. Both parameters arrive by value
and are moved into the result, so a caller can hand over the temporary
strings produced by the quoting helpers without copying them first.

Input:  MakeErrorWithNote("Invalid build flag: \"-o\"",
                          "Use \"--outfile=\" instead of \"-o\".")
Output: an ErrorWithNote whose text is the first argument and whose note
        is the second; where it is printed is the caller's decision.

```cpp
inline ErrorWithNote MakeErrorWithNote(std::string text, std::string note) {
        return ErrorWithNote{std::move(text), std::move(note)};
    }
```

## parseMangleCache

Reads the cache file at "abs_path" and returns it in the form above.

The read goes through "fs" rather than around it, so a run against an
in-memory tree sees the same file a run on disk would. "os_args" is not
scanned for options: it is handed to the logger so that a complaint
about this one file is printed with the same verbosity and colour
settings as the rest of the run.

The interesting case is the one that is not a failure. A file that is not
there is normal on a first build, so it produces an empty result and no
message, and the build simply starts a fresh cache. Everything else - a
file that cannot be read, a syntax error, a top level that is not an
object, a value that is neither a string nor "false" - is reported
through that log and also produces an empty result, because a mapping
that is only half understood is worse than none: continuing with it
would silently shorten a different set of properties than the previous
build did.

Input:  a file containing
            { "firstName": "a", "apiKey": false }
Output: cache holding "firstName" -> "a" and "apiKey" -> empty optional,
        order holding both names in the order they were written, and
        nothing logged.

Input:  a file containing { "firstName": true }
Output: an error naming "firstName" and asking for a name or "false",
        logged, and both containers empty in the result.

Input:  no file at that path
Output: both containers empty, nothing logged.

```cpp
MangleCacheResult parseMangleCache(
        const std::vector<std::string>& os_args,
        filesystem::Fs& fs,
        const std::string& abs_path);
```

## printMangleCache

Formats a mapping back into the cache file's text form. Nothing is
written here - the caller decides where the text goes, which is what
lets a build keep the file next to its configuration and lets a test
compare the string.

Two details make the file pleasant to keep in a repository. Names are
printed in the order they were read, so a build that changed nothing
rewrites the file byte for byte. And when a build has added names since
the file was read, the new ones are placed to match the file's existing
style: merged into a fully sorted file if the original order was already
sorted, and otherwise appended as a sorted block after the original
order, so a hand written file keeps its shape. A name that is listed in
"original_order" but has no mapping any more is written as "false", which
records that the property is now left alone instead of letting it be
forgotten and shortened to something new later.

"ascii_only" escapes everything outside the ASCII range, for a cache file
that has to survive a terminal, an editor or a version control system
with a different idea of the encoding.

Input:  cache holding "firstName" -> "a" and "apiKey" -> empty optional,
        original_order = { "firstName", "apiKey" }, ascii_only = false
Output: the text
            {
              "firstName": "a",
              "apiKey": false
            }
        with two space indentation and a trailing newline, and the
        values otherwise untouched.

```cpp
std::string printMangleCache(
        const std::unordered_map<std::string, std::optional<std::string>>& mangle_cache,
        const std::vector<std::string>& original_order,
        bool ascii_only);
```

## ApplyHtmlBundleDefault

Switches bundling on when a document is being built.

A document entry point - an ".html" file - cannot be served as one copied
file: it refers to its scripts and styles, so a build that left bundling
off would hand back a directory of untouched assets instead of the single
file a browser needs. So when any entry point ends in ".html" and
bundling has not been asked for either way, it is switched on here rather
than refused, because such a request cannot mean anything else.

The test is a plain case sensitive suffix check over the plain entry point
list, and it runs only while bundling is still off: an explicit choice on
the command line always wins, and an entry point given in the "output=input"
form is not inspected.

Input:  entry points { "index.html" } and bundling off
Output: the same structure with bundling on, nothing else touched.

Input:  entry points { "main.js" } and bundling off
Output: unchanged.

Input:  entry points { "index.html" } and bundling already on
Output: unchanged.

```cpp
void ApplyHtmlBundleDefault(api::BuildOptions& opts);
```

## ExitCode

The code a command returns. Run() hands it to the process, and a command
that fails early returns one of these rather than a bare literal, so the
meaning of a returned 2 is written down in one place.

```cpp
enum class ExitCode : int {
        kSuccess = 0,
        kBuildFailure = 1,
        kCLIUsageError = 2,
        kInterrupted = 130,
    };
```

## Command

The command word that selected what a run should do. The word is the
first argument, and a first argument that is a path rather than a command
is a build: see the detection in cli_run.cpp.

```cpp
enum class Command : uint8_t {
        kNone,
        kBuild,
        kDev,
        kServe,
        kWatch,
        kInit,
        kClean,
        kInfo,
        kTransform,
        kPack,
    };
```

## ParseOptionsKind

Whether the flags being parsed are the ones the command line itself will
act on, or the ones a host program asked about through
ParseBuildOptions() / ParseTransformOptions(). The two grammars are the
same, but the external one reports the complaint without its note,
because a note about a command line spelling is not an answer to a
question an embedder asked.

```cpp
enum class ParseOptionsKind : uint8_t {
        kInternal,
        kExternal,
    };
```

## AnalyzeMode

How much the run was asked to say about the size of what it produced.
Turning it on keeps the metafile, because the report is made from it.

```cpp
enum class AnalyzeMode : uint8_t {
        kDisabled,
        kEnabled,
        kVerbose,
    };
```
