"use strict";

const { getService } = require("./transport");
const { isRecord, toLintResult } = require("./convert");

const RULE_LEVELS = new Set(["off", "warn", "warning", "error"]);

function optionalString(options, name) {
  const value = options[name];
  if (value === undefined) return undefined;
  if (typeof value !== "string" || value.length === 0) {
    throw new TypeError(`${name} must be a non-empty string`);
  }
  return value;
}

function optionalBoolean(options, name) {
  const value = options[name];
  if (value === undefined) return false;
  if (typeof value !== "boolean") {
    throw new TypeError(`${name} must be a boolean`);
  }
  return value;
}

function normalizeFiles(value) {
  if (value === undefined) return undefined;
  const files = Array.isArray(value) ? value : [value];
  if (files.length === 0) {
    throw new TypeError(
      "files must not be empty; omit it to lint the project's default files"
    );
  }
  for (const file of files) {
    if (typeof file !== "string" || file.length === 0) {
      throw new TypeError("every entry in files must be a non-empty string");
    }
  }
  return files;
}

function normalizeRules(value) {
  if (value === undefined) return [];
  if (!isRecord(value)) {
    throw new TypeError("rules must be an object mapping rule names to levels");
  }

  const flags = [];
  for (const [name, level] of Object.entries(value)) {
    if (name.length === 0) {
      throw new TypeError("every rule in rules must have a name");
    }
    if (typeof level !== "string" || !RULE_LEVELS.has(level)) {
      throw new TypeError(
        `the level for rule "${name}" must be "off", "warn", "warning", or "error"`
      );
    }
    flags.push(`--rule=${name}:${level}`);
  }
  return flags;
}

function normalizeMaxWarnings(value) {
  if (value === undefined) return undefined;
  if (!Number.isInteger(value) || value < 0) {
    throw new TypeError("maxWarnings must be a non-negative integer");
  }
  return value;
}

async function lint(options = {}) {
  if (!isRecord(options)) {
    throw new TypeError("lint takes an options object");
  }

  const files = normalizeFiles(options.files);
  const config = optionalString(options, "config");
  const absWorkingDir = optionalString(options, "absWorkingDir");
  const fix = optionalBoolean(options, "fix");
  const fixDryRun = optionalBoolean(options, "fixDryRun");
  const maxWarnings = normalizeMaxWarnings(options.maxWarnings);

  if (fix && fixDryRun) {
    throw new TypeError("fix and fixDryRun cannot both be true");
  }

  const flags = normalizeRules(options.rules);
  if (config !== undefined) flags.push(`--config=${config}`);
  if (fix) flags.push("--fix");
  if (fixDryRun) flags.push("--fix-dry-run");
  if (maxWarnings !== undefined) flags.push(`--max-warnings=${maxWarnings}`);

  const request = { command: "lint", flags };
  if (files !== undefined) request.files = files;
  if (absWorkingDir !== undefined) request.absWorkingDir = absWorkingDir;

  const service = await getService();
  const response = await service.call(request);
  return toLintResult(response, maxWarnings);
}

module.exports = { lint };
