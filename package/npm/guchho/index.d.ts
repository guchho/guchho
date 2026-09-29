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

/**
 * Pretty-prints a metafile.
 *
 * The same question "analyzeMetafile" used to answer, under the name the
 * surface now publishes.
 */
export declare function analyze(metafile: Metafile | string): Promise<string>

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

export interface LexHTMLResult extends CompileResult {
  tokens: HtmlToken[]
  count: number
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

export interface LexCSSOptions {
  /** Comment tokens, which the lexer keeps out of "tokens". Off by default. */
  includeComments?: boolean
}

export interface LexCSSResult extends CompileResult {
  tokens: CssToken[]
  /** Only when includeComments was set. */
  comments?: CssComment[]
  count: number
}

/** One JS token. Its kind is the grammar's own name. */
export interface JsToken extends SourcePosition {
  kind: string
  value: string
}

export interface LexJSResult extends CompileResult {
  tokens: JsToken[]
  count: number
}

/** The source name a compile-stage diagnostic blames. Defaults to "<stdin>". */
export interface CompileSourceFileOptions {
  sourcefile?: string
}

/**
 * One node of the flattened tree parseHTML returns. An element carries its tag
 * name and the names of its attributes — not their values, which belong in the
 * printed form. "depth" is how the flat list is a tree again.
 */
export interface HtmlNode {
  type: string
  depth: number
  tag?: string
  attributes?: string[]
}

export interface ParseHTMLOptions extends CompileSourceFileOptions {
  /** Parse as a fragment: no html/head/body wrappers and no doctype complaint. */
  fragment?: boolean
  /** Count the imports the parser resolved. */
  collectImportRecords?: boolean
  /** Count inline script and style elements. */
  collectInlineCode?: boolean
}

export interface ParseHTMLResult extends CompileResult {
  /** The handle to hand to transformHTML or printHTML. */
  ast: AstHandle
  nodes: HtmlNode[]
  nodeCount: number
  /** Only when collectImportRecords was set. */
  importRecords?: number
  /** Only when collectInlineCode was set. */
  inlineScripts?: number
  /** Only when collectInlineCode was set. */
  inlineStyles?: number
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

export interface ParseCSSOptions extends CompileSourceFileOptions, CssMinifyOptions {}

export interface ParseCSSResult extends CompileResult {
  /** The handle to hand to transformCSS or printCSS. */
  ast: AstHandle
  rules: CssRule[]
  ruleCount: number
  symbolCount: number
  importRecords: number
}

/** A named thing in scope, as parseJS saw it. */
export interface JsSymbol {
  name: string
  useCount: number
}

export interface ParseJSResult extends CompileResult {
  /** The handle to hand to transformJS or printJS. */
  ast: AstHandle
  /** Whether the parser could finish the file without a syntax error. */
  ok: boolean
  partCount: number
  symbols: JsSymbol[]
}

/** What a transform did, named. A CSS transform names the passes it ran. */
export type TransformPass = 'removeDeadRules'

/**
 * The name of a pass that actually ran. The html and js transforms answer with
 * an empty list and a note: nothing is rewritable in them yet, so "nothing
 * ran" is the pass list.
 */
export interface TransformAudit {
  passes: TransformPass[]
}

/** The answer of transformCSS. "removed" counts rules dropped, and the
 * resulting "ruleCount" is what is left. */
export interface TransformCSSResult extends CompileResult, TransformAudit {
  /** The handle to hand to printCSS. Not the handle that was passed in. */
  ast: AstHandle
  removed: number
  ruleCount: number
}

/** The answer of transformHTML and transformJS. "note" says in prose why no
 * pass ran. */
export interface TransformIdentityResult extends CompileResult, TransformAudit {
  /** The handle to hand to the matching print*. Not the handle passed in. */
  ast: AstHandle
  note: string
  /** Where the tree came from, for diagnostics. */
  sourcefile: string
}

export interface TransformCSSOptions {
  /** Drop rules that use no selector reachable from the stylesheet. */
  removeDeadRules?: boolean
}

export interface PrintHTMLOptions {
  /** Reindent rather than stream. Off by default. */
  pretty?: boolean
  /** Minify the printed document. */
  minify?: boolean
  /** Whether script elements run, which print can ask about. */
  scriptingEnabled?: boolean
}

export interface PrintCSSOptions extends CssMinifyOptions {
  /** Escape non-ASCII. */
  asciiOnly?: boolean
}

export interface PrintJSOptions extends CssMinifyOptions {
  /** Escape non-ASCII. */
  asciiOnly?: boolean
}

export interface PrintResult extends CompileResult {
  /** The printed source. */
  code: string
}

/**
 * Lexes HTML into a token list.
 *
 * @throws {TypeError} When the input is not a string or bytes.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function lexHTML(input: CompileSource): Promise<LexHTMLResult>

/**
 * Lexes CSS into a token list, with comments kept to their own list.
 *
 * @throws {TypeError} When the input is not a string or bytes, or options are
 *   not an object.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function lexCSS(input: CompileSource, options?: LexCSSOptions): Promise<LexCSSResult>

/**
 * Lexes JS into a token list.
 *
 * @throws {TypeError} When the input is not a string or bytes.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function lexJS(input: CompileSource): Promise<LexJSResult>

/**
 * Parses HTML into a handle and a flattened tree. A document without a doctype
 * is reported, not refused: the answer still carries the tree.
 *
 * @throws {TypeError} When the input is not a string or bytes, or options are
 *   not an object.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function parseHTML(
  input: CompileSource,
  options?: ParseHTMLOptions
): Promise<ParseHTMLResult>

/**
 * Parses CSS into a handle and a rule list.
 *
 * @throws {TypeError} When the input is not a string or bytes, or options are
 *   not an object.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function parseCSS(
  input: CompileSource,
  options?: ParseCSSOptions
): Promise<ParseCSSResult>

/**
 * Parses JS into a handle, its parts and its symbols. A syntax error resolves
 * — with "ok" false and the errors filled in — rather than rejecting.
 *
 * @throws {TypeError} When the input is not a string or bytes, or options are
 *   not an object.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function parseJS(
  input: CompileSource,
  options?: ParseCSSOptions
): Promise<ParseJSResult>

/**
 * Transforms the tree a parseHTML returned. Consumes the handle and answers
 * with a new one to the same tree; the passes list says what ran.
 *
 * @throws {TypeError} When the handle is not a non-negative integer.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function transformHTML(ast: AstHandle): Promise<TransformIdentityResult>

/**
 * Transforms the tree a parseCSS returned. Consumes the handle and answers
 * with a new one; "passes" names the pass that ran.
 *
 * @throws {TypeError} When the handle is not a non-negative integer.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function transformCSS(
  ast: AstHandle,
  options?: TransformCSSOptions
): Promise<TransformCSSResult>

/**
 * Transforms the tree a parseJS returned. Consumes the handle and answers with
 * a new one to the same tree; the passes list says what ran.
 *
 * @throws {TypeError} When the handle is not a non-negative integer.
 * @throws {BuildFailure} When the engine refused the request.
 */
export declare function transformJS(ast: AstHandle): Promise<TransformIdentityResult>

/**
 * Prints the tree a parseHTML (or transformHTML) returned.
 *
 * @throws {TypeError} When the handle is not a non-negative integer.
 * @throws {BuildFailure} When the engine refused the request, including a
 *   handle from another language.
 */
export declare function printHTML(
  ast: AstHandle,
  options?: PrintHTMLOptions
): Promise<PrintResult>

/**
 * Prints the tree a parseCSS (or transformCSS) returned.
 *
 * @throws {TypeError} When the handle is not a non-negative integer.
 * @throws {BuildFailure} When the engine refused the request, including a
 *   handle from another language.
 */
export declare function printCSS(
  ast: AstHandle,
  options?: PrintCSSOptions
): Promise<PrintResult>

/**
 * Prints the tree a parseJS (or transformJS) returned.
 *
 * @throws {TypeError} When the handle is not a non-negative integer.
 * @throws {BuildFailure} When the engine refused the request, including a
 *   handle from another language.
 */
export declare function printJS(
  ast: AstHandle,
  options?: PrintJSOptions
): Promise<PrintResult>

/** The class context() returns. Declared for instanceof. */
export declare const BuildContext: {
  new (...args: any[]): BuildContext
  prototype: BuildContext
}