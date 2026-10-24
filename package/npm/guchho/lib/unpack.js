"use strict";

// unpack(options) — one archive in, its files out.
//
// The mirror of pack(), and the same contract: the request is validated before
// anything is sent, a failure throws, and a success returns the answer as data.
// What is different is the direction. There is no tree to walk and no archive to
// write; there is an archive to read, a directory to create, and whatever was
// deliberately left out — a symlink the extractor will not follow, a path that
// would escape the destination, an entry with too few components to strip.
//
// Everything here is checked before the service is asked, for the reason a
// TypeError rather than an engine error is the right answer to "archive is not
// a string": the engine can only say it refused, whereas the caller can be told
// what is wrong with a shape the caller built. Anything that only the engine can
// know — that the archive is missing, that the format is wrong for its bytes,
// that a file already exists — comes back as a BuildFailure with the engine's
// own words in it.

const { getService } = require("./transport");
const { toUnpackResult } = require("./convert");

// The formats unpack() accepts, kept in step with guchho::unpack::kFormats in
// include/guchho/unpack.hpp. The list is written out rather than discovered:
// asking the engine would cost a service start, and a caller's own options
// should be checkable without one.
const SUPPORTED_FORMATS = ["zip", "tar", "tar.gz"];

/**
 * Extracts an archive.
 *
 * @param {object} options
 * @param {string} options.archive The archive to read. Zip, tar and tar.gz are
 *   recognised by extension and by their first bytes.
 * @param {string} [options.outDir] Where to extract. Defaults to the archive's
 *   name without its extension, created if it is missing.
 * @param {string} [options.format] Which archive format to read. One of `zip`,
 *   `tar` or `tar.gz`. Left out, the format is detected from the extension and
 *   the archive's magic bytes.
 * @param {boolean} [options.overwrite] Replace existing files. Without it an
 *   existing file is an error rather than something to destroy.
 * @param {number} [options.stripComponents] Leading path components to drop
 *   from every entry, as `tar --strip-components` does. Entries with too few
 *   components are skipped with a warning.
 * @param {boolean} [options.preservePermissions] Recorded permissions are
 *   restored where the host file system supports them. Defaults to true.
 * @param {boolean} [options.preserveTimestamps] Modification times are restored
 *   where the host file system supports them. Defaults to true.
 * @returns {Promise<{outDir: string, files: string[], warnings: string[]}>}
 * @throws {TypeError} When an option is missing or has the wrong type.
 * @throws {RangeError} When the format is not one this engine can read, or a
 *   strip count is not a whole number.
 * @throws {BuildFailure} When the engine refused the request.
 */
async function unpack(options) {
  if (options === null || typeof options !== "object" || Array.isArray(options)) {
    throw new TypeError("unpack needs an options object");
  }

  if (typeof options.archive !== "string" || options.archive.length === 0) {
    throw new TypeError('unpack needs "archive" as a path');
  }

  const request = {
    command: "unpack",
    archive: options.archive,
  };

  if (options.outDir !== undefined && options.outDir !== null) {
    if (typeof options.outDir !== "string") {
      throw new TypeError('"outDir" must be a string');
    }
    if (options.outDir.length === 0) {
      throw new TypeError(
        '"outDir" must not be empty; leave it out to use the archive name'
      );
    }
    request.outDir = options.outDir;
  }

  if (options.format !== undefined && options.format !== null) {
    if (typeof options.format !== "string") {
      throw new TypeError('"format" must be a string');
    }
    if (options.format.length === 0) {
      throw new RangeError(
        '"format" must not be empty; leave it out to detect it from the archive'
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

  if (options.overwrite !== undefined && options.overwrite !== null) {
    if (typeof options.overwrite !== "boolean") {
      throw new TypeError('"overwrite" must be a boolean');
    }
    request.overwrite = options.overwrite;
  }

  if (options.stripComponents !== undefined && options.stripComponents !== null) {
    if (
      typeof options.stripComponents !== "number" ||
      !Number.isInteger(options.stripComponents)
    ) {
      throw new TypeError('"stripComponents" must be an integer');
    }
    if (options.stripComponents < 0) {
      throw new RangeError('"stripComponents" must not be negative');
    }
    request.stripComponents = options.stripComponents;
  }

  if (
    options.preservePermissions !== undefined &&
    options.preservePermissions !== null
  ) {
    if (typeof options.preservePermissions !== "boolean") {
      throw new TypeError('"preservePermissions" must be a boolean');
    }
    request.preservePermissions = options.preservePermissions;
  }

  if (
    options.preserveTimestamps !== undefined &&
    options.preserveTimestamps !== null
  ) {
    if (typeof options.preserveTimestamps !== "boolean") {
      throw new TypeError('"preserveTimestamps" must be a boolean');
    }
    request.preserveTimestamps = options.preserveTimestamps;
  }

  const service = await getService();
  const response = await service.call(request);
  return toUnpackResult(response);
}

module.exports = { unpack };
