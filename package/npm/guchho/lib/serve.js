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
// once the server is listening, so the address is a value rather than something
// to be discovered later by watching a log:
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

const { context } = require("./context");
const { normalize } = require("./options");

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
 * @param {number} [config.server.port] The port to bind. Zero asks the operating
 *   system for a free one, which is why the port is reported rather than assumed.
 * @param {string} [config.server.servedir] The directory to serve, when it is not
 *   the build's output directory.
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
        const info = await ctx.serve({
            servedir: server.servedir,
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

module.exports = { DevServer, serve };
