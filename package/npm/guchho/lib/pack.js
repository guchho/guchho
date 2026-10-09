"use strict";

// pack(options) — paths in, one archive out.
//
// The same contract as build(): the request is validated before anything is
// sent, a failure throws, and a success returns the answer as data. What is
// different is that there is no graph to resolve and nothing to report but a
// path, a size and whatever was deliberately left out — a symlink, a cycle, a
// directory the output file is standing inside of.
//
// Everything here is checked before the service is asked for, for the reason
// a TypeError rather than an engine error is the right answer to "inputs is
// not a list": the engine can only say it refused, whereas the caller can be
// told what is wrong with a shape the caller built. Anything that only the
// engine can know — that the level is refused, that two inputs collide on one
// archive path, that a file vanished between walking and writing — comes back
// as a BuildFailure with the engine's own words in it.

const { getService } = require("./service");
const { toPackResult } = require("./convert");

// The formats pack() accepts, kept in step with guchho::pack::kFormats in
// include/guchho/pack.hpp. The list is written out rather than discovered:
// asking the engine would cost a service start, and a caller's own options
// should be checkable without one.
const SUPPORTED_FORMATS = ["zip", "tar", "tar.gz"];

// The one format that stores its entries rather than compressing them, so
// that a level paired with it is refused here rather than sent on. Kept in
// step with guchho::pack::LevelAppliesTo.
const UNCOMPRESSED_FORMATS = ["tar"];

/**
 * Writes an archive.
 *
 * @param {object} options
 * @param {string[]} options.inputs The files and directories to put in the
 *   archive. Each becomes a top-level name taken from its last path component,
 *   so `dist` and `site/build` each arrive under their own name.
 * @param {string} options.outFile Where to write. The parent directory is
 *   created if it is missing; the file itself is written to a temporary path
 *   and moved into place, so a caller reading it after a failure is reading
 *   nothing rather than a half-written archive.
 * @param {string} [options.format] Which archive format to write. One of
 *   `zip` (the default), `tar` or `tar.gz`. Anything else is a RangeError
 *   thrown before the engine is asked.
 * @param {number} [options.level] Deflate level, 0 to 9. 0 stores. The engine's
 *   default is 6. Does not apply to `tar`, which stores its entries
 *   uncompressed; a level paired with `tar` is a RangeError.
 * @param {boolean} [options.overwrite] Replace an existing outFile. Without it
 *   an existing file is an error rather than something to destroy.
 * @param {Date} [options.date] The timestamp every entry gets. Without it each
 *   entry keeps the time of the file it came from.
 * @param {number} [options.mode] Unix permission bits to record for every file
 *   and directory, as `0o755` and friends. Without it each entry keeps the
 *   permissions of the file it came from.
 * @returns {Promise<{path: string, size: number, warnings: string[]}>}
 * @throws {TypeError} When an option is missing or has the wrong type.
 * @throws {RangeError} When a level, a mode or a date is out of range, or when
 *   the format is not one this engine can write.
 * @throws {BuildFailure} When the engine refused the request.
 */
async function pack(options) {
  if (options === null || typeof options !== "object" || Array.isArray(options)) {
    throw new TypeError("pack needs an options object");
  }

  const inputs = options.inputs;
  if (!Array.isArray(inputs) || inputs.length === 0) {
    throw new TypeError('pack needs "inputs" as a non-empty array of paths');
  }
  for (const entry of inputs) {
    if (typeof entry !== "string") {
      throw new TypeError('every entry in "inputs" must be a string');
    }
  }

  if (typeof options.outFile !== "string" || options.outFile.length === 0) {
    throw new TypeError('pack needs "outFile" as a path');
  }

  const request = {
    command: "pack",
    // Copied rather than shared: a caller who keeps the array and pushes to it
    // while the request is in flight would be editing a request the service
    // has not read yet.
    inputs: inputs.slice(),
    outFile: options.outFile,
  };

  if (options.format !== undefined && options.format !== null) {
    if (typeof options.format !== "string") {
      throw new TypeError('"format" must be a string');
    }
    if (options.format.length === 0) {
      throw new RangeError(
        '"format" must not be empty; leave it out to use the default'
      );
    }
    if (!SUPPORTED_FORMATS.includes(options.format)) {
      const supported = SUPPORTED_FORMATS.map((f) => JSON.stringify(f)).join(", ");
      throw new RangeError(
        `unsupported archive format: ${JSON.stringify(options.format)}; ` +
          `only ${supported} are supported today`
      );
    }
    request.format = options.format;
  }

  if (options.level !== undefined && options.level !== null) {
    if (typeof options.level !== "number" || !Number.isInteger(options.level)) {
      throw new TypeError('"level" must be an integer');
    }
    if (options.level < 0 || options.level > 9) {
      throw new RangeError('"level" must be from 0 to 9');
    }
    // The level is spelled correctly and paired with a format that cannot use
    // it. Worth the same code as any other mistake the caller can be told
    // about without starting the engine.
    if (request.format !== undefined && UNCOMPRESSED_FORMATS.includes(request.format)) {
      throw new RangeError(
        `compression level ${options.level} does not apply to the ` +
          `${JSON.stringify(request.format)} format; tar stores its entries ` +
          "uncompressed. Leave the level off, or write zip or tar.gz."
      );
    }
    request.level = options.level;
  }

  if (options.overwrite !== undefined && options.overwrite !== null) {
    if (typeof options.overwrite !== "boolean") {
      throw new TypeError('"overwrite" must be a boolean');
    }
    request.overwrite = options.overwrite;
  }

  if (options.date !== undefined && options.date !== null) {
    if (!(options.date instanceof Date)) {
      throw new TypeError('"date" must be a Date');
    }
    const milliseconds = options.date.getTime();
    if (!Number.isFinite(milliseconds)) {
      throw new RangeError('"date" is not a valid Date');
    }
    // Seconds, as a string. The protocol's number is a 32-bit integer, and an
    // epoch in seconds stops fitting in one in 2038 — sending a number would
    // have it truncated on the way in and the archive would come back with a
    // plausible, wrong timestamp rather than an error.
    request.date = String(Math.floor(milliseconds / 1000));
  }

  if (options.mode !== undefined && options.mode !== null) {
    if (typeof options.mode !== "number" || !Number.isInteger(options.mode)) {
      throw new TypeError('"mode" must be an integer');
    }
    if (options.mode < 0 || options.mode > 0o7777) {
      throw new RangeError('"mode" must be Unix permission bits, from 0 to 0o7777');
    }
    request.mode = options.mode;
  }

  const service = await getService();
  const response = await service.call(request);
  return toPackResult(response);
}

module.exports = { pack };
