#!/usr/bin/env node
// The CLI entry for @guchho/wasm: mount the host file system, hand the
// arguments to the engine's own main(), and exit with its exit code. The
// banner, the flag grammar and every diagnostic come from the engine, so
// this file is the entire difference between the wasm CLI and the native
// one.

import { runCli } from "../index.mjs";

const code = await runCli(process.argv.slice(2));
process.exit(typeof code === "number" ? code : 0);
