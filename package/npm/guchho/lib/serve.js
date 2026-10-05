"use strict";

// The dev server: a build, kept warm, with the engine's own HTTP server in
// front of it.
//
// This module does not implement an HTTP server. It asks the service for one —
// the same server `guchho serve` gets, through the same context — and holds the
// address it answers with. A second server in JavaScript would mean two
// implementations of the same thing: two sets of rules about what to serve, two
// sets of bugs, and a caller who could not tell which one they were talking to.
//
// The shape is the one a caller actually wants to hold on to. serve() resolves
// once the server is listening over a finished build, so the address is a value
// rather than something to be discovered later by watching a log:
//
//   const server = await serve({ build: {...}, server: { port: 3000 } });
//   console.log(server.url);
//   await server.close();
//
// The build itself is a context, and it is kept rather than discarded: the point
// of a dev server is that the second request is cheaper than the first, and
// rebuilding from scratch on every request would throw that away. So server.build()
// (and the rebuild() it wraps) work on the same warm context watch() and serve()
// share, and closing the server disposes of it.

const path = require("path");
const { context } = require("./context");
const { normalize } = require("./options");

// Where a build writes when the caller did not say. Guchho's own default, read
// from the engine's options rather than guessed at here.
const DEFAULT_OUTDIR = "dist";

/**
 * A running dev server.
 *
 * Not constructed by callers: serve() makes one and hands it back. The class is
 * exported so that `instanceof` is possible, which is the only reason it is
 * exported at all.
 */
class DevServer {
  /**
   * @param {BuildContext} ctx The context whose outputs are being served.
   * @param {{host: string, port: number, hosts: string[]}} info The addresses
   *   the service reported.
   */
  constructor(ctx, info) {
    this._context = ctx;
    this._host = info.host;
    this._port = info.port;
    this._hosts = info.hosts;
    this._closed = false;
  }

  /** The first address the engine bound. */
  get host() {
    return this._host;
  }

  /** The port the engine bound. Zero only if it reported no port at all. */
  get port() {
    return this._port;
  }

  /**
   * Every address the engine bound.
   *
   * A server on both a loopback and a LAN address has more than one, and a
   * caller that wants the LAN address — to reach it from a phone, say — cannot
   * get it from "host".
   */
  get hosts() {
    return this._hosts.slice();
  }

  /**
   * The URL to open.
   *
   * Built from an address that can actually be connected to, which is not always
   * the address the engine bound. Binding "0.0.0.0" is how a server says "every
   * interface", and it is not a destination: a browser asked to open
   * http://0.0.0.0:3000 reaches the machine by a route it does not have, and on
   * several platforms refuses outright. So a wildcard host is reported as it
   * bound but pointed at as localhost, which is where a browser on the same
   * machine should go. The bound addresses are still on `hosts` for a caller that
   * needs the LAN address.
   */
  get url() {
    const reachable = this._host === "0.0.0.0" || this._host === "::" ? "localhost" : this._host;
    return `http://${formatHost(reachable)}:${this._port}`;
  }

  /** The warm context behind the server, for a caller that wants the raw thing. */
  get context() {
    return this._context;
  }

  /** Whether close() has been called. */
  get closed() {
    return this._closed;
  }

  /**
   * Rebuilds, without restarting the server.
   *
   * @returns {Promise<object>} A build result, as context().rebuild() returns one.
   */
  async rebuild() {
    this._assertLive();
    return this._context.rebuild();
  }

  /**
   * Stops the server and releases the context behind it.
   *
   * Idempotent, because it is reached from a finally block as often as from a
   * caller, and stopping a server that is already stopped should be the same
   * quiet nothing as releasing a context twice.
   */
  async close() {
    if (this._closed) return;
    this._closed = true;
    await this._context.dispose();
  }

  _assertLive() {
    if (this._closed) {
      throw new Error("this dev server has been closed and cannot be used again");
    }
  }
}

// An IPv6 literal needs its brackets in a URL. "http://::1:3000" is not an
// address at all — the parser cannot tell the last colon from the port's.
function formatHost(host) {
  return host.includes(":") && !host.startsWith("[") ? `[${host}]` : host;
}

// Lets `await using server = await serve(...)` release it at the end of a scope,
// where forgetting a close() would leave the engine's HTTP server listening and
// the project's output files open.
if (typeof Symbol.asyncDispose === "symbol") {
    DevServer.prototype[Symbol.asyncDispose] = function () {
        return this.close();
    };
}

/**
 * Starts a dev server.
 *
 * @param {object} [config] The same configuration build() takes, plus an
 *   optional "server" section:
 * @param {object} [config.server]
 * @param {string} [config.server.host] The interface to bind. The engine's own
 *   default, which binds every interface.
 * @param {number} [config.server.port] The port to bind. Zero asks for the
 *   engine's own choice — it tries 8000 upwards and takes the first that is free —
 *   rather than an arbitrary one, which is why the port that was bound is
 *   reported on the handle instead of being assumed to be the one asked for.
 * @param {string} [config.server.servedir] The directory to serve. Defaults to the
 *   build's output directory, which is what makes the server serve the build at
 *   all; name it only to serve somewhere else.
 * @param {string} [config.server.fallback] The file to serve for a path that
 *   matches nothing, which is how a single-page app's routes are handled.
 * @returns {Promise<DevServer>}
 * @throws {TypeError} When the configuration is not an object, or names a server
 *   option the server does not have.
 */
async function serve(config = {}) {
    const { build, server: serverOptions } = normalize(config);
    const server = serverOptions === null ? {} : serverOptions;

    // "open" is accepted in the server section because a caller reading a config
    // file written for `guchho serve` expects to be able to leave it there. It is
    // not honoured: the service has no way to open a browser, and this module
    // will not shell out to one behind the caller's back. It is refused rather
    // than ignored, because a dev server that silently did not open is worse than
    // one that says it cannot.
    if (server.open !== undefined) {
        throw new TypeError(
            'server "open" is not supported by the JavaScript API: this module will not ' +
                "open a browser. Open server.url yourself, or use `guchho serve` on the command line."
        );
    }

    const ctx = await context({ build });
    try {
        // Build before listening, not after.
        //
        // The service starts a context's server and then builds into the directory
        // it is serving, but the two are not ordered against each other: a request
        // that arrives in between is answered from a directory that is not there
        // yet, with a 404, by a server the caller has already been handed. That is
        // not a race in the tests so much as a race in the contract — it is rare on
        // an idle machine and common on a loaded one, which is the worst way for a
        // bug to be shaped.
        //
        // Building first means that when this resolves the outputs are on disk and
        // the server is listening over them, so the first request is served like
        // every one after it. It also means a build that fails fails here, where
        // the caller still has a stack trace pointing at their configuration.
        await ctx.rebuild();

        const info = await ctx.serve({
            servedir: servedirFor(build, server),
            port: server.port,
            host: server.host,
            fallback: server.fallback,
        });
        return new DevServer(ctx, info);
    } catch (error) {
        // A server that failed to start must not leave a warm context behind: the
        // caller never received a handle, so nothing they hold would ever release
        // it, and the engine would keep the project's file handles open until the
        // process exited.
        await ctx.dispose();
        throw error;
    }
}

// The directory to serve, which is the build's output directory unless the caller
// named another one.
//
// This is not a convenience default; without it the server serves nothing. The
// engine serves a context's build output from an in-memory layer, and the service
// command that starts a context's server leaves that layer empty — it is the
// `guchho serve` command that wires a build's outputs in, and that is a different
// path. So a context served with no servedir answers every request with a 404,
// which is a dev server that is running, reachable, and serves no files. Pointing
// it at the output directory is what makes it serve the build.
//
// A relative directory is resolved here rather than sent on, because the engine
// resolves a relative path against the service's working directory — which is not
// necessarily the caller's, and is not the build's working directory either.
function servedirFor(build, server) {
    if (server.servedir !== undefined) {
        return server.servedir;
    }

    return path.resolve(build.absWorkingDir === undefined ? process.cwd() : build.absWorkingDir, build.outdir === undefined ? DEFAULT_OUTDIR : build.outdir);
}

module.exports = { DevServer, serve };
