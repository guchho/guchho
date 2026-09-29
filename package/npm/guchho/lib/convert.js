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
 */
function toBuildResult(response) {
  const result = {
    errors: toMessages(response.errors),
    warnings: toMessages(response.warnings),
  };

  if (Array.isArray(response.outputFiles)) {
    result.outputFiles = response.outputFiles.map(toOutputFile);
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
  if (response.mangleCache !== undefined) {
    result.mangleCache = toMangleCache(response.mangleCache);
  }

  return result;
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
  toMessage,
  toMessages,
  toNote,
  toOutputFile,
  toMangleCache,
  toText,
  toBuildResult,
  toTransformResult,
};
