"use strict";

// The native transport: the child process over a pipe, registered as the one
// the orchestrators use.
//
// This module exists so that lib/index.js can say "this is a Node process, use
// the native service" in one line, and so that the browser entry can say the
// same about the WebAssembly engine without either of them editing the
// orchestrators. Requiring it installs it.

const { setTransport } = require("./transport");
const { getService, stopService } = require("./service");
const { compilerRequest } = require("./engine");
const { withConfigPlugins } = require("./config");

setTransport({ getService, compilerRequest, withConfigPlugins, stopService });

module.exports = { getService, compilerRequest, withConfigPlugins, stopService };
