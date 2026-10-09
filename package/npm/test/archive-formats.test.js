// guchho.pack() with a format other than zip, checked against tools that are
// not guchho.
//
//     const { path } = await pack({ inputs: ["dist"], outFile: "out.tar" });
//
// The C++ tests read the archives back with their own readers, which is the
// only way to check a timestamp or a permission bit. What they cannot check is
// whether the thing they wrote is a tar file the rest of the world agrees is a
// tar file — so this file hands the archives to Node's own zlib and, when the
// machine has one, to the system tar, and asks those.
//
// Both are read-only checks on an archive guchho already wrote. Nothing here
// extracts into a tree it then compares file by file: a tar file that a
// reference reader can list, whose payload is the one guchho says it is, is
// the claim being made. A test that unzipped everything and diffed it would be
// checking the extraction, which is not what this package does.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const zlib = require("node:zlib");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");

const { cleanup, hasTar, makeTempDir, skipWithoutBinary, toTarPath } =
    require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };
const needsBinaryAndTar = {
    skip: skipWithoutBinary() || (hasTar() ? false : "no system tar on this machine"),
};

// A temporary tree with a file, a nested file and an empty directory — the
// three shapes a tar writer has to get right, and the last of which a zip
// writer is allowed to skip.
function makeTree() {
    const root = makeTempDir("formats");
    fs.mkdirSync(path.join(root, "dist", "assets"), { recursive: true });
    fs.mkdirSync(path.join(root, "dist", "emptydir"), { recursive: true });
    fs.writeFileSync(path.join(root, "dist", "app.js"), "let a = 1;\n");
    fs.writeFileSync(path.join(root, "dist", "assets", "logo.svg"), "<svg/>");
    return root;
}

// The first bytes of a file, without reading all of it.
function head(file, bytes) {
    const buffer = Buffer.alloc(bytes);
    const fd = fs.openSync(file, "r");
    try {
        fs.readSync(fd, buffer, 0, bytes, 0);
    } finally {
        fs.closeSync(fd);
    }
    return buffer;
}

// The system tar, run from the archive's own directory and naming it bare, for
// the two reasons a full Windows path is not given: tar reads "C:" as a remote
// host, and it reads backslashes as escapes. Both are the same traps the npm
// helper's own extraction has, and going through the same conversion keeps the
// two in step.
function systemTar(args, cwd) {
    const result = spawnSync("tar", args, { cwd, encoding: "utf8" });
    if (result.error) throw new Error(`could not run tar: ${result.error.message}`);
    return result;
}

describe("the formats pack() writes", () => {
    it("writes a tar file the system tar can list", needsBinaryAndTar, async () => {
        const root = makeTree();
        const outFile = path.join(root, "out.tar");

        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile,
            format: "tar",
        });

        // Listed, not extracted: "-t" reads the headers and stops, which is
        // the part that would fail if the headers were wrong.
        const listed = systemTar(["-tf", path.basename(outFile)], path.dirname(outFile));
        assert.equal(listed.status, 0, `tar -tf failed:\n${listed.stderr}`);

        const names = listed.stdout.split(/\r?\n/).filter(Boolean);
        assert.ok(names.includes("dist/"), `dist/ should be listed, got:\n${listed.stdout}`);
        assert.ok(names.includes("dist/app.js"), `dist/app.js should be listed`);
        assert.ok(
            names.includes("dist/assets/logo.svg"),
            `dist/assets/logo.svg should be listed`
        );
        // An empty directory is a real entry with no bytes under it, and a
        // reader that skipped it would produce a tree missing a directory the
        // caller asked to be there.
        assert.ok(
            names.includes("dist/emptydir/"),
            `dist/emptydir/ should be listed, got:\n${listed.stdout}`
        );
    });

    it("writes a tar file the system tar can extract", needsBinaryAndTar, async () => {
        const root = makeTree();
        const outFile = path.join(root, "out.tar");
        const unpacked = path.join(root, "unpacked");
        fs.mkdirSync(unpacked);

        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile,
            format: "tar",
        });

        const extracted = systemTar(
            ["-xf", path.basename(outFile), "-C", toTarPath(unpacked)],
            path.dirname(outFile)
        );
        assert.equal(extracted.status, 0, `tar -xf failed:\n${extracted.stderr}`);

        const dist = path.join(unpacked, "dist");
        assert.ok(fs.existsSync(dist), "dist/ should have come back");
        assert.ok(
            fs.statSync(dist).isDirectory(),
            "dist/ should still be a directory, not a file named like one"
        );
        assert.equal(
            fs.readFileSync(path.join(dist, "app.js"), "utf8"),
            "let a = 1;\n",
            "the file's bytes should be unchanged by the round trip"
        );
        assert.ok(fs.existsSync(path.join(dist, "emptydir")), "emptydir/ should have come back");
    });

    it("writes a tar.gz that Node's zlib reads back to the same tar", needsBinary, async () => {
        const root = makeTree();
        const plainFile = path.join(root, "out.tar");
        const gzFile = path.join(root, "out.tar.gz");

        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile: plainFile,
            format: "tar",
        });
        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile: gzFile,
            format: "tar.gz",
        });

        // The gzip header, read by hand because that is the one thing zlib
        // does not hand back: a file that starts with these three bytes is a
        // gzip member whose method is deflate, which is the only method there
        // is.
        const magic = head(gzFile, 3);
        assert.ok(
            magic.equals(Buffer.from([0x1f, 0x8b, 0x08])),
            `out.tar.gz should start with the gzip magic, got ${magic.toString("hex")}`
        );

        // The claim that matters: compressing and decompressing a tar file is
        // not supposed to change a single byte of it. If the gzip writer were
        // wrapping something other than the plain format's output — padding it
        // differently, writing a second member — this is what would notice.
        const inflated = zlib.gunzipSync(fs.readFileSync(gzFile));
        assert.deepEqual(
            inflated,
            fs.readFileSync(plainFile),
            "tar.gz should inflate to exactly what the same request writes as tar"
        );
    });

    it("writes a tar file a system tar can list as gzip", needsBinaryAndTar, async () => {
        const root = makeTree();
        const outFile = path.join(root, "out.tar.gz");

        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile,
            format: "tar.gz",
        });

        // tar guesses the compression from the file's own bytes rather than
        // from its name, so this is a check on the gzip framing as much as on
        // the tar inside it.
        const listed = systemTar(["-tf", path.basename(outFile)], path.dirname(outFile));
        assert.equal(listed.status, 0, `tar -tf failed on a .tar.gz:\n${listed.stderr}`);
        assert.ok(
            listed.stdout.split(/\r?\n/).filter(Boolean).includes("dist/app.js"),
            `dist/app.js should be listed, got:\n${listed.stdout}`
        );
    });

    it("writes the same entry list in both tar formats", needsBinary, async () => {
        const root = makeTree();
        const plainFile = path.join(root, "plain.tar");
        const gzFile = path.join(root, "packed.tar.gz");

        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile: plainFile,
            format: "tar",
        });
        await guchho.pack({
            inputs: [path.join(root, "dist")],
            outFile: gzFile,
            format: "tar.gz",
            level: 9,
        });

        // The level is a difference in how the bytes are stored, not in what
        // is being stored: a caller who changes their mind about the level
        // gets the same archive, packed differently.
        assert.ok(
            fs.statSync(gzFile).size < fs.statSync(plainFile).size,
            "a compressed archive of a text file should be smaller than the plain one"
        );
        assert.deepEqual(
            zlib.gunzipSync(fs.readFileSync(gzFile)),
            fs.readFileSync(plainFile),
            "the level should not change what the archive holds"
        );
    });
});
