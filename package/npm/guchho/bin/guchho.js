#!/usr/bin/env node

"use strict";

const { execFileSync } = require("child_process");

const { getBinaryPath } = require("../lib/main");

// A "guchho.config.js" or "guchho.config.json" may name plugins, and a plugin
// is JavaScript the engine's process cannot call. When the nearest config names
// some, the command is run through the service instead of the bare binary, so a
// plugin host is on the far end of the pipe to answer the hooks. When it does
// not — which is most projects — the binary is run directly, exactly as before.
const BUILTIN_PLUGINLESS = ["build", "dev", "serve", "watch"];

const binaryPath = getBinaryPath();

function runBinary(args) {
  try {
    execFileSync(binaryPath, args, { stdio: "inherit" });
  } catch (err) {
    if (err.status !== undefined) {
      process.exit(err.status);
    }
    throw err;
  }
}

// The config's plugins, grouped by configuration element, or none. A config that
// cannot be read here is left for the engine to report in its own words — the
// fallback is the plain binary, not a second opinion about what is wrong with
// the file.
async function configPluginGroups() {
  try {
    const { loadConfigPluginGroups } = require("../lib/config");
    return await loadConfigPluginGroups(process.cwd());
  } catch {
    return [];
  }
}

async function runWithPlugins(args, pluginGroups) {
  const { Service } = require("../lib/service");
  const { makePluginHost, callWithPlugins } = require("../lib/plugins");

  const service = new Service();
  await service.start();

  // Ctrl-C at the terminal reaches this process and, through the shared process
  // group, the service child. This handler is the belt to that suspenders: it
  // makes sure the child is gone before this process is, so a run is not left
  // orphaned holding the terminal.
  const stop = () => {
    if (service.child !== null) {
      try {
        service.child.kill();
      } catch {
        // Already gone.
      }
    }
    process.exit(130);
  };
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);

  try {
    const response = await callWithPlugins(service, makePluginHost(pluginGroups), {
      command: "run",
      argv: args,
      configPluginsHandled: true,
    });
    await service.stop();
    process.exit(typeof response.code === "number" ? response.code : 0);
  } catch (error) {
    await service.stop();
    process.stderr.write((error && error.message ? error.message : String(error)) + "\n");
    process.exit(1);
  }
}

async function main() {
  const args = process.argv.slice(2);
  const command = args[0];

  if (!BUILTIN_PLUGINLESS.includes(command)) {
    runBinary(args);
    return;
  }

  const pluginGroups = await configPluginGroups();
  if (!pluginGroups.some((group) => group.length > 0)) {
    runBinary(args);
    return;
  }

  await runWithPlugins(args, pluginGroups);
}

main().catch((error) => {
  process.stderr.write((error && error.stack ? error.stack : String(error)) + "\n");
  process.exit(1);
});
