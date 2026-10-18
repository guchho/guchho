"use strict";

// The version, as a constant.
//
// It used to be a function, and it is a string now because that is what the
// API means by "version": it names the package in the same place the manifest
// does, and a caller wanting to report it or compare it wants a value, not a
// call whose result they can miss. This is the intentional breaking change
// CHANGELOG.md names.
//
// Read from package.json rather than kept as a number here, because a version
// written in two places is a version that is right in one of them.
//
// This is the package's version, not the binary's and not the protocol's, and
// the three are allowed to differ: a host can be several patch releases ahead
// of a binary it is talking to, which is the whole reason the protocol version
// is negotiated at all.
//
// The import is written literally rather than built from __dirname: a path
// assembled at runtime is one a bundler cannot follow, and this module has to
// bundle for the browser, where it is still the package's own version the
// caller is asking for.

const version = require("../package.json").version;

module.exports = { version };