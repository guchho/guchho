"use strict";

// The failure shapes the package publishes, and the one function that turns a
// refusal into one of them.
//
// These live outside lib/service.js because a build in the browser names the
// same classes without importing the native pipe. lib/service.js re-exports
// both from here, so a caller that reached them through the service still does,
// and lib/index.js puts them on the public surface.

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

// Turns the engine's refusal shape into a rejection, and leaves a success alone.
// Every answer carries "errors"; a request the engine would not even begin
// carries the reason in "error" as well, which is a worse failure than a build
// error and says so in its own words.
//
// This is the service-call shape rather than the compiler-stage shape: a build,
// a transform and a pack treat a populated "errors" list as a failure, where a
// lex or a parse treats it as the answer. lib/compile.js keeps its own reader
// for that reason.
function unwrap(response) {
  if (response === null || typeof response !== "object") {
    throw new ServiceError("the Guchho service sent something that was not a response");
  }

  const errors = Array.isArray(response.errors) ? response.errors : [];
  const warnings = Array.isArray(response.warnings) ? response.warnings : [];

  if (typeof response.error === "string") {
    throw new BuildFailure(
      [
        {
          id: "protocol-error",
          pluginName: "",
          text: response.error,
          location: null,
          notes: [],
          detail: undefined,
        },
      ],
      warnings
    );
  }

  if (errors.length > 0) throw new BuildFailure(errors, warnings);
  return response;
}

module.exports = { ServiceError, BuildFailure, unwrap };
