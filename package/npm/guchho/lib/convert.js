"use strict";

// What the service says, turned into what a caller of guchho expects.
//
// Nothing here decides anything. Every field is either copied across, decoded
// from bytes, or filled in with a value that means "there is none" — the whole
// file is the seam between the engine's vocabulary and the published one, and the
// reason both sides can be written against their own names is that all the
// renaming happens in one place.
//
// The conversions that are not just copying:
//
//   - bytes become a string, because a transform's result is source text and a
//     caller concatenating it with other text should not have to know that it
//     arrived as a buffer;
//   - a "detail" sentinel becomes undefined, because the wire format has no way
//     to say "absent" for a value and inventing a number a caller would see
//     would be worse than the round trip through a sentinel;
//   - an output file gains a "text" that follows its "contents", because guchho
//     callers read one or the other and holding both in sync by hand is a bug
//     waiting for a caller to write.

// The service's way of saying "this message carries no detail".
//
// It is a number because the protocol has no undefined, and it is -1 rather
// than 0 because a detail of 0, false or "" are all perfectly good details that
// a caller might stash. A sentinel that collides with a real value is a value
// that gets destroyed.
const NO_DETAIL = -1;

/**
 * One message, in the shape guchho documents.
 *
 * Fields that were never there are added as undefined rather than left out. A
 * caller that does `message.notes.length` should not have to know which
 * messages came from a plugin and which did not.
 */
function toMessage(message) {
  return {
    id: typeof message.id === "string" ? message.id : "",
    pluginName: typeof message.pluginName === "string" ? message.pluginName : "",
    text: typeof message.text === "string" ? message.text : "",
    location: message.location ?? null,
    notes: Array.isArray(message.notes) ? message.notes.map(toNote) : [],
    detail: message.detail === NO_DETAIL ? undefined : message.detail,
  };
}

function toNote(note) {
  return {
    text: typeof note.text === "string" ? note.text : "",
    location: note.location ?? null,
  };
}

/** A list of messages, tolerating a list that is not one. */
function toMessages(messages) {
  return Array.isArray(messages) ? messages.map(toMessage) : [];
}

/**
 * One output file.
 *
 * "text" is a getter rather than a copy, and that is the whole point: a build
 * that produces twenty files would otherwise hold every one of them twice, once
 * as bytes and once as text, and the second copy is the larger of the two for
 * anything that is not already text. Reading it decodes on demand and throws it
 * away.
 */
function toOutputFile(file) {
  const contents = file.contents instanceof Uint8Array ? file.contents : new Uint8Array(0);
  return {
    path: typeof file.path === "string" ? file.path : "",
    contents,
    hash: typeof file.hash === "string" ? file.hash : "",
    get text() {
      return Buffer.from(contents.buffer, contents.byteOffset, contents.byteLength).toString("utf8");
    },
  };
}

/** The engine's list of {name, value} pairs, as the record guchho publishes. */
function toMangleCache(cache) {
  if (!Array.isArray(cache)) return undefined;

  // false rather than "" for an unmangled name, because "" is a name a caller
  // could legitimately have asked to be kept and cannot be told apart from
  // "this one was not mangled" if it stands for both.
  const out = {};
  for (const entry of cache) {
    if (entry === null || typeof entry !== "object") continue;
    if (typeof entry.name !== "string") continue;
    out[entry.name] = entry.value === false || entry.value === null || entry.value === undefined
      ? false
      : String(entry.value);
  }
  return out;
}

/** Bytes, decoded as UTF-8. A missing value decodes to "". */
function toText(value) {
  if (value === undefined || value === null) return "";
  if (typeof value === "string") return value;
  if (value instanceof Uint8Array) return Buffer.from(value.buffer, value.byteOffset, value.byteLength).toString("utf8");
  return String(value);
}

/**
 * A build response, in the shape guchho publishes.
 *
 * The optional parts are left undefined rather than being defaulted to empty,
 * because "there were no output files" and "output files were not collected" are
 * different answers and a caller that checks `.length` on the first when it
 * meant the second gets a build that looks empty rather than one that was never
 * asked.
 *
 * "outputs" and "inputs" are summaries rather than the full output files: a path
 * and a size is what a caller reporting on a build wants, and every byte of
 * every output is already in "outputFiles" for a build that was told not to
 * write them. "inputs" needs a metafile, so it is absent without one rather than
 * empty — the two mean different things to a caller counting files.
 */
function toBuildResult(response, duration) {
  const result = {
    // A build with warnings is a build that worked, so this is the errors list
    // and not the warnings list. It is derived rather than sent by the engine
    // because the engine reports failures by throwing and a build that reached
    // this line at all succeeded.
    success: (Array.isArray(response.errors) ? response.errors.length : 0) === 0,
    errors: toMessages(response.errors),
    warnings: toMessages(response.warnings),
  };

  if (Array.isArray(response.outputFiles)) {
    result.outputFiles = response.outputFiles.map(toOutputFile);
  }
  if (typeof response.duration === "number") {
    result.duration = response.duration;
  } else if (typeof duration === "number") {
    // Measured here rather than in the engine because a build that was asked to
    // write nothing still costs the same parse, and because the number a caller
    // wants is the one it waited for, which includes the service round trip.
    result.duration = duration;
  }

  // The paths and the sizes, from whichever source this build produced. The
  // output files are the direct answer when the build kept them; a metafile
  // describes them whether or not the bytes travelled.
  if (Array.isArray(response.outputFiles)) {
    result.outputs = response.outputFiles.map((file) => ({
      path: typeof file.path === "string" ? file.path : "",
      size: file.contents instanceof Uint8Array ? file.contents.byteLength : 0,
      hash: typeof file.hash === "string" ? file.hash : "",
    }));
  }

  if (typeof response.metafile === "string") {
    try {
      result.metafile = JSON.parse(response.metafile);
    } catch {
      // A metafile the engine wrote and this could not read is not a failed
      // build. The build's outputs are in result.outputFiles or on disk; only
      // the analysis is missing, and an absent metafile says that better than a
      // half-parsed object would.
      result.metafile = undefined;
    }
  }

  if (result.metafile !== undefined && isRecord(result.metafile)) {
    result.inputs = toInputSummaries(result.metafile);
    if (result.outputs === undefined) {
      result.outputs = toOutputSummaries(result.metafile);
    }
  }

  if (response.mangleCache !== undefined) {
    result.mangleCache = toMangleCache(response.mangleCache);
  }

  return result;
}

/** Whether a parsed metafile is worth reading a field out of. */
function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

/**
 * The inputs a metafile names, as a flat list of paths and sizes.
 *
 * The source of truth is the contribution map on each output — "inputs" beside an
 * output, mapping every file that ended up inside it to the bytes it contributed.
 * That is the bundler's own transitive graph, and it is the only place the full
 * set appears: the metafile's top-level "inputs" names the entry points and
 * nothing else, so a caller who wanted to know what a build read and read that
 * instead would be told about one file and would believe it.
 *
 * A metafile whose outputs carry no contribution map falls back to the top-level
 * inputs, which is a smaller answer but an honest one.
 *
 * Sizes are summed across outputs, so a file split across two chunks is reported
 * once at the total it contributed rather than twice at a partial amount.
 */
function toInputSummaries(metafile) {
  if (!isRecord(metafile)) return [];

  const contributed = new Map();
  const formats = new Map();

  for (const output of Object.values(metafile.outputs || {})) {
    if (!isRecord(output)) continue;

    for (const [path, contribution] of Object.entries(output.inputs || {})) {
      const bytes = isRecord(contribution)
        ? numberOr(contribution.bytesInOutput, 0)
        : numberOr(contribution, 0);
      contributed.set(path, (contributed.get(path) || 0) + bytes);
    }
    // The format is recorded once per input in the top-level map, and reading it
    // from there keeps a single answer to "how was this parsed".
    for (const [path, input] of Object.entries(metafile.inputs || {})) {
      if (isRecord(input) && typeof input.format === "string") formats.set(path, input.format);
    }
  }

  if (contributed.size === 0) {
    for (const [path, input] of Object.entries(metafile.inputs || {})) {
      if (isRecord(input)) contributed.set(path, numberOr(input.bytes, 0));
    }
  }

  return [...contributed].map(([path, bytes]) => ({
    path,
    bytes,
    format: formats.get(path),
  }));
}

/**
 * The outputs a metafile names, as a flat list of paths and sizes.
 *
 * This is what "outputs" means for a build that wrote its files: the build put
 * the bytes on disk and did not send them over a pipe nobody was going to read,
 * but the metafile still knows what it produced and how big each piece is.
 */
function toOutputSummaries(metafile) {
  if (!isRecord(metafile) || !isRecord(metafile.outputs)) return [];

  const summaries = [];
  for (const [path, output] of Object.entries(metafile.outputs)) {
    if (!isRecord(output)) continue;
    summaries.push({
      path,
      size: numberOr(output.bytes, 0),
    });
  }
  return summaries;
}

/**
 * The imports a metafile's outputs still carry.
 *
 * These are what the built code requires at runtime and did not get inlined. A
 * relative one is here too: bundle:false is the default, and under it a build
 * leaves sibling imports alone on purpose. Calling those "not dependencies"
 * because they are not bare specifiers would report an empty dependency list for
 * a bundle that plainly has some.
 *
 * Sorted and deduplicated across every output, because an analysis whose list
 * reorders between runs is one nobody can diff.
 */
function toDependencySummaries(metafile) {
  if (!isRecord(metafile) || !isRecord(metafile.outputs)) return [];

  const external = new Set();
  for (const output of Object.values(metafile.outputs)) {
    if (!isRecord(output) || !Array.isArray(output.imports)) continue;
    for (const edge of output.imports) {
      if (isRecord(edge) && typeof edge.path === "string") external.add(edge.path);
    }
  }
  return [...external].sort();
}

/** A finite number, or the fallback. A caller summing these gets a number. */
function numberOr(value, fallback) {
  return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

/** A transform response, in the shape guchho publishes. */
function toTransformResult(response) {
  return {
    code: toText(response.code),
    map: response.map !== undefined && response.map !== null ? toText(response.map) : "",
    errors: toMessages(response.errors),
    warnings: toMessages(response.warnings),
  };
}

module.exports = {
  NO_DETAIL,
  isRecord,
  toMessage,
  toMessages,
  toNote,
  toOutputFile,
  toMangleCache,
  toText,
  toBuildResult,
  toTransformResult,
  toInputSummaries,
  toOutputSummaries,
  toDependencySummaries,
};
