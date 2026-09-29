"use strict";

// The connection to "guchho --service=1".
//
// One process, started on demand, answering as many requests as are sent to it.
// The alternative — a process per build — is what the command line already is,
// and it is why this exists: a build costs a parser start, a config read and a
// cache warm-up, and a tool that rebuilds on every keystroke pays all three
// again every time. The engine's own caches live in this process, which is the
// entire reason the outputs come back the same way twice.
//
// What is deliberately not here: any timeout, any retry, and any reconnection.
// Each of those is a decision about what a caller wants when the bundler is
// broken, and a bundler that silently restarts is a bundler whose errors arrive
// out of order. What is here is the one thing that has to be true: a process
// that died takes its pending requests with it, and every one of them is told so
// rather than left waiting forever.

const child_process = require("child_process");
const { EventEmitter } = require("events");

const { getBinaryPath } = require("./main");
const {
  MAX_VERSION,
  DecodeError,
  encodePacket,
  readValue,
  FrameReader,
} = require("./protocol");

// The protocol version, which is not the package version.
//
// They answer different questions and they change for different reasons. The
// package version moves when the bundler does. This moves when the frames do,
// and it is the one thing that decides whether a host and a binary can talk at
// all — so it is the first thing said, before either side decodes anything from
// the other.
const PROTOCOL_VERSION = "1";

/** The service process died with a pending request outstanding. */
class ServiceError extends Error {
  constructor(message, details = {}) {
    super(message);
    this.name = "ServiceError";
    this.code = details.code;
    this.errors = details.errors;
    this.warnings = details.warnings;
  }
}

/** A request the service refused, carrying the engine's own wording. */
class BuildFailure extends Error {
  constructor(errors, warnings) {
    const first = errors.length > 0 ? errors[0] : null;
    super(first ? first.text : "the build failed");
    this.name = "BuildFailure";
    this.errors = errors;
    this.warnings = warnings;
  }
}

/**
 * A live connection to one Guchho process.
 *
 * Not exported: a caller gets a service by asking for a build, and the lifetime
 * is the package's problem rather than the caller's. That is what keeps the
 * "one process, many requests" promise from being a thing the caller has to
 * remember to hold.
 */
class Service extends EventEmitter {
  constructor() {
    super();
    this.child = null;
    this.reader = new FrameReader();
    this.nextId = 0;
    this.pending = new Map();
    this.stderr = [];
    this.stopped = false;
    this.exited = false;
    this.stopping = null;
  }

  /**
   * Starts the process and waits for it to say which protocol it speaks.
   * @returns {Promise<Service>}
   */
  async start() {
    if (this.child !== null) return this;

    const binary = getBinaryPath();
    this.child = child_process.spawn(binary, [`--service=${PROTOCOL_VERSION}`], {
      stdio: ["pipe", "pipe", "pipe"],
      // No shell. The binary path is this package's to choose and the argument
      // is a constant, but a shell in between would be a place where a path
      // with a space or a quote in it becomes something other than a path.
      shell: false,
      windowsHide: true,
    });

    this.child.stderr.on("data", (chunk) => this._onStderr(chunk));

    this.child.on("error", (error) => this._onGone(error));
    this.child.on("exit", (code, signal) => {
      this._onGone(
        new ServiceError(
          signal !== null
            ? `the Guchho service was stopped by ${signal}`
            : `the Guchho service exited with code ${code}`,
          { code }
        )
      );
    });

    // The handshake, before anything else is sent. Reading it here rather than
    // in the background is what turns "the binary is too old to talk to" into
    // an error at the first build, instead of a stream of nonsense a few
    // seconds later.
    //
    // Nothing else is reading stdout yet, and that is not an accident. The
    // greeting and the answers that follow it come through the same
    // reassembler, and a reassembler fed the same bytes twice does not see one
    // frame and a half of the next — it sees the tail of something it has
    // already handed over. The permanent reader is attached below, once the
    // greeting has been taken and there is nothing left to race it for.
    let greeting;
    try {
      greeting = await this._readFrame();
    } catch (error) {
      this.stop();
      throw error;
    }

    if (greeting === null) {
      this.stop();
      throw new ServiceError(
        `the Guchho service exited before saying which protocol it speaks. ` +
          `The binary at ${binary} is not a service, or is too old to be one.`
      );
    }

    const version = greeting.toString("utf8");
    if (version !== PROTOCOL_VERSION) {
      this.stop();
      throw new ServiceError(
        `this package speaks the service protocol "${PROTOCOL_VERSION}" and the binary at ` +
          `${binary} speaks "${version}". Update whichever of the two is older.`
      );
    }

    // The greeting is taken, so the permanent reader can take over. Everything
    // from here is an answer to something, and nothing is an answer to nothing.
    this.child.stdout.on("data", (chunk) => this._onStdout(chunk));

    // The process does not hold the event loop open.
    //
    // A child process and its pipes are libuv handles, and Node keeps running
    // while any of them is open. Without this, a script that built something and
    // then finished would sit there: the work is done, nothing is pending, and
    // the process will not exit because a bundler it no longer needs is still
    // reading from a pipe. It is a hang with no cause a caller can point at.
    //
    // The other half is ref() below, because unref'ing alone would be worse: an
    // unref'd process cannot keep itself alive to answer a request, so a script
    // whose only pending work is a build would exit before the build finished.
    // So the process is referenced exactly while something is waiting on it.
    this._unref();

    return this;
  }

  // Takes the process and its pipes out of the event loop's reference count.
  _unref() {
    const child = this.child;
    if (child === null) return;
    child.unref();
    if (typeof child.stdin.unref === "function") child.stdin.unref();
    if (typeof child.stdout.unref === "function") child.stdout.unref();
  }

  // Puts them back, for as long as somebody is waiting.
  _ref() {
    const child = this.child;
    if (child === null) return;
    child.ref();
    if (typeof child.stdin.ref === "function") child.stdin.ref();
    if (typeof child.stdout.ref === "function") child.stdout.ref();
  }

  /**
   * Sends one request and resolves with the answer.
   *
   * Rejects with a BuildFailure when the engine refused the request, and with a
   * ServiceError when the process could not answer at all. Those are different
   * failures with different fixes — one is a problem in the code being built,
   * the other is a problem with the installation — and lumping them together
   * would make the first look like the second.
   */
  request(payload) {
    if (this.child === null) {
      return Promise.reject(new ServiceError("the Guchho service has not been started"));
    }
    if (this.stopped) {
      return Promise.reject(new ServiceError("the Guchho service has been stopped"));
    }

    const id = this.nextId++;
    const frame = encodePacket(id, true, payload);

    return new Promise((resolve, reject) => {
      // Referenced for exactly as long as this request is outstanding. The count
      // rather than a flag, because two builds running at once must not release
      // each other's reference and leave the second one unheld while it waits.
      this.pending.set(id, {
        resolve: (value) => {
          this._settled(id);
          resolve(value);
        },
        reject: (error) => {
          this._settled(id);
          reject(error);
        },
      });

      this._ref();

      this.child.stdin.write(frame, (error) => {
        if (error) {
          const waiting = this.pending.get(id);
          if (waiting !== undefined) {
            this.pending.delete(id);
            waiting.reject(new ServiceError(`could not send to the Guchho service: ${error.message}`));
          }
        }
      });
    });
  }

  // Called when a request is answered, refused, or abandoned. Releases the
  // reference that request was holding, and only drops the process out of the
  // event loop when nothing is waiting on it any more.
  _settled(id) {
    this.pending.delete(id);
    if (this.pending.size === 0) this._unref();
  }

  /**
   * Asks the service to answer a request and turns "the engine said no" into a
   * rejection, so that a caller does not have to check for an "error" key on
   * every response.
   *
   * A refusal is a rejection rather than a resolved value with a flag on it
   * because a build that failed is an exception in every tool written against
   * esbuild, and a caller that has to remember to look is a caller that will
   * forget.
   */
  async call(payload) {
    const response = await this.request(payload);
    return unwrap(response);
  }

  _onStdout(chunk) {
    let frames;
    try {
      frames = this.reader.feed(chunk);
    } catch (error) {
      // A frame this version cannot read is not something to recover from: the
      // two sides are not speaking the same protocol, and every frame after it
      // would be read as nonsense. Stop and tell everyone waiting.
      this.stop();
      this._failAll(error);
      return;
    }

    for (const frame of frames) {
      let id;
      let value;
      try {
        if (frame.length < 4) throw new DecodeError("a frame had no room for a request id");
        const raw = frame.readUInt32LE(0);
        id = raw >>> 1;
        // The direction bit is the service's to set and not the host's to send.
        // A frame with the wrong bit is not a response to anything, so it is
        // reported rather than dispatched to a waiting caller.
        if ((raw & 1) === 0) {
          throw new DecodeError("the service sent a request, but this host has no callbacks to answer it with");
        }
        [value] = readValue(frame, 4);
      } catch (error) {
        this.stop();
        this._failAll(error);
        return;
      }

      const waiting = this.pending.get(id);
      if (waiting === undefined) {
        // An answer to a request that was already given up on, which is what a
        // cancelled or timed-out request looks like. Dropping it is right: the
        // caller has moved on, and delivering it to nobody would be a message
        // with no listener.
        continue;
      }
      this.pending.delete(id);
      waiting.resolve(value);
    }
  }

  _onStderr(chunk) {
    // Kept, and not printed. The engine logs as it builds, and on a service
    // that is a hundred lines of "Done in 40ms" that the caller has no way to
    // ask for or to turn off. They are held so a failure can quote what the
    // engine said at the time, which is the one moment a host wants them.
    this.stderr.push(chunk);
    // Bounded, because a long-lived process that watched a project for a day
    // would otherwise hold every line it ever printed.
    if (this.stderr.length > 256) this.stderr.shift();
    this.emit("stderr", chunk);
  }

  _onGone(error) {
    this.exited = true;
    this.stopped = true;
    this._failAll(error);
  }

  // Everyone waiting is told the process is gone. A promise that never settles
  // is worse than one that rejects: the caller is stuck holding a build that
  // will never finish, and nothing about it says why.
  //
  // Each goes through its own reject, which is what releases the reference it
  // was holding — dropping the map first would skip that and leave a reference
  // outstanding for a process that no longer exists.
  _failAll(error) {
    const waiting = [...this.pending.values()];
    for (const { reject } of waiting) reject(error);
  }

  _readFrame() {
    // The child is captured rather than reached through "this.child", because
    // the one path that matters here is the one where the process is already
    // gone by the time the listeners come off — a version mismatch stops the
    // service and then unwinds through here, and reaching through a nulled field
    // on the way out throws a second, unrelated error over the first.
    const child = this.child;
    const stream = child.stdout;

    return new Promise((resolve, reject) => {
      const onData = (chunk) => {
        cleanup();
        try {
          const frames = this.reader.feed(chunk);
          resolve(frames.length > 0 ? frames[0] : null);
        } catch (error) {
          reject(error);
        }
      };
      const onEnd = () => {
        cleanup();
        resolve(null);
      };
      const onError = (error) => {
        cleanup();
        reject(error);
      };
      const cleanup = () => {
        stream.off("data", onData);
        stream.off("end", onEnd);
        stream.off("error", onError);
      };

      stream.on("data", onData);
      stream.once("end", onEnd);
      stream.once("error", onError);
    });
  }

  /**
   * Ends the process, refusing anything still in flight.
   *
   * Idempotent, because the two ways it is called — the process exiting on its
   * own, and the package deciding it is finished — can happen in either order.
   *
   * Returns a promise for the process actually being gone, which is not the same
   * moment. Closing its input is a request; the process noticing, finishing the
   * build it is in the middle of, and letting go of every file it has open takes
   * a moment after that. A caller that removes a build's output directory the
   * instant this resolves gets EPERM on Windows, and a retry loop is a worse
   * answer than waiting for the thing that is actually true.
   */
  stop() {
    if (this.exited) return Promise.resolve();
    if (this.child === null) return Promise.resolve();

    this.stopped = true;

    if (this.stopping === null) {
      this.stopping = new Promise((resolve) => {
        this.child.once("exit", () => resolve());
        this.child.once("error", () => resolve());
      });

      try {
        if (this.child.stdin.writable) this.child.stdin.end();
      } catch {
        // Already gone. Nothing to close, and the exit handler above is what
        // settles the promise.
      }
    }

    this.child = null;
    this._failAll(new ServiceError("the Guchho service has been stopped"));
    return this.stopping;
  }
}

// Turns the service's refusal shape into a rejection, and leaves a success
// alone. Every answer carries "errors"; a request the engine would not even
// begin carries the reason in "error" as well, which is a worse failure than a
// build error and says so in its own words.
function unwrap(response) {
  if (response === null || typeof response !== "object") {
    throw new ServiceError("the Guchho service sent something that was not a response");
  }

  const errors = Array.isArray(response.errors) ? response.errors : [];
  const warnings = Array.isArray(response.warnings) ? response.warnings : [];

  if (typeof response.error === "string") {
    throw new BuildFailure(
      [{ id: "protocol-error", pluginName: "", text: response.error, location: null, notes: [], detail: undefined }],
      warnings
    );
  }

  if (errors.length > 0) throw new BuildFailure(errors, warnings);
  return response;
}

// The one process, shared by every request.
//
// Started on the first build rather than when this module is loaded, so that
// importing the package costs nothing: a script that only wants getBinaryPath()
// never starts a process it will not talk to.
let shared = null;
let starting = null;

function getService() {
  if (shared !== null && !shared.stopped) return Promise.resolve(shared);
  if (starting !== null) return starting;

  starting = new Service().start().then(
    (service) => {
      shared = service;
      starting = null;
      // Nothing else holds a reference to the process but this module, and a
      // Node process that exits with a pipe still open hangs rather than
      // exiting. Ending the input is the service's cue to finish and go.
      service.child.stdin.on("error", () => {});
      return service;
    },
    (error) => {
      starting = null;
      throw error;
    }
  );

  return starting;
}

/**
 * Ends the shared process, once it has actually ended.
 *
 * Awaiting this is the supported way to know the service is gone. It is what
 * lets a caller clean up after a build — remove its output directory, move a
 * file it just produced — without racing the process that had it open.
 */
async function stopService() {
  const service = shared;
  shared = null;
  starting = null;
  if (service !== null) await service.stop();
}

module.exports = {
  PROTOCOL_VERSION,
  MAX_VERSION,
  Service,
  ServiceError,
  BuildFailure,
  getService,
  stopService,
  unwrap,
};
