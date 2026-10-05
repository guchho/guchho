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
//
// The version is a string, not a function: it is read once from the manifest
// the package was installed as, so comparing it against the runtime's
// expectations is a value comparison, the way a version always is.

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
  /**
   * The entry points, and only the entry points.
   *
   * The transitive set is the contribution map on each output — the `inputs` under
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
       * What this output still imports at runtime — anything not inlined.
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
   * "entry" is the one spelling that moves — to entryPoints — so the nested form
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

/**
 * Build options in the nested spelling.
 *
 * Identical to BuildOptions except that "entry" is accepted for "entryPoints".
 * It is a separate interface only because that one alias differs; everything
 * else is inherited, so the two cannot drift into describing different options.
 */
export interface BuildOptionsNested
  extends Omit<BuildOptions, keyof BuildConfigSections> {
  entry?: EntryPoint[] | EntryPoint
}

/** The keys that only exist on a whole configuration, not on a build's options.
 * Omitted from the nested form because "build: { server: … }" nests a server
 * inside a build, which is not a thing: server, watch and plugins are siblings of
 * build, not children. */
interface BuildConfigSections {
  build: BuildOptionsNested
  server: object
  watch: object
  plugins: object
}

/** A hook the engine calls while a build runs. */
export interface Plugin {
  name: string
  /** Called once, before anything is built. */
  onStart?: (build: PluginBuild) => void | Promise<void>
  /** Asked where a specifier resolves to. */
  onResolve?: (
    specifier: string,
    context: { importer?: string; kind?: string }
  ) => { path: string; external?: boolean } | null | undefined | Promise<
    { path: string; external?: boolean } | null | undefined
  >
  /** Asked for a file's contents, instead of the loader reading it. */
  onLoad?: (
    path: string,
    context: { importer?: string; kind?: string }
  ) => { contents: string | Uint8Array; loader?: Loader } | null | undefined | Promise<
    { contents: string | Uint8Array; loader?: Loader } | null | undefined
  >
  /** Called after each build, with its result. */
  onEnd?: (build: PluginBuild) => void | Promise<void>
  /** Called when the build is torn down. */
  onDispose?: (build: PluginBuild) => void | Promise<void>
}

/** What a plugin is handed when a hook fires. */
export interface PluginBuild {
  /** The options this build was made with. */
  options: BuildOptions
  /** The build's result, once there is one. */
  readonly result?: BuildResult
}

export interface BuildOptions extends CommonOptions {
  /**
   * What to build. Optional because `stdin` is an alternative, not because a
   * build without one is fine — a configuration with neither throws rather than
   * falling back to a glob, finding nothing and reporting success.
   */
  entryPoints?: EntryPoint[] | EntryPoint
  /** Which loader to use, by file extension. A build's loader is a map, where a
   * transform's is a single name for its one input. */
  loader?: Record<string, Loader>
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
   * from each output's contribution map — the metafile's top-level `inputs` names
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

/** A produced file, without its contents. */
export interface OutputSummary {
  path: string
  size: number
}

/** A file that went into a build. */
export interface InputSummary {
  path: string
  /** Bytes this file contributed, summed across every output it appears in. */
  bytes: number
  format?: string
}

/** What a build was made of, and what came out. */
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

/**
 * The options the dev server reads from a configuration's "server" section.
 *
 * Declared once and referenced by CommonOptions, so the section a caller may write
 * and the section this package validates are the same list. `open` is
 * deliberately absent: it is accepted at runtime and refused with an explanation,
 * because a config written for `guchho serve` may carry it and a server that
 * silently did not open a browser is worse than one that says it cannot.
 */
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

/** The options the watcher reads from a configuration's "watch" section. */
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

/**
 * A running dev server, from serve().
 *
 * The engine's own HTTP server — the one `guchho serve` gets — not a second
 * implementation. Close it when finished; it holds a listening socket and the
 * project's output files open.
 */
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
  [Symbol.asyncDispose](): Promise<void>
}

/**
 * A running watcher, from watch().
 *
 * Every `buildEnd` is a rebuild *this handle performed* — the engine's service
 * answers requests and never sends one of its own, so a changed file is not
 * reported until something asks for a rebuild. See the note on watch().
 */
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
  [Symbol.asyncDispose](): Promise<void>
}

/** A build that failed. Carries the diagnostics it failed with. */
export declare class BuildFailure extends Error {
  constructor(errors: Message[], warnings?: Message[])
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
 * An array builds each configuration and answers with one result per
 * configuration, in the order given. It does not go looking for more: a caller
 * who wrote three configurations wants three builds.
 *
 * @throws {BuildFailure} When a build failed.
 */
/** An overload rather than a union, because the answer's shape follows the
 * argument's. A caller passing one configuration gets one result back and can
 * read result.success without narrowing first; a caller passing an array gets an
 * array. A union would make every caller check, which is the cost of supporting
 * both spellings leaking onto the common one. */
export declare function build(options: BuildOptions): Promise<BuildResult>
export declare function build(
  options: BuildOptions[]
): Promise<BuildResult[]>

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

/**
 * Starts a dev server on a build.
 *
 * Resolves once the server is listening, so the address is a value rather than
 * something to be scraped out of a log. The engine's own HTTP server is used —
 * the one `guchho serve` gets — rather than a second implementation.
 *
 * The served directory defaults to the build's output directory, which is what
 * makes the server serve the build at all: a context served with no directory
 * answers every request with a 404.
 *
 * The build is kept warm rather than discarded, so `server.rebuild()` costs what a
 * rebuild costs and not what a first build costs. Close the server when finished.
 *
 * @throws {TypeError} When the configuration is not an object, names a server
 *   option the server does not have, or asks for `open`.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function serve(options?: BuildOptions): Promise<DevServer>

/**
 * Starts watching a build.
 *
 * The opening build has already run by the time this resolves, so the outputs
 * exist and `watcher.result` is that build's result.
 *
 * **The events are not a push.** Guchho's service answers requests and never sends
 * one of its own, so a changed file is not reported until something asks for a
 * rebuild. Every `buildEnd` is a rebuild this handle performed, either because
 * `rebuild()` was called or because `watch.interval` asked for one. Asking for a
 * rebuild runs a build pass whether or not anything changed, which is why
 * `interval` is opt-in and has a real price — set it to 0 and drive it yourself.
 * A true push needs a notification packet on the protocol, which does not exist
 * yet.
 *
 * @throws {TypeError} When the configuration is not an object, names a watch
 *   option the watcher does not have, or gives an interval that is not a
 *   non-negative number.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function watch(options?: BuildOptions): Promise<Watcher>

/**
 * Analyses a build configuration without producing anything.
 *
 * The numbers come from the metafile the engine already writes, so this is the
 * bundler's own dependency graph rather than a second opinion from a scanner that
 * walks the tree and guesses — a scanner disagrees with the bundler about resolve
 * extensions, tsconfig paths and package exports, and it disagrees by reporting
 * files the build never read.
 *
 * Nothing is written: the build runs with `write: false` and leaves the project
 * exactly as it found it. For a build that has to happen anyway, ask for
 * `metafile: true` and read `inputs` off the result.
 *
 * @throws {BuildFailure} When the configuration does not build. An analysis of a
 * build that failed is an analysis of nothing.
 */
export declare function analyze(
  options?: BuildOptions | BuildOptionsNested
): Promise<AnalysisReport>

/**
 * Pretty-prints a metafile as a text report.
 *
 * What `analyze` used to do, under the name that says what it does. Accepts the
 * parsed metafile or its JSON text, because a caller holding one usually has the
 * object from a build and a caller holding the other read it off disk.
 */
export declare function analyzeMetafile(metafile: Metafile | string): Promise<string>

/**
 * Ends the service process, once it has actually ended.
 *
 * Await this before removing a build's output directory: while the process is
 * alive it has those files open, and on Windows removing them fails.
 */
export declare function stop(): Promise<void>

/** This package's version, as published. A string, not a function. */
export declare const version: string

// ---------------------------------------------------------------------------
// The compiler API: lex, parse, transform and print, one function per
// language. Each stage sends one request to the same service a build uses, so
// the lexer, the parser and the printer are the engine's own. The AST never
// crosses the wire: a parse answers with a numeric handle and a structural
// summary, a transform turns one handle into another, and a print turns a
// handle into text.
// ---------------------------------------------------------------------------

/** The source a compile command reads. Bytes pass through as bytes. */
export type CompileSource = string | Uint8Array

/**
 * The handle a parse returned. Opaque to this package: it is the service's id
 * for a tree it holds, and only transform* and print* may use it, in the same
 * language it came from.
 */
export type AstHandle = number

/** Where a token or a node sat in the source. Zero when no location was kept. */
export interface SourcePosition {
  /** Offset in bytes from the start of the input. */
  start: number
  /** Offset in bytes from the start of the input. */
  end: number
  /** 1-based line. */
  line: number
  /** 0-based column, in bytes. */
  column: number
  /** In bytes. */
  length: number
}

/** What every compile command answers with, before its own fields. */
export interface CompileResult {
  errors: Message[]
  warnings: Message[]
}

export type HtmlTokenKind =
  | 'comment'
  | 'doctype'
  | 'start-tag'
  | 'end-tag'
  | 'eof'
  | 'character'
  | 'null-character'
  | 'whitespace-character'

/** One HTML token. For a tag, "value" is the tag name and "length" says how
 * many attributes it carried. */
export interface HtmlToken extends SourcePosition {
  kind: HtmlTokenKind
  value: string
}

export type CssTokenKind =
  | 'endOfFile' | 'atKeyword' | 'unterminatedString' | 'badUrl' | 'cdc' | 'cdo'
  | 'closeBrace' | 'closeBracket' | 'closeParen' | 'colon' | 'comma' | 'delim'
  | 'ampersand' | 'asterisk' | 'bar' | 'caret' | 'dollar' | 'dot' | 'equals'
  | 'exclamation' | 'greaterThan' | 'lessThan' | 'minus' | 'plus' | 'slash'
  | 'tilde' | 'dimension' | 'function' | 'hash' | 'ident' | 'number'
  | 'openBrace' | 'openBracket' | 'openParen' | 'percentage' | 'semicolon'
  | 'string' | 'url' | 'whitespace' | 'symbol'

/** One CSS token. Whitespace and comments are separate lists, not tokens. */
export interface CssToken extends SourcePosition {
  kind: CssTokenKind
  value: string
}

/** One CSS comment. Its text is the whole comment, "/" fences and all. */
export interface CssComment extends SourcePosition {
  text: string
}

/** One JS token. Its kind is the grammar's own name. */
export interface JsToken extends SourcePosition {
  kind: string
  value: string
}

/**
 * One node of the flattened tree an HTML parse returns. An element carries its tag
 * name and the names of its attributes — not their values, which belong in the
 * printed form. "depth" is how the flat list is a tree again.
 */
export interface HtmlNode {
  type: string
  depth: number
  tag?: string
  attributes?: string[]
}

export interface CssMinifyOptions {
  minifyWhitespace?: boolean
  minifySyntax?: boolean
  minifyIdentifiers?: boolean
}

/** One top-level rule of the parsed stylesheet. */
export interface CssRule {
  kind: string
  start: number
}

/** A named thing in scope, as a JS parse saw it. */
export interface JsSymbol {
  name: string
  useCount: number
}

export interface PrintResult extends CompileResult {
  /** The printed source. */
  code: string
}

/** The languages the engine has a lexer, a parser and a printer for. */
export type Language = 'html' | 'css' | 'js'

/** The source name a diagnostic blames. Defaults to "<stdin>". */
export interface CompileSourceFileOptions {
  sourcefile?: string
}

/**
 * The options every stage needs.
 *
 * "language" is required on all three. There is no default, because inferring one
 * from the source text is guessing — and a guess that is right 95% of the time is
 * a guess that silently mis-lexes the other 5%.
 */
export interface CompileOptions extends CompileSourceFileOptions {
  language: Language
}

export interface LexOptions extends CompileOptions {
  /** CSS only. Whether the comment list is populated; off is also cheaper. */
  includeComments?: boolean
}

export interface ParseOptions extends CompileOptions, CssMinifyOptions {
  /** HTML only. Parse as a fragment: no wrappers and no doctype complaint. */
  fragment?: boolean
  /** HTML only. Count the imports the parser resolved. */
  collectImportRecords?: boolean
  /** HTML only. Count inline script and style elements. */
  collectInlineCode?: boolean
}

export interface PrintOptions extends CompileOptions, CssMinifyOptions {
  /** HTML only. Reindent rather than stream. Off by default. */
  pretty?: boolean
  /** HTML only. Minify the printed document. */
  minify?: boolean
  /** HTML only. Whether script elements run, which print can ask about. */
  scriptingEnabled?: boolean
  /** Escape non-ASCII. */
  asciiOnly?: boolean
}

/**
 * What lexer answers with.
 *
 * The token list is the language's own: HtmlToken, CssToken or JsToken. "kind" is
 * a string rather than one of three unions because the three kinds are three
 * unrelated vocabularies and a union of them would put a type error in every
 * caller that narrowed.
 */
export interface LexResult extends CompileResult {
  tokens: (HtmlToken | CssToken | JsToken)[]
  /** CSS only, and only when includeComments was set. */
  comments?: CssComment[]
  count: number
}

/**
 * What parse answers with.
 *
 * The summary fields are the language's — nodes for HTML, rules for CSS, parts
 * and symbols for JS — and are all optional, because which ones are present
 * depends on which language was parsed. A caller reading result.rules has already
 * said which language it asked for, and narrowing on language narrows these.
 */
export interface ParseResult extends CompileResult {
  /** The handle to hand to print(). */
  ast: AstHandle
  /** HTML: the flattened tree. */
  nodes?: HtmlNode[]
  /** HTML. */
  nodeCount?: number
  /** HTML, only when collectImportRecords was set. */
  importRecords?: number
  /** HTML, only when collectInlineCode was set. */
  inlineScripts?: number
  /** HTML, only when collectInlineCode was set. */
  inlineStyles?: number
  /** CSS: the top-level rules. */
  rules?: CssRule[]
  /** CSS. */
  ruleCount?: number
  /** CSS. */
  symbolCount?: number
  /** JS: whether the parser finished without a syntax error. */
  ok?: boolean
  /** JS. */
  partCount?: number
  /** JS: the named things in scope. */
  symbols?: JsSymbol[]
}

/**
 * Runs the engine's lexer over some source.
 *
 * @throws {TypeError} When the input is not a string or bytes, or options are
 *   not an object, or "language" is missing or is not one this engine has.
 * @throws {BuildFailure} When the engine refused the request. A source the lexer
 *   could not read resolves with a populated "errors" list instead — that is an
 *   answer, not a refusal.
 */
export declare function lexer(
  input: CompileSource,
  options: LexOptions
): Promise<LexResult>

/**
 * Runs the engine's parser over some source.
 *
 * @throws {TypeError} When the input is not a string or bytes, or options are
 *   not an object, or "language" is missing or is not one this engine has.
 * @throws {BuildFailure} When the engine refused the request. A syntax error
 *   resolves — for JS, with "ok" false — rather than rejecting.
 */
export declare function parse(
  input: CompileSource,
  options: ParseOptions
): Promise<ParseResult>

/**
 * Prints a parsed tree back to source.
 *
 * @throws {TypeError} When the handle is not a non-negative integer, when
 *   "language" is missing, or when the handle belongs to another language.
 *   Handles are numbered per language, so handle 0 names a valid tree in each of
 *   the three; passing one to the wrong language is refused here rather than
 *   reaching the engine, whose answer for it describes a tree the caller never
 *   passed.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function print(
  ast: AstHandle,
  options: PrintOptions
): Promise<PrintResult>

/** The class context() returns. Declared for instanceof. */
export declare const BuildContext: {
  new (...args: any[]): BuildContext
  prototype: BuildContext
}