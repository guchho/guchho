"use strict";

// The version, read from the package's own metadata.
//
// Read from package.json rather than kept as a number here, because a version
// written in two places is a version that is right in one of them. It is read
// once and cached, because resolving a path on a property read is not free and
// version is read often enough by tooling to notice.
//
// This is the package's version, not the binary's and not the protocol's, and
// the three are allowed to differ: a host can be several patch releases ahead of
// a binary it is talking to, which is the whole reason the protocol version is
// negotiated at all.

const path = require("path");

let cached = null;

function version() {
  if (cached === null) {
    cached = require(path.join(__dirname, "..", "package.json")).version;
  }
  return cached;
}

module.exports = { version };
