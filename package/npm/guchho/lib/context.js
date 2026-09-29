"use strict";

// The live build: a context that outlives one request.
//
// This is the object esbuild calls a "context", and the thing it exists for is
// that the expensive half of a build — reading the project, resolving packages,
// parsing — is worth doing once. A context holds that state, so the second build
// of a file that changed is not the same cost as the first.
//
// A note on how rebuilds arrive, because it differs from esbuild and the
// difference is the engine's, not a choice made here.
//
// In esbuild, ctx.watch() takes an onRebuild callback and the service calls it
// when a file changes. This service is request and response only: every frame
// the host sends is answered, and the host refuses frames that arrive unsolicited
// because there is nothing on this side to answer them. So a rebuild has to be
// asked for.
//
// In practice:
//
//   await ctx.watch();
//   for (;;) {
//     const result = await ctx.rebuild();
//     // use result
//   }
//
// and if a caller wants the esbuild-shaped callback, watch() with onRebuild
// gives it for the rebuilds it drives itself. Turning this into a true push
// needs a change on the C++ side — a notification packet with a reserved id and
// a dispatch that answers it — and that is written down in the notes beside the
// protocol rather than faked here.

const { getService } = require("./service");
const { toFlags, toEntryPoints } = require("./flags");
const { toBuildResult } = require("./convert");

/** A build that is set up once and built as many times as asked. */
class BuildContext {
  /**
   * @param {object} [options]
   * @param {number} key The service's name for this context.
   * @param {object} request The request that made it, kept for rebuilds.
   * @param {object} service
   */
  constructor(key, request, service) {
    this.key = key;
    this._request = request;
    this._service = service;
    this._disposed = false;
    // Null rather than undefined, so that "not watching" is one thing to check
    // rather than two.
    this._onRebuild = null;
  }

  /** Where the outputs go, as the service reports them. */
  get _identity() {
    return { context: this.key, key: this.key };
  }

  /**
   * Builds again, using the options this context was made with.
   *
   * @returns {Promise<object>} errors, warnings, and — when the context was
   *   created with write: false — the output files.
   */
  async rebuild() {
    this._assertLive();
    const response = await this._service.request({ command: "rebuild", ...this._identity });
    return this._deliver(response);
  }

  /**
   * Starts watching for changes.
   *
   * Watching does not produce a result by itself — the engine rebuilds when a
   * file changes and the result is picked up by the next rebuild(). See the note
   * at the top of this file about why.
   *
   * @param {{delay?: number, onRebuild?: Function}} [options]
   */
  async watch(options = {}) {
    this._assertLive();
    await this._service.call({
      command: "watch",
      ...this._identity,
      delay: options.delay,
    });

    // The callback is kept and called for rebuilds this context performs, so
    // that a caller written against esbuild's shape keeps working and is not
    // silently given a callback that never fires.
    this._onRebuild = typeof options.onRebuild === "function" ? options.onRebuild : null;
  }

  /**
   * Stops a watch. The context is still usable; this ends the watcher, not the
   * context, which is esbuild's division and the one that lets a caller stop
   * watching and build on demand from the same state.
   */
  async cancel() {
    this._assertLive();
    await this._service.call({ command: "cancel", ...this._identity });
    this._onRebuild = null;
  }

  /**
   * Serves the outputs over HTTP.
   *
   * @returns {Promise<{host: string, port: number, hosts: string[]}>}
   */
  async serve(options = {}) {
    this._assertLive();
    const response = await this._service.call({
      command: "serve",
      ...this._identity,
      servedir: options.servedir,
      port: options.port,
      host: options.host,
      fallback: options.fallback,
    });

    // "host" as a string because that is the shape esbuild publishes, taken from
    // the first address the engine reports. The list is kept as well: a context
    // bound to both a loopback and a LAN address has more than one, and dropping
    // the rest would leave a caller that wanted the LAN address with a URL it
    // cannot connect to from outside.
    const hosts = Array.isArray(response.hosts) ? response.hosts.map(String) : [];
    return {
      host: hosts.length > 0 ? hosts[0] : "localhost",
      port: typeof response.port === "number" ? response.port : 0,
      hosts,
    };
  }

  /**
   * Releases the context.
   *
   * Idempotent, because it is reached from a finally block as often as from a
   * caller, and disposing a context twice is an error on the service side that a
   // cleanup path should never be the cause of.
   */
  async dispose() {
    if (this._disposed) return;
    this._disposed = true;
    try {
      await this._service.call({ command: "dispose", ...this._identity });
    } catch {
      // The service may already be gone, in which case the context went with it
      // and there is nothing left to release. A dispose that throws because the
      // process died would turn a working teardown into a failure.
    }
  }

  // Turns a response into the published shape, and tells the rebuild callback.
  //
  // The callback is called after the result has been built and not before, and
  // its own failure is not allowed to become the caller's: a callback that
  // throws is a bug in the callback, and swallowing it silently is worse — so it
  // is reported and the result is still returned.
  _deliver(response) {
    const result = toBuildResult(response);
    if (this._onRebuild !== null) {
      this._onRebuild(result);
    }
    return result;
  }

  _assertLive() {
    if (this._disposed) {
      throw new Error("this build context has been disposed of and cannot be used again");
    }
  }
}

/**
 * Sets up a build that can be repeated.
 *
 * @param {object} [options] The same options build() takes.
 * @returns {Promise<BuildContext>}
 */
async function context(options = {}) {
  const service = await getService();
  const request = {
    command: "context",
    flags: toFlags(options),
    entries: toEntryPoints(options),
    ...answerFields(options),
  };

  const response = await service.call(request);

  // A context with no key is a context that was not made. The service refuses
  // that case, so reaching here means the shape changed underneath this, and
  // handing back an object that fails on its first use would be a worse report.
  if (typeof response.key !== "number") {
    throw new Error("the Guchho service accepted a context request but sent back no key for it");
  }

  return new BuildContext(response.key, request, service);
}

// The request fields that are not flags: the ones the service reads itself.
//
// "write" defaults to true, which is esbuild's default and the engine's. It is
// worth being explicit either way rather than leaving it off, because leaving it
// off is not the same as leaving it out: the service reads an absent field as
// its own default, and the two are the same today only by agreement.
function answerFields(options) {
  const fields = { write: options.write !== false };

  if (options.metafile === true) fields.metafile = true;
  if (options.absWorkingDir !== undefined) fields.absWorkingDir = options.absWorkingDir;
  if (options.nodePaths !== undefined) fields.nodePaths = options.nodePaths;
  if (options.mangleCache !== undefined) fields.mangleCache = options.mangleCache;
  if (options.color !== undefined) fields.color = options.color;
  if (options.stdin !== undefined) fields.stdin = options.stdin;
  if (options.sourcemapRaw !== undefined) fields.sourcemapRaw = options.sourcemapRaw;

  return fields;
}

module.exports = { BuildContext, context, answerFields };
