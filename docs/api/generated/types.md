---
title: Types & results
description: The core types and result shapes of the guchho package.
---

<!-- GENERATED from package/npm/guchho/index.d.ts. Do not edit by hand. -->

# Types & results

Diagnostics, files, the metafile, the loaders/formats/platforms vocabularies and the shapes a build answers with.

In this page:

- [`Location`](#location)
- [`Note`](#note)
- [`Message`](#message)
- [`OutputFile`](#outputfile)
- [`Metafile`](#metafile)
- [`OutputSummary`](#outputsummary)
- [`InputSummary`](#inputsummary)
- [`AnalysisReport`](#analysisreport)
- [`BuildResult`](#buildresult)
- [`Loader`](#loader)
- [`Format`](#format)
- [`Platform`](#platform)
- [`LogLevel`](#loglevel)
- [`EntryPoint`](#entrypoint)
- [`DevServer`](#devserver)
- [`Watcher`](#watcher)

## Location

Where a diagnostic points, in a file or in a namespace.

```ts
export interface Location {
  file: string
  namespace: string
  /** 1-based */
  line: number
  /** 0-based, in bytes */
  column: number
  /** in bytes */
  length: number
  lineText: string
  suggestion: string
}
```

## Note

A secondary message attached to a primary one.

```ts
export interface Note {
  text: string
  location: Location | null
}
```

## Message

One diagnostic.
"detail" is whatever a plugin put there, passed through untouched. It is undefined on every message this package produces itself, because nothing here sets it.

```ts
export interface Message {
  id: string
  pluginName: string
  text: string
  location: Location | null
  notes: Note[]
  detail: any
}
```

## OutputFile

A file the build produced.

```ts
export interface OutputFile {
  path: string
  contents: Uint8Array
  hash: string
  /** "contents" as text. Decoded on each read. */
  readonly text: string
}
```

## Metafile

What the build read and wrote, and what it cost.

```ts
export interface Metafile {
  /**
   * The entry points, and only the entry points.
   *
   * The transitive set is the contribution map on each output - the `inputs` under
   * `outputs`, below. This top-level map is not a record of everything the build
   * read; reading it as one reports a single file for a build that read fifty.
   */
  inputs: {
    [path: string]: {
      bytes: number
      imports: { path: string; kind: string; external?: boolean }
      format?: string
    }
  }
  outputs: {
    [path: string]: {
      bytes: number
      /** Every file inside this output, and what it contributed. This is where the
       * transitive set lives. */
      inputs: { path: string; bytesInOutput: number }
      /**
       * What this output still imports at runtime - anything not inlined.
       *
       * `bundle` defaults to false, so under it a relative sibling import is
       * listed here and left alone on purpose.
       */
      imports: { path: string; kind: string; external?: boolean }
      exports: string[]
      entryPoint?: string
    }
  }
}
```

## OutputSummary

A produced file, without its contents.

```ts
export interface OutputSummary {
  path: string
  size: number
}
```

## InputSummary

A file that went into a build.

```ts
export interface InputSummary {
  path: string
  /** Bytes this file contributed, summed across every output it appears in. */
  bytes: number
  format?: string
}
```

## AnalysisReport

What a build was made of, and what came out.

```ts
export interface AnalysisReport {
  inputs: InputSummary[]
  outputs: OutputSummary[]
  /**
   * What the built output still imports, sorted and deduplicated.
   *
   * Relative paths are here too: `bundle` defaults to false, and under it a
   * build leaves sibling imports alone on purpose.
   */
  dependencies: string[]
  /** The size of the whole output tree. The sum of `outputs`. */
  totalBytes: number
}
```

## BuildResult

```ts
export interface BuildResult<Provided extends BuildOptions = BuildOptions> {
  /**
   * Whether the build worked. A build with warnings is a build that worked, so
   * this is the errors list and not the warnings list.
   *
   * A build that failed throws instead, so a caller reaching the result at all
   * has a successful one. This is here for the code that reads the result before
   * it knows how it got there.
   */
  success: boolean
  errors: Message[]
  warnings: Message[]
  /** Milliseconds, measured across the service round trip. */
  duration: number
  /**
   * Every file produced, with its size.
   *
   * Present whether or not write is false: a build that wrote its files still
   * knows what it produced, and a caller asking "what did this build make" should
   * not have to ask whether the bytes are still in memory to find out.
   */
  outputs: OutputSummary[]
  /**
   * Every file the build read.
   *
   * Only when metafile is true. This is the bundler's own transitive graph, read
   * from each output's contribution map - the metafile's top-level `inputs` names
   * the entry points and nothing else, so reading that instead would report one
   * file and be confident about it.
   */
  inputs?: InputSummary[]
  /** Only when write is false. */
  outputFiles?: Provided['write'] extends false ? OutputFile[] : undefined
  /** Only when metafile is true. */
  metafile?: Provided['metafile'] extends true ? Metafile : undefined
  /** Only when mangleCache is passed in. */
  mangleCache?: Record<string, string | false>
}
```

## Loader

A loader name, as the grammar spells it.

```ts
export type Loader =
  | 'js' | 'jsx' | 'ts' | 'tsx' | 'json' | 'text' | 'css' | 'html'
```

## Format

The output format of a script.

```ts
export type Format = 'iife' | 'cjs' | 'esm' | 'umd' | 'amd' | 'system'
```

## Platform

The platform whose conventions the output follows.

```ts
export type Platform = 'browser' | 'node' | 'neutral'
```

## LogLevel

How chatty the engine is.

```ts
export type LogLevel = 'verbose' | 'debug' | 'info' | 'warning' | 'error' | 'silent'
```

## EntryPoint

One entry point: a path in, and the name to write it out as.

```ts
export type EntryPoint = string | { out?: string; in: string } | [string | null, string]
```

## DevServer

A running dev server, from serve().
The engine's own HTTP server - the one `guchho serve` gets - not a second implementation. Close it when finished; it holds a listening socket and the project's output files open.

```ts
export interface DevServer {
  /** The first address the engine bound. */
  readonly host: string
  /** The port the engine bound, which is not always the one that was asked for. */
  readonly port: number
  /** Every address the engine bound, for a caller that wants the LAN one. */
  readonly hosts: string[]
  /**
   * A URL that can be opened.
   *
   * Built from an address that is connectable even when the engine bound a
   * wildcard, because "0.0.0.0" is not a destination a browser can route to.
   */
  readonly url: string
  /** The warm context behind the server. */
  readonly context: BuildContext
  /** Whether close() has been called. */
  readonly closed: boolean
  /** Rebuilds without restarting the server. */
  rebuild(): Promise<BuildResult>
  /** Stops the server and releases the context. Safe to call more than once. */
  close(): Promise<void>
  /** Releases it at the end of an `await using` scope. Needs "esnext.disposable"
   * (or "esnext") in lib, which is TS 5.2 and later. */
  [Symbol.asyncDispose](): Promise<void>
}
```

## Watcher

A running watcher, from watch().
Every `buildEnd` is a rebuild *this handle performed* - the engine's service answers requests and never sends one of its own, so a changed file is not reported until something asks for a rebuild. See the note on watch().

```ts
export interface Watcher {
  /** The result of the most recent build, or null before the first. */
  readonly result: BuildResult | null
  /** The interval this watcher drives itself at. Zero if it does not. */
  readonly interval: number
  /** The warm context behind the watcher. */
  readonly context: BuildContext
  /** Whether close() has been called. */
  readonly closed: boolean
  /** Subscribes to an event. Returns this, so calls can be chained. */
  on(event: 'buildStart', listener: () => void): this
  on(event: 'buildEnd', listener: (result: BuildResult) => void): this
  on(event: 'error', listener: (error: Error) => void): this
  off(event: string, listener: (...args: never[]) => void): this
  /** Rebuilds, emitting buildStart before and buildEnd after. */
  rebuild(): Promise<BuildResult>
  /** Stops watching and releases the context. Safe to call more than once. */
  close(): Promise<void>
  /** Releases it at the end of an `await using` scope. Needs "esnext.disposable"
   * (or "esnext") in lib, which is TS 5.2 and later. */
  [Symbol.asyncDispose](): Promise<void>
}
```

