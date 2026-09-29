// Type declarations for guchho.
//
// The API is the one guchho publishes, so that a project moving between the
// two bundlers is a change of import name rather than a rewrite. Where Guchho's
// options are a superset — the loader map, the HTML and CSS pipelines — the
// extra names are declared alongside; where Guchho does not have an guchho
// option, it is left out rather than declared and refused at runtime.
//
// Two things are deliberately not declared:
//
//   - buildSync and transformSync. Both would have to block the thread until a
//     child process answers, which is the one thing a long-lived service
//     protocol cannot do without a second process to ask. Declaring them would
//     be a promise this package does not keep.
//
//   - the plugin system. There is no plugin API yet, and declaring the hooks
//     would be a promise that nothing implements.

/** Where a diagnostic points, in a file or in a namespace. */
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

/** A secondary message attached to a primary one. */
export interface Note {
  text: string
  location: Location | null
}

/**
 * One diagnostic.
 *
 * "detail" is whatever a plugin put there, passed through untouched. It is
 * undefined on every message this package produces itself, because nothing here
 * sets it.
 */
export interface Message {
  id: string
  pluginName: string
  text: string
  location: Location | null
  notes: Note[]
  detail: any
}

/** A file the build produced. */
export interface OutputFile {
  path: string
  contents: Uint8Array
  hash: string
  /** "contents" as text. Decoded on each read. */
  readonly text: string
}

/** What the build read and wrote, and what it cost. */
export interface Metafile {
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
      inputs: { path: string; bytesInOutput: number }
      imports: { path: string; kind: string; external?: boolean }
      exports: string[]
      entryPoint?: string
    }
  }
}

/** A loader name, as the grammar spells it. */
export type Loader =
  | 'js' | 'jsx' | 'ts' | 'tsx' | 'json' | 'text' | 'css' | 'html'
  | 'base64' | 'dataurl' | 'file' | 'binary' | 'copy' | 'empty'

/** The output format of a script. */
export type Format = 'iife' | 'cjs' | 'esm' | 'umd' | 'amd' | 'system'

/** The platform whose conventions the output follows. */
export type Platform = 'browser' | 'node' | 'neutral'

/** How chatty the engine is. */
export type LogLevel = 'verbose' | 'debug' | 'info' | 'warning' | 'error' | 'silent'

/** One entry point: a path in, and the name to write it out as. */
export type EntryPoint = string | { out?: string; in: string } | [string | null, string]

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
  /** Which loader to use, by file extension. */
  loader?: Record<string, Loader>
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
  /** Text prepended to and appended to every output file. */
  banner?: { js?: string; css?: string }
  footer?: { js?: string; css?: string }
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
  /** Write a sourcemap beside each output. */
  sourcemap?: boolean | 'inline' | 'external' | 'linked' | 'both'
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
}

export interface BuildOptions extends CommonOptions {
  entryPoints: EntryPoint[] | EntryPoint
  outdir?: string
  outfile?: string
  outbase?: string
  format?: Format
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

export interface BuildResult<Provided extends BuildOptions = BuildOptions> {
  errors: Message[]
  warnings: Message[]
  /** Only when write is false. */
  outputFiles?: Provided['write'] extends false ? OutputFile[] : undefined
  /** Only when metafile is true. */
  metafile?: Provided['metafile'] extends true ? Metafile : undefined
  /** Only when mangleCache is passed in. */
  mangleCache?: Record<string, string | false>
}

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

export interface TransformResult {
  code: string
  /** Empty when no sourcemap was asked for. */
  map: string
  errors: Message[]
  warnings: Message[]
}

/**
 * A build that is set up once and built as many times as asked.
 *
 * A context holds everything the engine learned reading the project, which is
 * what makes the second build cheaper than the first. Dispose it when finished:
 * it holds a watcher, and sometimes a listening socket.
 */
export interface BuildContext {
  /** Builds again with the options this context was made with. */
  rebuild(): Promise<BuildResult>
  /**
   * Starts watching for changes.
   *
   * This is not guchho's watch: the service answers requests and sends nothing
   * unasked, so a rebuild has to be asked for. Call rebuild() to collect the
   * result, and use onRebuild to be told about the ones this context performs.
   */
  watch(options?: {
    /** Milliseconds of quiet before a change is taken as final. */
    delay?: number
    /** Called with the result of each rebuild. */
    onRebuild?: (result: BuildResult) => void
  }): Promise<void>
  /** Stops watching. The context is still usable. */
  cancel(): Promise<void>
  /** Serves the outputs over HTTP. */
  serve(options?: {
    servedir?: string
    port?: number
    host?: string
    fallback?: string
  }): Promise<{ host: string; port: number; hosts: string[] }>
  /** Releases the context. Safe to call more than once. */
  dispose(): Promise<void>
}

export interface ServeOptions {
  port?: number
  host?: string
  servedir?: string
  fallback?: string
}

/** A build that failed. Carries the diagnostics it failed with. */
export declare class BuildFailure extends Error {
  readonly errors: Message[]
  readonly warnings: Message[]
}

/**
 * The service itself could not be reached or could not answer.
 *
 * A different thing from a BuildFailure, with a different fix: this one is about
 * the installation, that one is about the code being built.
 */
export declare class ServiceError extends Error {
  readonly code?: number
  readonly errors?: Message[]
  readonly warnings?: Message[]
}

/**
 * Builds a project.
 *
 * @throws {BuildFailure} When the build failed.
 */
export declare function build(options: BuildOptions): Promise<BuildResult>

/**
 * Transforms one piece of source.
 *
 * @throws {BuildFailure} When the transform failed.
 */
export declare function transform(
  code: string | Uint8Array,
  options?: TransformOptions
): Promise<TransformResult>

/** Sets up a build that can be repeated. */
export declare function context(options: BuildOptions): Promise<BuildContext>

/** Formats diagnostics the way a terminal would show them. */
export declare function formatMessages(
  messages: Message[],
  options?: {
    kind?: 'error' | 'warning'
    color?: boolean
    terminalWidth?: number
  }
): Promise<string>

/** Pretty-prints a metafile. */
export declare function analyzeMetafile(
  metafile: Metafile | string
): Promise<string>

/**
 * Ends the service process, once it has actually ended.
 *
 * Await this before removing a build's output directory: while the process is
 * alive it has those files open, and on Windows removing them fails.
 */
export declare function stop(): Promise<void>

/** This package's version. */
export declare function version(): string

/** The path of the native binary this package found. */
export declare function getBinaryPath(): string

/** The platform key, as used in the optional dependency names. */
export declare function getPlatformKey(): string

/** Runs the native binary with the given arguments. */
export declare function spawnBinary(args?: string[]): import('child_process').ChildProcess

/** The class context() returns. Declared for instanceof. */
export declare const BuildContext: {
  new (...args: any[]): BuildContext
  prototype: BuildContext
}
