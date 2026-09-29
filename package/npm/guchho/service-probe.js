"use strict";
const path = require("path");
const os = require("os");
const fs = require("fs");

(async () => {
  const { getService, stopService } = require("./lib/service");
  const { toFlags, toEntryPoints } = require("./lib/flags");

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-svc-"));
  fs.writeFileSync(path.join(dir, "entry.js"), "export const answer = 41 + 1;\n");
  const startDir = process.cwd();
  process.chdir(dir);

  const service = await getService();
  console.log("handshake ok, service started");

  const r1 = await service.call({
    command: "build",
    flags: toFlags({ bundle: true, format: "esm", write: false, minify: false, logLevel: "silent" }),
    entries: toEntryPoints({ entryPoints: ["entry.js"] }),
    write: false,
  });
  console.log("build:      errors=%d warnings=%d outputs=%d",
    r1.errors.length, r1.warnings.length, (r1.outputFiles || []).length);
  for (const f of r1.outputFiles || []) {
    console.log("  out %s (%d bytes) hash=%s", f.path, f.contents.length, f.hash);
    console.log("  ----\n" + Buffer.from(f.contents).toString("utf8") + "\n  ----");
  }

  const r2 = await service.call({
    command: "transform",
    flags: toFlags({ minify: false, logLevel: "silent" }),
    sourcefile: "in.ts",
    loader: "ts",
    input: "const x: number = 1;\nconsole.log(x);\n",
  });
  console.log("transform:  errors=%d, code=%d bytes", r2.errors.length, r2.code.length);
  console.log("  ----\n" + Buffer.from(r2.code).toString("utf8") + "  ----");

  // A refusal must reject, not resolve with a flag on it.
  try {
    await service.call({
      command: "build",
      flags: toFlags({ bundle: true, logLevel: "silent" }),
      entries: toEntryPoints({ entryPoints: ["does-not-exist.js"] }),
      write: false,
    });
    console.log("REFUSAL:  !! resolved, expected a throw");
  } catch (e) {
    console.log("REFUSAL:  rejected with %s: %j", e.name, e.errors.map(x => x.text));
  }

  // A context has to survive more than one request, which is the entire point.
  const c = await service.call({
    command: "context",
    flags: toFlags({ bundle: true, format: "esm", write: false, logLevel: "silent" }),
    entries: toEntryPoints({ entryPoints: ["entry.js"] }),
    write: false,
  });
  const key = c.key;
  console.log("context:   key=%j, errors=%d", key, c.errors.length);

  // A distinguishable edit: the first source folds to 42 and so does 6*7, which
  // would have made a stale rebuild look like a fresh one.
  fs.writeFileSync(path.join(dir, "entry.js"), "export const answer = 'rebuilt';\n");
  const r3 = await service.call({ command: "rebuild", context: key, key });
  console.log("rebuild:   errors=%d, %d outputs",
    r3.errors.length, (r3.outputFiles || []).length);
  const built = (r3.outputFiles || [])[0];
  if (built) {
    const text = Buffer.from(built.contents).toString("utf8");
    console.log("  picked up the edit: %s", text.includes("rebuilt") ? "yes" : "NO (stale)");
    console.log("  ----\n" + text + "  ----");
  }

  await service.call({ command: "dispose", context: key, key });
  console.log("dispose:   ok");

  const fm = await service.call({
    command: "format-msgs",
    messages: [{ text: "x is not defined", location: { file: "entry.js", namespace: "file", line: 1, column: 0, length: 1, lineText: "x" }, notes: [] }],
    kind: "error",
    color: false,
    terminalWidth: 80,
  });
  console.log("format-msgs:", JSON.stringify(fm.logs));

  const mf = await service.call({ command: "analyze-metafile", metafile: JSON.stringify({ inputs: {}, outputs: {} }) });
  console.log("analyze:   %d chars", mf.text.length);

  await stopService();
  console.log("stopped cleanly");
  process.chdir(startDir);
  fs.rmSync(dir, { recursive: true, force: true });
  console.log("cleaned up (a directory that is still the cwd cannot be removed on Windows)");
})().catch((e) => {
  console.error("PROBE FAILED:", e);
  process.exit(1);
});
