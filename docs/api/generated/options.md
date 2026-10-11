---
title: Options
description: Every option interface of the guchho package.
---

<!-- GENERATED from package/npm/guchho/index.d.ts by scripts/gen-docs-api.mjs. Do not edit by hand. -->

# Options

The options a caller hands to `build`, `transform`, `pack`, `serve`, `watch` and `analyze`, plus the sections of a configuration file.

In this page:

- [`CommonOptions`](#commonoptions)
- [`BuildOptions`](#buildoptions)
- [`BuildOptionsNested`](#buildoptionsnested)
- [`BuildConfigSections`](#buildconfigsections)
- [`TransformOptions`](#transformoptions)
- [`PackOptions`](#packoptions)
- [`ServeOptions`](#serveoptions)
- [`WatchOptions`](#watchoptions)

## CommonOptions

```ts
export interface CommonOptions {
  /** Absolute path relative paths are resolved against. */
  absWorkingDir?: string
  /** Modules to look for in these directories, ahead of node_modules. */
  nodePaths?: string[]
  /** Extensions tried when a path has none, in order. */
  resolveExtensions?: string[]
  /** Which fields of a package.json to read for its entry point. */
  mainFields?: string[]
  /** Which conditions of an exports map to take. */
  conditions?: string[]
  /** Replacement paths for modules, by prefix. */
  alias?: Record<string, string>
  /** Identifiers to replace, and what to replace them with. */
  define?: Record<string, string>
  /** Packages to leave as imports rather than bundle. */
  external?: string[]
  /** How chatty the engine is. */
  logLevel?: LogLevel
  /** Where the engine's own log lines go. */
  logLimit?: number
  /** Colour in diagnostics, or "auto" to decide from the terminal. */
  color?: boolean
  /** The target language version. */
  target?: string | string[]
  /** Where "file" in a diagnostic is resolved against. */
  sourceRoot?: string
  /** The prefix on every generated URL. */
  publicPath?: string
  /** Text placed at the top of every generated JavaScript file. */
  banner?: string
  /** Text placed at the bottom of every generated JavaScript file. */
  footer?: string
  /** Legal comments to keep: none, inline, eof, linked, external. */
  legalComments?: 'none' | 'inline' | 'eof' | 'linked' | 'external'
  /** Character set for non-ASCII output. */
  charset?: 'ascii' | 'utf8'
  /** The JSX flavour. */
  jsx?: 'transform' | 'preserve' | 'automatic'
  jsxFactory?: string
  jsxFragment?: string
  jsxImportSource?: string
  jsxDev?: boolean
  jsxSideEffects?: boolean
  /** The tsconfig to read paths and compiler options from. */
  tsconfig?: string
  /** Renames to apply, or patterns of names to drop. */
  mangleProps?: string
  reserveProps?: string
  drop?: string | string[]
  dropLabels?: string | string[]
  keepNames?: boolean
  mangleQuoted?: boolean
  /** Only ever true in this package: there is no watch mode option on build. */
  minify?: boolean
  minifyWhitespace?: boolean
  minifyIdentifiers?: boolean
  minifySyntax?: boolean
  minifyHtml?: boolean
  pretty?: boolean
  /** A property name pattern, applied to an object at runtime. */
  supported?: Record<string, boolean>
  /**
   * Write sourcemaps. `true` for the engine's default mode, or name one.
   *
   * The named values used to be dropped on the floor: the flag table read
   * `sourcemap` as a boolean, and a string is not `true`, so "inline" produced no
   * flag and no complaint.
   */
  sourcemap?: boolean | 'none' | 'inline' | 'external' | 'linked' | 'both'
  /** Overwrite files that already exist. Off by default. */
  allowOverwrite?: boolean
  /** Follow symlinks rather than bundling what they point at. */
  preserveSymlinks?: boolean
  /** A metafile for the build to analyse, for the bundle. */
  mangleCache?: Record<string, string | false>
  /** Take the build's input from memory rather than from disk. */
  stdin?: {
    contents: string | Uint8Array
    loader: Loader
    resolveDir?: string
    sourcefile?: string
  }
  /**
   * The same options under "build", as a config file nests them.
   *
   * An alias for writing them flat, not a second API: build({ outdir }) and
   * build({ build: { outdir } }) produce identical flags. Giving the same option
   * at both levels is refused rather than resolved, because picking one is a
   * guess about where the output goes.
   *
   * "entry" is the one spelling that moves — to entrypoints — so the nested form
   * accepts it.
   */
    build?: BuildOptionsNested
  /** Server options, when the configuration is one for serving. */
  server?: ServeOptions
  /** Watch options. `true` means "build and keep watching". */
  watch?: boolean | WatchOptions
  /** Plugins to run the build with. See the plugin API. */
  plugins?: Plugin[] | Plugin
}
```

## BuildOptions

```ts
export interface BuildOptions extends CommonOptions {
  /**
   * What to build. Optional because `stdin` is an alternative, not because a
   * build without one is fine — a configuration with neither throws rather than
   * falling back to a glob, finding nothing and reporting success.
   */
  entrypoints?: EntryPoint[] | EntryPoint
  /**
   * The spelling this option had before "entrypoints" was renamed to lowercase.
   *
   * An alias, not a second option: it reaches the same place and setting both
   * is a TypeError rather than a silent preference for one. Kept so a script
   * written against the old name keeps running.
   */
  entryPoints?: EntryPoint[] | EntryPoint
  /** Which loader to use, by file extension. A build's loader is a map, where a
   * transform's is a single name for its one input. */
  loader?: Record<string, Loader>
  outdir?: string
  outfile?: string
  outbase?: string
  format?: Format
  /**
   * Which entry export the bundle publishes as its public value.
   *
   * - `"named"` — a namespace object whose properties are the export names
   *   (the default, and what a build without the option always does).
   * - `"default"` — the entry point's default export *is* the value: a
   *   CommonJS `require()` hands it straight back, and a UMD or IIFE global
   *   is it. The entry must have a default export (a CommonJS entry's
   *   `module.exports` counts); named exports alongside it are not exposed.
   * - `"none"` — publish nothing. Valid only when no entry point exports
   *   anything.
   * - `"auto"` — infer from the entry's actual exports, style: no
   *   exports means `"none"`, a lone default means `"default"`, anything
   *   else means `"named"`.
   *
   * Read for every output format.
   */
  exports?: "default" | "named" | "none" | "auto"
  /**
   * The global a UMD or IIFE wrapper publishes under, as a dotted path.
   *
   * `globalName` is the same option under its other spelling; setting both is
   * a TypeError rather than a preference.
   */
  name?: string
  /** The spelling `name` is an alias of. */
  globalName?: string
  platform?: Platform
  bundle?: boolean
  splitting?: boolean
  treeShaking?: boolean
  sourcesContent?: boolean
  ignoreAnnotations?: boolean
  metafile?: boolean
  /** Whether to write the outputs. True unless said otherwise. */
  write?: boolean
  /** A sourcemap to embed rather than write beside the output. */
  sourcemapRaw?: string
}
```

## BuildOptionsNested

Build options in the nested spelling.
Identical to BuildOptions except that "entry" is accepted for "entrypoints". It is a separate interface only because that one alias differs; everything else is inherited, so the two cannot drift into describing different options.

```ts
export interface BuildOptionsNested
  extends Omit<BuildOptions, keyof BuildConfigSections> {
  entry?: EntryPoint[] | EntryPoint
}
```

## BuildConfigSections

Not exported. Referenced by the options page so the "build: { … }" section and its siblings stay one spelled-out list.

```ts
interface BuildConfigSections {
  build: BuildOptionsNested
  server: object
  watch: object
  plugins: object
}
```

## TransformOptions

```ts
export interface TransformOptions extends CommonOptions {
  loader?: Loader
  /** The name a diagnostic should blame. Worth setting. */
  sourcefile?: string
  format?: Format
  platform?: Platform
  bundle?: boolean
  sourcemap?: boolean
  minify?: boolean
  minifyWhitespace?: boolean
  minifyIdentifiers?: boolean
  minifySyntax?: boolean
  target?: string | string[]
  charset?: 'ascii' | 'utf8'
  legalComments?: 'none' | 'inline' | 'eof' | 'linked' | 'external'
  globalName?: string
}
```

## PackOptions

What pack() writes, and from what.
Every path is a path on disk. Each entry of inputs becomes a top-level name taken from its last path component, so "dist" and "site/build" each arrive under their own name rather than under the tree they came from.

```ts
export interface PackOptions {
  /**
   * Where to write the archive. The parent directory is created if it is
   * missing, and the archive itself is moved into place at the end — so a
   * caller reading this path after a failure is reading nothing rather than a
   * half-written archive.
   */
  outFile: string
  /** The files and directories to put in the archive. */
  inputs: string[]
  /**
   * Which archive format to write: `"zip"` (the default), `"tar"` or
   * `"tar.gz"`. Leaving this out is the same as writing `"zip"`; any other
   * string is a RangeError thrown before the engine is asked.
   */
  format?: "zip" | "tar" | "tar.gz"
  /**
   * Deflate level, 0 to 9. 0 stores rather than compresses. The engine's
   * default is 6, which is what a caller gets by leaving this out.
   *
   * Does not apply to `"tar"`, which stores its entries uncompressed; a level
   * paired with `"tar"` is a RangeError.
   */
  level?: number
  /**
   * Replace an existing outFile. Without it, an existing file is an error
   * rather than something to destroy.
   */
  overwrite?: boolean
  /**
   * The timestamp every entry gets. Without it each entry keeps the time of
   * the file it came from.
   *
   * A Date rather than a number of seconds, because that is the type a caller
   * has and converting it in the caller would be a step they would get wrong
   * on the one day a timestamp is not in this decade's range.
   */
  date?: Date
  /**
   * Unix permission bits to record for every file and directory, as `0o755`
   * and friends. Without it each entry keeps the permissions of the file it
   * came from. Recorded, not applied: an archive extracted on Windows does not
   * become executable because a mode was written into it.
   */
  mode?: number
}
```

## ServeOptions

The options the dev server reads from a configuration's "server" section.
Declared once and referenced by CommonOptions, so the section a caller may write and the section this package validates are the same list. `open` is deliberately absent: it is accepted at runtime and refused with an explanation, because a config written for `guchho serve` may carry it and a server that silently did not open a browser is worse than one that says it cannot.

```ts
export interface ServeOptions {
  /**
   * The port to bind. Zero — or leaving it out — asks the engine to try 8000
   * upwards and take the first that is free, which is why the port that was
   * actually bound is on the handle rather than assumed to be this one.
   */
  port?: number
  /** The interface to bind. Left out, every interface is bound. */
  host?: string
  /** The directory to serve. Defaults to the build's output directory. */
  servedir?: string
  /** Served when nothing else matches, which is how SPA routes are handled. */
  fallback?: string
}
```

## WatchOptions

The options the watcher reads from a configuration's "watch" section.

```ts
export interface WatchOptions {
  /** Milliseconds of quiet before a change is taken as final. */
  delay?: number
  /**
   * How often the handle rebuilds on its own, in milliseconds. Zero means never:
   * the handle then rebuilds only when `rebuild()` is called. Left out, the handle
   * drives itself.
   */
  interval?: number
}
```

