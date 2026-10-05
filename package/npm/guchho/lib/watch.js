"use strict";

// The watcher: a warm build that runs again on demand, with events for the
// rebuilds it performs.
//
// The honest shape of this, first, because it is the thing a caller has to know
// before they rely on it.
//
// Guchho's service answers requests and never sends one of its own. The engine's
// watcher thread does notice a changed file and does rebuild — but it rebuilds
// inside the service, and nothing about that reaches this side. There is no
// notification packet on the protocol, so a host cannot be told "a file changed"
// without asking. Every buildEnd this watcher emits is therefore a rebuild *this
// handle performed*, either because the caller asked for one or because the
// interval asked for one. It is not a push, and it is not dressed up as one.
//
// What that costs is stated plainly rather than hidden. Asking for a rebuild runs
// a build pass, whether or not anything changed, because the engine reports the
// result of a pass and keeps no queue of them. So `interval` is opt-in and has a
// real price — one build per tick — and a caller who wants no wasted passes calls
// watcher.rebuild() from their own loop instead. A true push needs a notification
// packet with a reserved id and a dispatch that answers it, which is written down
// beside the protocol rather than approximated here with a timer.
//
//   const watcher = await watch({ entrypoints: ["src/index.js"] });
//   watcher.on("buildEnd", (result) => console.log(result.duration, "ms"));
//   await watcher.close();
//
// A build error does not stop the watcher. The whole point of watching is that
// the next save may be the one that fixes the last mistake, and a watcher that
// died on the first bad edit would have to be rebuilt by hand every time.

const { EventEmitter } = require("events");
const { context } = require("./context");
const { normalize } = require("./options");

/** How often an interval-driven watcher rebuilds when no interval was given. */
const DEFAULT_INTERVAL = 200;

/**
 * A running watcher.
 *
 * Not constructed by callers: watch() makes one and hands it back. The class is
 * exported so that `instanceof` is possible, which is the only reason it is
 * exported at all.
 */
class Watcher extends EventEmitter {
  /**
   * @param {BuildContext} ctx The context being watched.
   * @param {{delay?: number, interval?: number}} options
   */
  constructor(ctx, options = {}) {
    super();

    this._context = ctx;
    this._interval = normalizeInterval(options.interval);
    this._closed = false;
    // The most recent build result, so the opening build is observable: watch()
    // has already built by the time it resolves, and a caller who wants to know
    // whether that first build worked should not have to trigger a second one to
    // find out.
    this._result = null;
    // One build at a time. A tick that lands while the previous rebuild is still
    // running is dropped rather than queued: a queue would build up during a slow
    // build and then run every one of them back to back, which is the opposite of
    // what a watcher is for. The engine already serialises concurrent rebuilds
    // into one pass — but a caller who asked for a rebuild wants *that* rebuild,
    // and quietly receiving the previous one's result instead is worse than being
    // told the watcher was busy.
    this._building = false;
    this._timer = null;
  }

  /** The warm context behind the watcher, for a caller that wants the raw thing. */
  get context() {
    return this._context;
  }

  /** The interval this watcher drives itself at, in milliseconds. Zero if it does not. */
  get interval() {
    return this._interval;
  }

  /**
   * The result of the most recent build, or null before the first one.
   *
   * A copy is not made: a build result is this package's own plain object, and
   * handing out a reference to it is what lets a caller keep it and compare it
   * against the next one.
   */
  get result() {
    return this._result;
  }

  /** Whether close() has been called. */
  get closed() {
    return this._closed;
  }

  /**
   * Rebuilds, and says so on the way in and on the way out.
   *
   * The events are emitted around the build rather than instead of returning it,
   * so a caller can both listen and await the same pass.
   *
   * @returns {Promise<object>} A build result, as context().rebuild() returns one.
   * @throws {Error} When the watcher has been closed.
   */
  async rebuild() {
    this._assertLive();
    if (this._building) {
      throw new Error("this watcher is already rebuilding; await the build in flight first");
    }

    this._building = true;
    this._emit("buildStart");
    try {
      const result = await this._context.rebuild();
      this._result = result;
      this._emit("buildEnd", result);
      return result;
    } finally {
      this._building = false;
    }
  }

  /**
   * Stops watching and releases the context behind it.
   *
   * Idempotent, because it is reached from a finally block as often as from a
   * caller. A build already running is left to finish: the engine treats a stop
   * as a request to wrap up rather than an abort, and abandoning a build
   * half-written would be worse than a close() that takes a moment.
   */
  async close() {
    if (this._closed) return;
    this._closed = true;

    if (this._timer !== null) {
      clearInterval(this._timer);
      this._timer = null;
    }

    try {
      await this._context.cancel();
    } catch {
      // The engine's watcher may already be gone, in which case there is nothing
      // to stop. A close() that failed because the service died would turn a
      // working teardown into a failure, which is the opposite of what the caller
      // asked for.
    }
    await this._context.dispose();
  }

  // Starts driving itself, if an interval was asked for.
  //
  // The first tick is not immediate: watch() has already done the opening build,
  // and a second one before the caller has had a chance to attach a listener
  // would emit a buildEnd to nobody.
  _startTimer() {
    if (this._interval <= 0) return;

    this._timer = setInterval(() => {
      // A rejected rebuild is reported and the timer kept. A watcher that stopped
      // driving itself because one build failed would fail exactly once, and the
      // next save — the one that fixes it — would go unnoticed.
      this.rebuild().catch((error) => this._emitError(error));
    }, this._interval);
  }

  // Emits an error, unless nobody is listening.
  //
  // EventEmitter throws an unhandled 'error' event, which is right for a class
  // whose whole job is events and wrong here: a caller who only wants the
  // rebuild results should not have to attach an error listener to use a watcher,
  // and a throw from inside a timer callback is an unhandled rejection that takes
  // the process down. So the error is dropped when unhandled, and the build error
  // itself is still in every result's "errors" list for a caller who wants it.
  _emitError(error) {
    if (this.listenerCount("error") === 0) return;
    try {
      this.emit("error", error);
    } catch {
      // An error listener that throws is a bug in the listener, and this is
      // already the path that exists to contain bugs in listeners. Letting it out
      // here would put it back on the rebuild's own path, which is the thing the
      // try/catch above exists to keep off it.
    }
  }

  // Emits, and keeps a listener's own failure from becoming the caller's.
  //
  // A listener that throws is a bug in the listener. Letting it out turns a
  // successful rebuild into a failed call, and the caller then believes the build
  // failed when it did not — so it is reported and the build still counts as done.
  _emit(event, ...args) {
    try {
      this.emit(event, ...args);
    } catch (error) {
      this._emitError(error);
    }
  }

  _assertLive() {
    if (this._closed) {
      throw new Error("this watcher has been closed and cannot be used again");
    }
  }
}

// Lets `await using watcher = await watch(...)` release it at the end of a scope,
// where forgetting a close() would leave the engine watching files forever.
if (typeof Symbol.asyncDispose === "symbol") {
    Watcher.prototype[Symbol.asyncDispose] = function () {
        return this.close();
    };
}

// An interval, or zero for "do not drive yourself".
//
// A negative interval is refused rather than folded into zero, because the two
// readings are opposites — "as fast as possible" and "never" — and a caller who
// meant one and got the other would find out by watching nothing happen.
function normalizeInterval(given) {
    if (given === undefined || given === null) return DEFAULT_INTERVAL;
    if (typeof given !== "number" || !Number.isFinite(given) || given < 0) {
        throw new TypeError(`watch "interval" must be a number of milliseconds, not ${JSON.stringify(given)}`);
    }

    // Zero means never, and is a real setting rather than an absence: a caller
    // who wants to drive rebuilds from their own loop has to be able to ask for
    // that, and leaving the interval out already means the opposite. A fractional
    // interval is rounded because no timer can honour one — setInterval
    // truncates it anyway, so rounding here keeps the reported value honest.
    return Math.floor(given);
}

/**
 * Starts watching a build.
 *
 * The opening build has already run by the time this resolves, so the outputs
 * exist and `watcher.result` is that build's result.
 *
 * @param {object} [config] The same configuration build() takes, plus an optional
 *   "watch" section:
 * @param {object} [config.watch]
 * @param {number} [config.watch.delay] The engine's quiet period before a change
 *   triggers a rebuild, which is what makes a burst of saves one build.
 * @param {number} [config.watch.interval] How often this handle rebuilds on its
 *   own, in milliseconds. Defaults to 200. Zero or false to drive it yourself by
 *   calling rebuild().
 * @returns {Promise<Watcher>}
 * @throws {TypeError} When the configuration is not an object, names a watch option
 *   the watcher does not have, or gives an interval that is not a number.
 */
async function watch(config = {}) {
    const { build, watch: watchOptions } = normalize(config);
    const settings = watchOptions === null ? {} : watchOptions;

    const ctx = await context({ build });
    try {
        await ctx.watch({ delay: settings.delay });

        const watcher = new Watcher(ctx, settings);

        // The opening build, before the handle is handed back, so a caller never
        // receives a watcher that has not built yet. Its failures are in the
        // result rather than thrown: a project that does not compile the first
        // time is a normal state for a watcher to be in, and the next save is
        // what may fix it.
        watcher._result = await ctx.rebuild();
        watcher._startTimer();

        return watcher;
    } catch (error) {
        await ctx.dispose();
        throw error;
    }
}

module.exports = { Watcher, watch, DEFAULT_INTERVAL };
