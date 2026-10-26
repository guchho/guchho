// Type declarations for @guchho/wasm — the Guchho engine compiled to
// WebAssembly.
//
// The surface is two things: a createGuchho() factory that returns an engine
// object, and the same compiler stages as module-level functions running
// against one shared engine, for the caller who wants one lex or one build
// without holding an instance. index.cjs re-exports the same names through a
// CommonJS wrapper, so these declarations cover both entry points.
//
// The compiler shapes below — Location, Message, the Lex/Parse/Print options
// and results — mirror the ones the native guchho package publishes: the
// engine is the same C++ code behind the same service protocol, so a caller
// moving between the two changes the import name, not the types. They are
// declared here rather than imported because this package has no dependencies
// at all, and the fallback engine should not depend on the package it is a
// fallback for.
//
// Two surfaces are deliberately thin. Emscripten's filesystem and module
// objects are large, change with the SDK, and are reached here through the
// members this package's own README and tests use; anything past those is
// reachable through an index signature rather than declared and guessed at.
//
// Nothing here is generated: this file is written against index.mjs and the
// engine objects createGuchho() returns.

// ---------------------------------------------------------------------------
// Diagnostics and source positions — the shapes the engine's own service
// protocol answers with, unchanged.
// ---------------------------------------------------------------------------

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
 * undefined on every message this engine produces itself, because nothing here
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

/** The source a compile command reads. Bytes pass through as bytes. */
export type CompileSource = string | Uint8Array

/**
 * The handle a parse returned. Opaque to the caller: it is the engine's id for
 * a tree it holds, and only print() may use it, in the same language it came
 * from.
 */
export type AstHandle = number

/** The languages this engine has a lexer, a parser and a printer for. */
export type Language = "html" | "css" | "js"

// ---------------------------------------------------------------------------
// Tokens and tree summaries, per language.
// ---------------------------------------------------------------------------

export type HtmlTokenKind =
  | "comment"
  | "doctype"
  | "start-tag"
  | "end-tag"
  | "eof"
  | "character"
  | "null-character"
  | "whitespace-character"

/**
 * One HTML token. For a tag, "value" is the tag name and "length" says how
 * many attributes it carried.
 */
export interface HtmlToken extends SourcePosition {
  kind: HtmlTokenKind
  value: string
}

export type CssTokenKind =
  | "endOfFile" | "atKeyword" | "unterminatedString" | "badUrl" | "cdc" | "cdo"
  | "closeBrace" | "closeBracket" | "closeParen" | "colon" | "comma" | "delim"
  | "ampersand" | "asterisk" | "bar" | "caret" | "dollar" | "dot" | "equals"
  | "exclamation" | "greaterThan" | "lessThan" | "minus" | "plus" | "slash"
  | "tilde" | "dimension" | "function" | "hash" | "ident" | "number"
  | "openBrace" | "openBracket" | "openParen" | "percentage" | "semicolon"
  | "string" | "url" | "whitespace" | "symbol"

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
 * One node of the flattened tree an HTML parse returns. An element carries its
 * tag name and the names of its attributes — not their values, which belong in
 * the printed form. "depth" is how the flat list is a tree again.
 */
export interface HtmlNode {
  type: string
  depth: number
  tag?: string
  attributes?: string[]
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

// ---------------------------------------------------------------------------
// The compiler API: lex, parse and print. Each call is one request to the
// engine's own service dispatch, answered in process — the same protocol the
// native package sends over a pipe. The AST never crosses the boundary: a
// parse answers with a numeric handle, and a print turns one handle into
// text.
// ---------------------------------------------------------------------------

/** What every compile command answers with, before its own fields. */
export interface CompileResult {
  errors: Message[]
  warnings: Message[]
}

export interface CssMinifyOptions {
  minifyWhitespace?: boolean
  minifySyntax?: boolean
  minifyIdentifiers?: boolean
}

/** The source name a diagnostic blames. */
export interface CompileSourceFileOptions {
  sourcefile?: string
}

/**
 * The options every stage needs.
 *
 * "language" is required on all three. There is no default, because inferring
 * one from the source text is guessing — and a guess that is right 95% of the
 * time is a guess that silently mis-lexes the other 5%.
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
 * The token list is the language's own: HtmlToken, CssToken or JsToken.
 * "kind" is a string rather than one of three unions because the three kinds
 * are three unrelated vocabularies.
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
 * The summary fields are the language's — nodes for HTML, rules for CSS, ok
 * and symbols for JS — and are all optional, because which ones are present
 * depends on which language was parsed. A caller reading result.rules has
 * already said which language it asked for.
 */
export interface ParseResult extends CompileResult {
  /** The handle to hand to print(). */
  ast: AstHandle
  /** HTML: the flattened tree. */
  nodes?: HtmlNode[]
  /** HTML. */
  nodeCount?: number
  /** HTML and CSS. The number of import records the parser found. */
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

/** What print answers with, for every language. */
export interface PrintResult extends CompileResult {
  /** The printed source. */
  code: string
}

// ---------------------------------------------------------------------------
// The service protocol, for any command the engine serves beyond the three
// stages — pack, format-msgs, analyze-metafile.
// ---------------------------------------------------------------------------

/**
 * One raw service request. The fields past "command" are the engine's own,
 * per command; see docs/wasm.md and the native package's protocol.
 */
export interface ServiceRequest {
  command: string
  [field: string]: any
}

/**
 * One raw service answer, returned untouched.
 *
 * request() throws when the engine refused — an "error" field rather than a
 * populated "errors" list — so the "error" field is only ever seen through
 * requestRaw(). Both are declared here because the response is the same
 * object either way.
 */
export interface ServiceResponse {
  errors?: Message[]
  warnings?: Message[]
  /** Present when the engine refused the request. */
  error?: string
  [field: string]: any
}

// ---------------------------------------------------------------------------
// The engine object.
// ---------------------------------------------------------------------------

/** A virtual file tree: paths relative to the engine's root, and their bytes. */
export interface FilesMap {
  [path: string]: string | Uint8Array
}

/**
 * The engine's Emscripten filesystem. The members this package's README and
 * tests use are declared; the object has more, reachable through the index
 * signature.
 */
export interface EmscriptenFS {
  /** Returns the contents as a string when an encoding was given, else bytes. */
  readFile(path: string, options?: { encoding?: string } | string): any
  writeFile(path: string, data: string | Uint8Array, options?: object): void
  readdir(path: string): string[]
  mkdir(path: string, mode?: number): void
  chdir(path: string): void
  /** NODEFS is how the Node path mounts the host disk; see mountHost(). */
  mount(type: any, options: object, mountpoint: string): void
  [method: string]: any
}

/**
 * The compiled module itself: Emscripten's MODULARIZE result. ccall is how
 * index.mjs reaches the C ABI (guchho_build and friends); the rest of the
 * surface is the SDK's own.
 */
export interface GuchhoModule {
  ccall(name: string, returnType: string | null, argTypes: string[], args: any[]): any
  cwrap(name: string, returnType: string | null, argTypes: string[]): (...args: any[]) => any
  callMain(argv: string[]): number
  FS: EmscriptenFS
  [field: string]: any
}

export interface BuildOptions {
  /** Where the outputs go. A host path on Node, relative to "/" in memory. */
  outdir?: string
  minify?: boolean
  quiet?: boolean
  verbose?: boolean
  name?: string
}

/** What a successful build answers with. A failed build throws instead. */
export interface BuildResult {
  exitCode: number
}

/**
 * What pack() writes, and from what.
 *
 * Paths are paths in this engine's filesystem: host paths on Node (the host
 * tree is mounted under /host and the engine is chdir'd into the project), and
 * paths relative to "/" in a browser. Each input becomes a top-level name in
 * the archive, taken from its last path component.
 */
export interface PackOptions {
  /** The files and directories to put in the archive. */
  inputs: string[]
  /** Where to write the archive, inside the engine's filesystem. */
  outFile: string
  /** Archive format. Defaults to "zip"; any other value is a RangeError. */
  format?: "zip" | "tar" | "tar.gz"
  /** Deflate level, 0 to 9. Applies to zip and tar.gz, not tar. */
  level?: number
  /** Replace an existing outFile. Without it an existing file is an error. */
  overwrite?: boolean
  /** Timestamp every entry gets, instead of each source file's own. */
  date?: Date
  /** Unix permission bits to record for every entry, e.g. 0o755. */
  mode?: number
}

export interface PackResult {
  /** The archive, as the path it was written to. */
  path: string
  /** Its size in bytes. */
  size: number
  /** What was deliberately left out, such as a symlink or a cycle. */
  warnings: string[]
}

/** The options packBytes() takes: PackOptions without a path to write. */
export type PackBytesOptions = Omit<PackOptions, "outFile">

export interface PackBytesResult {
  /** The archive itself, in memory. */
  bytes: Uint8Array
  /** What was deliberately left out, such as a symlink or a cycle. */
  warnings: string[]
}

/**
 * What unpack() reads, and where its files go.
 *
 * Paths are paths in this engine's filesystem, as in PackOptions. The archive
 * is the only required option: outDir follows the archive's name, and the
 * format follows its extension and first bytes.
 */
export interface UnpackOptions {
  /** The archive to extract: a zip, tar or tar.gz file. */
  archive: string
  /** Where to extract. Defaults to the archive's name without its extension. */
  outDir?: string
  /** Archive format. Detected from the extension and magic bytes when omitted. */
  format?: "zip" | "tar" | "tar.gz"
  /** Replace existing files. Without it a collision is an error. */
  overwrite?: boolean
  /** Leading path components to drop, like tar --strip-components. */
  stripComponents?: number
  /** Restore recorded permissions where the host supports it. Defaults true. */
  preservePermissions?: boolean
  /** Restore recorded modification times where the host supports it. Defaults true. */
  preserveTimestamps?: boolean
}

export interface UnpackResult {
  /** The directory the files were extracted into. */
  outDir: string
  /** The files written, relative to outDir. */
  files: string[]
  /** Entries deliberately skipped, such as symlinks or unsafe paths. */
  warnings: string[]
}

/** The options unpackBytes() takes: UnpackOptions without paths on disk. */
export type UnpackBytesOptions = Omit<UnpackOptions, "archive" | "outDir">

export interface UnpackBytesResult {
  /** The extracted files, keyed by their path relative to the archive root. */
  files: { [path: string]: Uint8Array }
  /** Entries deliberately skipped, such as symlinks or unsafe paths. */
  warnings: string[]
}

/**
 * What createGuchho() returns: one engine instance.
 *
 * The compiler stages are the same functions the module-level exports are —
 * Promise-returning, resolving with the same shapes — bound to this
 * instance's module rather than to the shared one.
 */
export interface Engine {
  /** The compiled Emscripten module. */
  module: GuchhoModule
  /** The engine's filesystem. On Node it holds the host disk under /host. */
  FS: EmscriptenFS

  /**
   * Runs the CLI exactly as the native binary would: arguments are the ones
   * after the program name, and the return is the engine's exit code. The
   * host process's own exit status is left alone.
   */
  callMain(argv: string[]): number

  /**
   * One-shot build through the C ABI. Synchronous — the engine runs in this
   * process — and it throws with the engine's own diagnostic when the build
   * fails, rather than resolving with a nonzero code.
   */
  build(entry: string, options?: BuildOptions): BuildResult

  /** The engine version string, e.g. "1.0.7". */
  version(): string

  /**
   * One service request, in this process. Rejects when the engine refused,
   * carrying the engine's wording and the parsed errors.
   */
  request(payload: ServiceRequest): Promise<ServiceResponse>

  /**
   * The same request without the refusal handling: the engine's response is
   * returned as-is, "error" field and all, so the caller can unwrap it the
   * way its own layer expects.
   */
  requestRaw(payload: ServiceRequest): Promise<ServiceResponse>

  /**
   * The engine's own lexer, told which language it is for.
   *
   * @throws {TypeError} When the input is not a string or bytes, options is
   *   not an object, or "language" is missing or is not one this engine has.
   * @throws {Error} When the engine refused the request. A source the lexer
   *   could not read resolves with a populated "errors" list instead.
   */
  lexer(source: CompileSource, options: LexOptions): Promise<LexResult>

  /**
   * The engine's own parser. A syntax error resolves — for JS with "ok"
   * false — rather than rejecting; a refusal rejects.
   */
  parse(source: CompileSource, options: ParseOptions): Promise<ParseResult>

  /**
   * Prints a parsed tree back to source.
   *
   * @throws {TypeError} When the handle is not a non-negative integer, when
   *   "language" is missing, or when the handle belongs to another language.
   */
  print(ast: AstHandle, options: PrintOptions): Promise<PrintResult>

  /**
   * Writes an archive from files already in this instance's filesystem.
   *
   * @throws {TypeError} When an option is missing or has the wrong type.
   * @throws {RangeError} When a level, mode or format is out of range.
   * @throws {Error} When the engine refused the request.
   */
  pack(options: PackOptions): Promise<PackResult>

  /**
   * Extracts an archive into this instance's filesystem.
   *
   * @throws {TypeError} When an option is missing or has the wrong type.
   * @throws {RangeError} When the format or strip count is out of range.
   * @throws {Error} When the engine refused the request.
   */
  unpack(options: UnpackOptions): Promise<UnpackResult>

  /** Packs to memory instead of a path. */
  packBytes(options: PackBytesOptions): Promise<PackBytesResult>

  /** Unpacks bytes from memory and answers with the extracted files. */
  unpackBytes(bytes: Uint8Array, options?: UnpackBytesOptions): Promise<UnpackBytesResult>

  /** Writes more files into this instance's filesystem after creation. */
  loadFiles(files: FilesMap): void

  /** Releases the instance. The module frees itself with the host; this is
   * the hook a caller disposes through. */
  dispose(): void
}

/** What mountHost() returns: the helpers that translate paths the other way. */
export interface HostMount {
  FS: EmscriptenFS
  /** The host path that was mounted, e.g. "C:\\" or "/". */
  root: string
  /** Maps a path inside cwd to its "/host" counterpart. */
  virtual(path: string): string
}

export interface CreateOptions {
  /** Node only. The directory to mount and chdir to. Default process.cwd(). */
  cwd?: string
  /** A virtual file tree written in before the first call (browser/sandbox). */
  files?: FilesMap
  /** Replaces the sink the engine's stdout lines go to. */
  print?: (line: string) => void
  /** Replaces the sink the engine's stderr lines go to. */
  printErr?: (line: string) => void
}

/** What configure() takes. The same fields createGuchho() takes. */
export interface ConfigureOptions {
  files?: FilesMap
  print?: (line: string) => void
  printErr?: (line: string) => void
}

/**
 * Creates an engine instance.
 *
 * On Node the host file system is mounted and the working directory is
 * changed to cwd, so entry points, outdirs and resolved imports behave as
 * they do in the native binary and outputs land on the real disk. When a
 * files map is given — the browser path — the tree is written into memory
 * instead and nothing touches a disk.
 */
export declare function createGuchho(options?: CreateOptions): Promise<Engine>

/**
 * Mounts the host and runs the CLI in one call: the same path the npm bin
 * (bin/guchho.js) takes. Arguments are the ones after the program name.
 */
export declare function runCli(argv: string[]): Promise<number>

/**
 * Maps an absolute host path to its "/host" counterpart inside the engine's
 * filesystem: "C:\\proj" becomes "/host/proj", "/home/u/proj" becomes
 * "/host/home/u/proj". Node only — a browser has no absolute paths.
 */
export declare function toVirtual(absPath: string): string

/**
 * Mounts the host file system at "/host" and chdirs into the mount point for
 * cwd. Node only; createGuchho() does this for you unless a files map says
 * otherwise.
 */
export declare function mountHost(module: GuchhoModule, cwd?: string): HostMount

// ---------------------------------------------------------------------------
// The module-level functions.
//
// They share one lazily-created engine. configure() sets what that engine is
// created from and must come before the first of them; after the engine
// exists, everything routes to the same instance. A caller who needs more
// than one engine — or a different tree per build — uses createGuchho().
// ---------------------------------------------------------------------------

/**
 * Sets the virtual file tree the shared engine is created with, and the
 * diagnostic sinks it writes to.
 *
 * @throws {Error} When the shared engine already exists: configure() must
 *   come before the first build, lex, parse or print.
 */
export declare function configure(options?: ConfigureOptions): void

/**
 * Writes files into the shared engine's tree after it has been created. The
 * same as calling loadFiles on an engine from createGuchho().
 */
export declare function loadFiles(files: FilesMap): Promise<void>

/**
 * The engine's lexer, against the shared engine.
 *
 * @throws {TypeError} When the input is not a string or bytes, options is not
 *   an object, or "language" is missing or is not one this engine has.
 */
export declare function lexer(source: CompileSource, options: LexOptions): Promise<LexResult>

/** The engine's parser, against the shared engine. */
export declare function parse(source: CompileSource, options: ParseOptions): Promise<ParseResult>

/**
 * Prints a parsed tree back to source, against the shared engine.
 *
 * @throws {TypeError} When the handle is not a non-negative integer, when
 *   "language" is missing, or when the handle belongs to another language.
 */
export declare function print(ast: AstHandle, options: PrintOptions): Promise<PrintResult>

/**
 * One build through the shared engine. Rejects with the engine's own
 * diagnostic when the build fails.
 */
export declare function build(entry: string, options?: BuildOptions): Promise<BuildResult>

/** Writes an archive through the shared engine. */
export declare function pack(options: PackOptions): Promise<PackResult>

/** Extracts an archive through the shared engine. */
export declare function unpack(options: UnpackOptions): Promise<UnpackResult>

/** Packs to memory through the shared engine. */
export declare function packBytes(options: PackBytesOptions): Promise<PackBytesResult>

/** Unpacks bytes through the shared engine. */
export declare function unpackBytes(
  bytes: Uint8Array,
  options?: UnpackBytesOptions
): Promise<UnpackBytesResult>

/**
 * One service request against the shared engine. Rejects when the engine
 * refused the request, carrying the engine's wording and the parsed errors;
 * for the response untouched, use createGuchho() and its requestRaw().
 */
export declare function request(payload: ServiceRequest): Promise<ServiceResponse>

/** The shared engine's version string, e.g. "1.0.7". */
export declare function version(): Promise<string>

/**
 * The default export: the same factory as the named `createGuchho`. An ESM
 * `import createGuchho from "@guchho/wasm"` resolves to this, and it is the
 * same function the CommonJS `require("@guchho/wasm").default` gives.
 */
export default createGuchho
