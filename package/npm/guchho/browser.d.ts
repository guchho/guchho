// Type declarations for the guchho package's browser entry — the "browser"
// export condition, lib/browser/index.js (and .mjs).
//
// Everything the Node entry publishes exists on this surface too, so the
// whole file after the header is a re-export of index.d.ts. What is added
// here are the two names that exist ONLY in the browser: configure() and
// loadFiles(). They are kept out of index.d.ts on purpose: declaring them for
// every consumer would hand a Node project two functions that are absent at
// runtime, and a typed call into a crash — which is the promise a declaration
// must not make.
//
// How a project lands here: TypeScript resolves this file through the
// package's "browser" types condition, so a browser-targeting project adds
// "browser" to compilerOptions.customConditions. Every other project resolves
// index.d.ts, which is the surface it can actually run.
//
// The runtime this describes is lib/browser/index.js, which exports exactly
// these names: the orchestrators, the three compiler stages, configure,
// loadFiles, the refusing watch/serve, the no-op stop, and the failure
// classes.

export * from "./index";

/**
 * Sets the virtual file tree the WASM engine is created with, and the
 * diagnostic sinks it writes to.
 *
 * Call this before the first build, lex, parse or print: the engine is created
 * once, and a browser build reads its entry points out of the in-memory tree it
 * was created with. Throws if the engine already exists.
 */
export declare function configure(options: {
  /** Virtual paths → file contents. The engine's whole filesystem. */
  files?: Record<string, string | Uint8Array>
  /** Receives each line the engine prints. */
  print?: (line: string) => void
  /** Receives each line the engine prints to stderr. */
  printErr?: (line: string) => void
}): void

/**
 * Writes files into the engine's in-memory tree after it has been created.
 *
 * For the browser the engine is usually created with a files map via
 * `configure()`; this is the path for adding more before a later build.
 */
export declare function loadFiles(
  files: Record<string, string | Uint8Array>
): Promise<void>
