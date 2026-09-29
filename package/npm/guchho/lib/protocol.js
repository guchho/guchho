"use strict";

// The wire format, in the other language.
//
// Everything here is a translation of src/service/protocol.cpp and nothing else.
// The two have to agree byte for byte, so when one changes the other changes
// with it: a tag is one number in a table in each file, a length prefix is
// four little-endian bytes in both, and the version string is a frame rather
// than a packet in both.
//
// The shape of it:
//
//   frame   := length:u32 body
//   packet  := id:u32 value
//   value   := tag payload
//
// "id" carries the direction in its lowest bit — a request is even, a response
// is that same id with the bit set — so the two can never be confused for one
// another, and the host does not have to keep a table of who it is talking to.
// "length" is the body alone, not the body plus itself, so a reader adds four to
// find the next frame.
//
// A string is UTF-8 and a blob is not decoded at all. That distinction is the
// reason both exist: an output file is a PNG, and calling it a string would be
// a claim about its contents that is not true.

const TAG_NULL = 0;
const TAG_BOOL = 1;
const TAG_NUMBER = 2;
const TAG_STRING = 3;
const TAG_BYTES = 4;
const TAG_ARRAY = 5;
const TAG_OBJECT = 6;

// How long the version string and the frame lengths are allowed to be.
//
// Both are a number in a frame, so both are attacker- or accident-controlled
// until they are checked. The packet limit is well above any plausible request —
// a build's metafile is the biggest thing that crosses, and it is megabytes, not
// gigabytes — and the point of having it is that a corrupt length cannot make
// this process reserve a gigabyte before noticing the frame is nonsense.
const MAX_PACKET = 512 * 1024 * 1024;
const MAX_VERSION = 64;

/** A value that cannot be sent, named rather than thrown as a bare string. */
class EncodeError extends Error {
  constructor(message) {
    super(message);
    this.name = "EncodeError";
  }
}

/** A frame this build cannot read. */
class DecodeError extends Error {
  constructor(message) {
    super(message);
    this.name = "DecodeError";
  }
}

// A buffer that grows as it is written to.
//
// The obvious alternatives are both worse here. An array of small buffers means
// a join at the end that copies everything, and the result of that join is what
// gets written. Allocating a worst-case buffer up front means guessing a size,
// and a request's size is not knowable without walking it first. Growing by
// doubling is one copy per doubling — about twenty for a megabyte, and each copy
// of the part that is still there, which is most of it.
class ByteWriter {
  constructor() {
    this.buf = Buffer.allocUnsafe(256);
    this.length = 0;
  }

  _room(bytes) {
    const needed = this.length + bytes;
    if (needed <= this.buf.length) return;
    let size = this.buf.length;
    while (size < needed) size *= 2;
    const grown = Buffer.allocUnsafe(size);
    this.buf.copy(grown, 0, 0, this.length);
    this.buf = grown;
  }

  byte(value) {
    this._room(1);
    this.buf[this.length++] = value;
  }

  u32(value) {
    this._room(4);
    this.buf.writeUInt32LE(value, this.length);
    this.length += 4;
  }

  i32(value) {
    this._room(4);
    this.buf.writeInt32LE(value, this.length);
    this.length += 4;
  }

  bytes(value) {
    this._room(value.length);
    value.copy(this.buf, this.length);
    this.length += value.length;
  }

  take() {
    return this.buf.subarray(0, this.length);
  }
}

// The written form of a value, appended to "out".
//
// Appended rather than returned so that a whole packet is built in one buffer
// and written once. A transform's code is a megabyte, and building that as a
// tree of intermediate arrays and joining at the end would copy it once per
// nesting level for no reason.
function writeValue(value, out) {
  if (value === null || value === undefined) {
    out.byte(TAG_NULL);
    return;
  }

  switch (typeof value) {
    case "boolean":
      out.byte(TAG_BOOL);
      out.byte(value ? 1 : 0);
      return;

    case "number":
      if (!Number.isInteger(value)) {
        // Not rounded and not refused silently. The protocol carries a signed
        // 32-bit integer and nothing else, so a 1.5 has no representation here;
        // turning it into 2 would produce a build of something the caller never
        // asked for and report success.
        throw new EncodeError(
          `the service protocol carries whole numbers only, and ${value} is not one. ` +
            `A value like this should be sent as a string or a buffer.`
        );
      }
      if (value < -2147483648 || value > 2147483647) {
        throw new EncodeError(
          `the service protocol carries 32-bit numbers, and ${value} does not fit in one.`
        );
      }
      out.byte(TAG_NUMBER);
      out.i32(value);
      return;

    case "string": {
      const bytes = Buffer.from(value, "utf8");
      out.byte(TAG_STRING);
      out.u32(bytes.length);
      out.bytes(bytes);
      return;
    }

    case "object":
      break;

    default:
      throw new EncodeError(`a ${typeof value} cannot be sent over the service protocol`);
  }

  if (Buffer.isBuffer(value) || value instanceof Uint8Array) {
    // A typed array is copied rather than referenced, because a view over
    // something the caller is free to mutate would be a request that changes
    // underneath the write that is already in flight.
    const bytes = Buffer.isBuffer(value) ? value : Buffer.from(value.buffer, value.byteOffset, value.byteLength);
    out.byte(TAG_BYTES);
    out.u32(bytes.length);
    out.bytes(bytes);
    return;
  }

  if (Array.isArray(value)) {
    out.byte(TAG_ARRAY);
    out.u32(value.length);
    for (const item of value) writeValue(item, out);
    return;
  }

  // A plain object. Its keys are sent in the order they were written, because a
  // request is a document somebody else wrote and reading it back in the order
  // it was written in is the order it is easiest to read in.
  const keys = Object.keys(value);
  out.byte(TAG_OBJECT);
  out.u32(keys.length);
  for (const key of keys) {
    const name = Buffer.from(key, "utf8");
    out.u32(name.length);
    out.bytes(name);
    writeValue(value[key], out);
  }
}

/** One packet, ready to write: the length, the id, and the value. */
function encodePacket(id, isRequest, value) {
  const body = new ByteWriter();
  body.u32(((id << 1) | (isRequest ? 0 : 1)) >>> 0);
  writeValue(value, body);

  const payload = body.take();
  const out = Buffer.allocUnsafe(4 + payload.length);
  out.writeUInt32LE(payload.length, 0);
  payload.copy(out, 4);
  return out;
}

/** The greeting: a length and the version text, and not a packet. */
function encodeVersionFrame(version) {
  const body = Buffer.from(version, "utf8");
  const out = Buffer.allocUnsafe(4 + body.length);
  out.writeUInt32LE(body.length, 0);
  body.copy(out, 4);
  return out;
}

// The read form, as [value, offset]. An offset rather than a cursor object
// because this is called once per value in a frame and an object per value would
// be an allocation per value in a build's metafile.
function readValue(buf, offset) {
  if (offset >= buf.length) {
    throw new DecodeError("a value ended where there was nothing left to read");
  }

  const tag = buf[offset];
  offset += 1;

  switch (tag) {
    case TAG_NULL:
      return [null, offset];

    case TAG_BOOL:
      if (offset >= buf.length) throw new DecodeError("a boolean had no value");
      return [buf[offset] !== 0, offset + 1];

    case TAG_NUMBER: {
      if (buf.length - offset < 4) throw new DecodeError("a number was cut short");
      return [buf.readInt32LE(offset), offset + 4];
    }

    case TAG_STRING: {
      if (buf.length - offset < 4) throw new DecodeError("a string had no length");
      const length = buf.readUInt32LE(offset);
      offset += 4;
      if (buf.length - offset < length) throw new DecodeError("a string was cut short");
      return [buf.toString("utf8", offset, offset + length), offset + length];
    }

    case TAG_BYTES: {
      if (buf.length - offset < 4) throw new DecodeError("a buffer had no length");
      const length = buf.readUInt32LE(offset);
      offset += 4;
      if (buf.length - offset < length) throw new DecodeError("a buffer was cut short");
      // A view over the frame rather than a copy. The frame is ours and is not
      // reused, and a build's output is read once by the caller, so copying
      // here would double the peak memory of exactly the largest thing this
      // process holds.
      const bytes = new Uint8Array(buf.buffer, buf.byteOffset + offset, length);
      return [bytes, offset + length];
    }

    case TAG_ARRAY: {
      if (buf.length - offset < 4) throw new DecodeError("an array had no count");
      const count = buf.readUInt32LE(offset);
      offset += 4;
      // Every element is at least one byte, so a count larger than what is left
      // cannot be satisfied. Checked before the loop so a corrupt count does not
      // become a huge array.
      if (count > buf.length - offset) throw new DecodeError("an array claimed more items than the frame holds");
      const items = new Array(count);
      for (let i = 0; i < count; i++) {
        const [item, next] = readValue(buf, offset);
        items[i] = item;
        offset = next;
      }
      return [items, offset];
    }

    case TAG_OBJECT: {
      if (buf.length - offset < 4) throw new DecodeError("an object had no count");
      const count = buf.readUInt32LE(offset);
      offset += 4;
      if (count > buf.length - offset) throw new DecodeError("an object claimed more entries than the frame holds");
      const entries = {};
      for (let i = 0; i < count; i++) {
        if (buf.length - offset < 4) throw new DecodeError("a key had no length");
        const keyLength = buf.readUInt32LE(offset);
        offset += 4;
        if (buf.length - offset < keyLength) throw new DecodeError("a key was cut short");
        const key = buf.toString("utf8", offset, offset + keyLength);
        offset += keyLength;
        const [item, next] = readValue(buf, offset);
        entries[key] = item;
        offset = next;
      }
      return [entries, offset];
    }

    default:
      throw new DecodeError(
        `the service sent a value this version does not have a case for (tag ${tag}), ` +
          `so the two are not speaking the same protocol`
      );
  }
}

/**
 * A stream of frames, out of a stream of bytes.
 *
 * A class rather than a function for the same reason it is one in C++: a read on
 * a pipe returns whatever happened to have arrived, which is rarely a whole
 * frame and is very often half of one. What is left over between calls is what
 * makes a stream of frames out of a stream of bytes.
 */
class FrameReader {
  constructor() {
    this.pending = [];
    this.pendingLength = 0;
  }

  /**
   * Adds what arrived and hands back every complete frame in it, in order.
   * @param {Buffer} chunk
   * @returns {Buffer[]} the bodies of the frames that are now complete
   */
  feed(chunk) {
    if (this.pendingLength === 0) {
      this.pending = [chunk];
      this.pendingLength = chunk.length;
    } else {
      this.pending.push(chunk);
      this.pendingLength += chunk.length;
    }

    const frames = [];
    let consumed = 0;

    for (;;) {
      const buffered = this.pendingLength - consumed;
      if (buffered < 4) break;

      const head = this._at(consumed, 4);
      const bodyLength = head.readUInt32LE(0);

      if (bodyLength > MAX_PACKET) {
        throw new DecodeError(
          `the service sent a frame claiming to be ${bodyLength} bytes, which is more than this protocol allows`
        );
      }
      if (buffered - 4 < bodyLength) break;

      frames.push(this._at(consumed + 4, bodyLength));
      consumed += 4 + bodyLength;
    }

    if (consumed > 0) {
      // Everything consumed is dropped, so what is held is only what is still
      // owed. Without this a long-lived reader grows for as long as the process
      // runs, holding a copy of every byte that ever arrived.
      this.pending = [this._at(consumed, this.pendingLength - consumed)];
      this.pendingLength -= consumed;
    }

    return frames;
  }

  // "offset" and "length" bytes into the held bytes, as one buffer. The result
  // is a copy, which is why the frames handed to a caller are copies: they
  // outlive the buffer they came from, because a caller holds an output file
  // while the next frame is being read.
  _at(offset, length) {
    if (length === 0) return Buffer.alloc(0);
    if (this.pending.length === 1) {
      return this.pending[0].subarray(offset, offset + length);
    }
    return Buffer.concat(this.pending).subarray(offset, offset + length);
  }
}

module.exports = {
  TAG_NULL,
  TAG_BOOL,
  TAG_NUMBER,
  TAG_STRING,
  TAG_BYTES,
  TAG_ARRAY,
  TAG_OBJECT,
  MAX_PACKET,
  MAX_VERSION,
  EncodeError,
  DecodeError,
  writeValue,
  encodePacket,
  encodeVersionFrame,
  readValue,
  FrameReader,
};
