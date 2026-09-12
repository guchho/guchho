#!/usr/bin/env node

"use strict";

const { execFileSync } = require("child_process");
const path = require("path");

const { getBinaryPath } = require("../lib/main");

const binaryPath = getBinaryPath();

try {
  execFileSync(binaryPath, process.argv.slice(2), { stdio: "inherit" });
} catch (err) {
  if (err.status !== undefined) {
    process.exit(err.status);
  }
  throw err;
}
