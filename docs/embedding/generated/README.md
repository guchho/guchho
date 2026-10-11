---
title: Generated embedding reference
description: The C++ surface, produced from api.hpp and cli.hpp.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp by scripts/gen-docs-embedding.mjs. Do not edit by hand. -->

# Generated embedding reference

This section is produced from `include/guchho/api.hpp` and
`include/guchho/cli.hpp` by `scripts/gen-docs-embedding.mjs` — the public
C++ surface, transcribed exactly and automatically kept in step. Edit the
generator or the headers, not these pages.

The [embedding guide](../README.md) is where the reasoning lives; these are
for looking things up.

## Enumerations

The vocabulary a build is described in. Every decision a caller can make about a build is a scoped enumeration, so option structs stay a flat list of assignments. [Full page](enums.md).

- [`SourceMap`](enums.md#sourcemap)
- [`SourcesContent`](enums.md#sourcescontent)
- [`LegalComments`](enums.md#legalcomments)
- [`JSX`](enums.md#jsx)
- [`Target`](enums.md#target)
- [`Loader`](enums.md#loader)
- [`Platform`](enums.md#platform)
- [`Format`](enums.md#format)
- [`Exports`](enums.md#exports)
- [`Packages`](enums.md#packages)
- [`EngineName`](enums.md#enginename)
- [`StderrColor`](enums.md#stderrcolor)
- [`LogLevel`](enums.md#loglevel)
- [`Charset`](enums.md#charset)
- [`TreeShaking`](enums.md#treeshaking)
- [`DropFlags`](enums.md#dropflags)
- [`MangleQuoted`](enums.md#manglequoted)
- [`AbsPathsFlags`](enums.md#abspathsflags)
- [`SideEffects`](enums.md#sideeffects)
- [`ResolveKind`](enums.md#resolvekind)
- [`MessageKind`](enums.md#messagekind)
- [`TargetParse`](enums.md#targetparse)

## Option structs

Configuration is input only. These structs have defaults for every member, so a caller names just the fields it cares about. [Full page](options.md).

- [`BuildOptions`](options.md#buildoptions)
- [`TransformOptions`](options.md#transformoptions)
- [`ServeOptions`](options.md#serveoptions)
- [`WatchOptions`](options.md#watchoptions)
- [`FormatMessagesOptions`](options.md#formatmessagesoptions)
- [`AnalyzeMetafileOptions`](options.md#analyzemetafileoptions)
- [`OnResolveOptions`](options.md#onresolveoptions)
- [`OnLoadOptions`](options.md#onloadoptions)
- [`ResolveOptions`](options.md#resolveoptions)
- [`MangleCacheResult`](options.md#manglecacheresult)
- [`ErrorWithNote`](options.md#errorwithnote)
- [`ParseOptionsExtras`](options.md#parseoptionsextras)

## Result and shared types

Failure is data, not an exception: anything that can go wrong while building arrives as Message entries in a result struct. [Full page](types.md).

- [`Engine`](types.md#engine)
- [`TargetSpec`](types.md#targetspec)
- [`Location`](types.md#location)
- [`Note`](types.md#note)
- [`Message`](types.md#message)
- [`OutputFile`](types.md#outputfile)
- [`EntryPoint`](types.md#entrypoint)
- [`StdinOptions`](types.md#stdinoptions)
- [`ExplicitlySet`](types.md#explicitlyset)
- [`BuildResult`](types.md#buildresult)
- [`TransformResult`](types.md#transformresult)
- [`EffectiveBuildConfigs`](types.md#effectivebuildconfigs)
- [`ServeOnRequestArgs`](types.md#serveonrequestargs)
- [`ServeResult`](types.md#serveresult)

## Sessions and the server

A BuildContext is a build that is still alive. Work is either synchronous or owned by an object the caller creates and destroys; nothing is started implicitly. [Full page](context.md).

- [`BuildContext`](context.md#buildcontext)
- [`Watcher`](context.md#watcher)
- [`CORSOptions`](context.md#corsoptions)

## The C++ plugin API

A plugin is a struct with a name and a setup function. Setup runs once, when the context is created; the hooks it registers then run for every build of that context. [Full page](plugins.md).

- [`Plugin`](plugins.md#plugin)
- [`PluginBuild`](plugins.md#pluginbuild)
- [`ResolveResult`](plugins.md#resolveresult)
- [`OnStartResult`](plugins.md#onstartresult)
- [`OnEndResult`](plugins.md#onendresult)
- [`OnResolveArgs`](plugins.md#onresolveargs)
- [`OnResolveResult`](plugins.md#onresolveresult)
- [`OnLoadArgs`](plugins.md#onloadargs)
- [`OnLoadResult`](plugins.md#onloadresult)

## Engine functions

One-shot calls, resolution helpers, the diagnostic renderers and the constants the engine publishes. [Full page](api-functions.md).

- [`ParseTargetSpec`](api-functions.md#parsetargetspec)
- [`ValidEngineNames`](api-functions.md#validenginenames)
- [`Build`](api-functions.md#build)
- [`ResolveEffectiveBuildConfigs`](api-functions.md#resolveeffectivebuildconfigs)
- [`Transform`](api-functions.md#transform)
- [`Context`](api-functions.md#context)
- [`FormatMessages`](api-functions.md#formatmessages)
- [`AnalyzeMetafile`](api-functions.md#analyzemetafile)
- [`strip_dir_prefix`](api-functions.md#strip_dir_prefix)
- [`validate_path_template`](api-functions.md#validate_path_template)
- [`Serve`](api-functions.md#serve)
- [`RebuildFn`](api-functions.md#rebuildfn)

## The command line surface

The boundary between Guchho as a program and Guchho as a library: running a command line, reading arguments without running anything, and the exit codes. [Full page](cli.md).

- [`Run`](cli.md#run)
- [`RunWithPlugins`](cli.md#runwithplugins)
- [`RunService`](cli.md#runservice)
- [`RunServiceRequest`](cli.md#runservicerequest)
- [`RunServiceCompileRequest`](cli.md#runservicecompilerequest)
- [`IsServiceCompileCommand`](cli.md#isservicecompilecommand)
- [`ParseBuildOptions`](cli.md#parsebuildoptions)
- [`ParseTransformOptions`](cli.md#parsetransformoptions)
- [`ParseLoader`](cli.md#parseloader)
- [`MakeErrorWithNote`](cli.md#makeerrorwithnote)
- [`parseMangleCache`](cli.md#parsemanglecache)
- [`printMangleCache`](cli.md#printmanglecache)
- [`ApplyHtmlBundleDefault`](cli.md#applyhtmlbundledefault)
- [`ExitCode`](cli.md#exitcode)
- [`Command`](cli.md#command)
- [`ParseOptionsKind`](cli.md#parseoptionskind)
- [`AnalyzeMode`](cli.md#analyzemode)


## Regenerate

```sh
node scripts/gen-docs-embedding.mjs
```
