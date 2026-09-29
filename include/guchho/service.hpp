#pragma once

// =============================================================================
// Service protocol
// =============================================================================
//
// The wire format between the npm package and this binary, and the read loop
// that serves it. It exists so that a JavaScript caller can ask for a build and
// get the answer back as data — the outputs, the diagnostics as objects rather
// than as lines of text, the metafile — instead of a log it has to scrape and a
// directory full of files it did not ask to be written.
//
// The command line is the same engine with a different reader, and neither is a
// translation of the other: both go through api::Build, and the difference is
// only what happens to the result. That is why this is here rather than in a
// reimplementation of the build: an option is accepted here or refused there for
// exactly the same reason, because the same parser decides.
//
// Two things are worth saying about the shape of it, because both are decisions
// and neither is the only way to do it.
//
// The first is that this is a binary format and not a line of JSON. The values
// that cross it are not all small: an output file is its own bytes, and a
// transform's input can be a megabyte. Sending those as JSON text means either
// base64, which is a third larger, or escaping, which is not better. A length
// prefix in front of each value is the whole solution, and it also gives the
// reader a way to know a packet ended without trusting its contents.
//
// The second is that the number type is a signed 32-bit integer and nothing
// else. There are no floats and no 64-bit values, which is a restriction rather
// than an oversight: it means a value that is not a small integer cannot be
// expressed here, and the right answer to that is for it to be a string or a
// blob rather than a lossy number. Entry points and metafiles both depend on
// this, and both are shaped around it — entry points travel as [out, in] string
// pairs rather than as objects, and the metafile travels as the JSON text the
// engine already produced rather than as a structure rebuilt here.
//
// The protocol has one more direction than a request and a response: the binary
// can send a request of its own, to ask the host to do something, and gets a
// response on the same id. The service does not use that yet — the plugin
// callbacks that would need it are not reachable from here — but the framing
// carries it, because adding a second direction later is then a change to what
// is sent rather than to how it is sent.

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "guchho/api.hpp"

namespace guchho::service {

    // The version of this protocol, which is not the version of the product.
    //
    // They are separate because they answer different questions. The product
    // version changes when the bundler changes; this changes when the wire
    // format changes, which is what decides whether a host and a binary can talk
    // to each other. A host sends this as "--service=<version>" and the binary
    // answers with the same string as its first frame, so a binary that does not
    // speak this protocol says so by returning a version that does not match
    // rather than by failing to parse something.
    inline constexpr const char* kVersion = "1";

    // A value in the tagged union that crosses the wire.
    //
    // The seven cases are the seven things the protocol can express, and the
    // list is deliberately short: anything a caller needs that is not here is a
    // case that was not anticipated, and adding one is a protocol change rather
    // than a convenience. A std::any would have been shorter to write and
    // impossible to read back, because decoding has to know what it is decoding.
    class Value {
        public:
            enum class Kind : uint8_t {
                kNull,      // nothing: an absent option, or an explicit null
                kBool,      // true or false
                kNumber,    // a signed 32-bit integer, and nothing else
                kString,    // UTF-8 text
                kBytes,     // a blob, written the same way a string is but not decoded as one
                kArray,     // a count, then that many values
                kObject,    // a count, then that many key/value pairs
            };

            Value() : kind_(Kind::kNull) {}

            static Value Null() { return Value(); }
            static Value Bool(bool value);
            static Value Number(int32_t value);
            static Value String(std::string value);
            static Value Bytes(std::vector<uint8_t> value);
            static Value Array(std::vector<Value> value);
            static Value Object(std::vector<std::pair<std::string, Value>> value);

            // The same two, for the braced form a literal reads as.
            //
            // An object here is a list of pairs with a string key, so writing one
            // as Value::Object({{"id", Value::String(id)}, ...}) is the shape of
            // the thing rather than a spelling of it, and the vector form is kept
            // for the callers that build one entry at a time.
            static Value Array(std::initializer_list<Value> value);
            static Value Object(std::initializer_list<std::pair<std::string, Value>> value);

            Kind GetKind() const { return kind_; }

            bool IsNull() const { return kind_ == Kind::kNull; }
            bool IsBool() const { return kind_ == Kind::kBool; }
            bool IsNumber() const { return kind_ == Kind::kNumber; }
            bool IsString() const { return kind_ == Kind::kString; }
            bool IsBytes() const { return kind_ == Kind::kBytes; }
            bool IsArray() const { return kind_ == Kind::kArray; }
            bool IsObject() const { return kind_ == Kind::kObject; }

            // Each of these reads its own kind and is an error otherwise, which
            // is checked on the way out of the decoder rather than here: a
            // request that asks for a string where a number was sent is a
            // malformed request, and the decoder is the one place that can say
            // so about the whole packet.
            bool AsBool() const;
            int32_t AsNumber() const;
            const std::string& AsString() const;
            const std::vector<uint8_t>& AsBytes() const;
            const std::vector<Value>& AsArray() const;
            const std::vector<std::pair<std::string, Value>>& AsObject() const;

            // The value stored under "key", or null if this is not an object or
            // has no such key. A missing key and a key holding null are the same
            // answer, which is what makes "was it given" and "was it empty" one
            // question rather than two.
            const Value* Find(std::string_view key) const;

            // The value at "index", or null if this is not an array or is
            // shorter than that.
            const Value* At(size_t index) const;

            // The count of elements in an array or an object, and zero for
            // anything else, so a caller can iterate without checking first.
            size_t Size() const;

        private:
            Kind kind_;

            bool bool_{};
            int32_t number_{};
            std::string string_;
            std::vector<uint8_t> bytes_;
            std::vector<Value> array_;

            // An object keeps its entries in the order they were built rather
            // than in a map, because a request is a document someone else wrote
            // and the order it was written in is the order it is easiest to read
            // back. Lookups are linear, which is fine: the objects here have a
            // handful of keys each and are read once.
            std::vector<std::pair<std::string, Value>> object_;
    };

    // One message on the wire.
    //
    // An id is enough to say who is talking, and the lowest bit of it says in
    // which direction. Both halves fit in one uint32 so that the id does not
    // have to be reserved anywhere: a request from the host has an even id and a
    // response to it has the same id with the bit set, and a request from the
    // binary is the mirror of that.
    struct Packet {
        uint32_t id = 0;
        bool is_request = false;
        Value value;
    };

    // Turns a packet into the bytes that go on the wire: a four-byte little
    // endian length, then the packet. The length is of the packet, not of the
    // prefix, so a reader that knows the total has to add four to what it reads
    // — which is the same arithmetic the format needs anyway to know that the
    // next packet starts here.
    std::vector<uint8_t> EncodePacket(const Packet& packet);

    // Reads packets out of a stream that arrives in pieces.
    //
    // This is a class rather than a function because a read on a pipe returns
    // whatever happened to have arrived, which is rarely a whole packet and is
    // very often half of one. Holding what is left over between calls is what
    // makes a stream of packets out of a stream of bytes: Feed() takes what
    // arrived and either has a complete packet or joins it to what it was
    // already holding. The leftovers are never read twice.
    class PacketReader {
        public:
            // Adds what arrived and hands back every complete packet in it, in
            // order. A packet that is not yet complete is held, not returned.
            std::vector<Packet> Feed(const uint8_t* data, size_t length);

            // The same, for callers holding the bytes as a string.
            std::vector<Packet> Feed(const std::string& data) {
                return Feed(reinterpret_cast<const uint8_t*>(data.data()), data.size());
            }

            // True when a frame arrived that is not a packet this build can
            // read. The service answers a version mismatch and stops; a
            // truncated or corrupt frame would otherwise be read as a value that
            // is merely wrong, which is a far worse error to be handed.
            bool IsBroken() const { return broken_; }

            // Why it is broken, for the message that says so.
            const std::string& Error() const { return error_; }

        private:
            std::vector<uint8_t> buffer_;
            size_t offset_ = 0;
            bool broken_ = false;
            std::string error_;
    };

    // A diagnostic as a value, in the same shape the JSON writer produces:
    // id, pluginName, text, location, notes, and a "detail" that is a number.
    //
    // "detail" is a number and not the payload, because the payload is a
    // std::any and the union has no case for "whatever the host had in mind". A
    // handle lets the host find its own object again, and it is the same
    // convention esbuild uses for the same reason.
    Value MessageToValue(const api::Message& message);
    std::vector<Value> MessagesToValue(const std::vector<api::Message>& messages);

    // An output file as a value. The contents are bytes, which is why this is
    // worth a case in the union at all: an output may be a PNG, and sending it
    // as a string would be a claim about it that is not true.
    Value OutputFileToValue(const api::OutputFile& file);

    // A location as a value, or null when there is none. The field is always
    // present in a message and is sometimes null, so a host can read it without
    // first asking whether the key is there.
    Value LocationToValue(const api::Location& location);

    // The other direction: a message the host built out of the same fields, which
    // is what "format-msgs" is for. A value that is not shaped like a message is
    // nullopt rather than a partly-filled Message, because a diagnostic with
    // missing text is a sentence with no words in it.
    std::optional<api::Message> MessageFromValue(const Value& value);

    // Turns one message into the bytes of a JSON document.
    //
    // This is not a general JSON writer and is not trying to be one: it writes
    // the shapes this protocol actually sends, which are the diagnostic, the
    // output file list and the metafile. A number that is not representable is a
    // refusal rather than a null, because a caller reading a metafile and
    // finding a null where a byte count was means something is wrong that no
    // message would otherwise describe.
    std::string MessagesToJSON(const std::vector<api::Message>& messages);

    // A message as JSON on its own, which is what the "error" command reports
    // when the host has already formatted one and wants it shown the way the
    // engine would have shown it.
    std::string MessageToJSON(const api::Message& message);

    // The output files as a JSON array. Contents are base64, because JSON has no
    // way to hold a byte that is not text and base64 is the only encoding
    // available that does not care what the bytes are.
    std::string OutputFilesToJSON(const std::vector<api::OutputFile>& files);

    // Reads a message back out of a JSON document, which is how a diagnostic
    // that the host formatted itself is reported to the engine so that the
    // engine can render it with the run's own log settings.
    std::optional<api::Message> MessageFromJSON(const std::string& json);

    // A whole document to a value, and a value to a document. Exposed because
    // the request side needs the same machinery and neither direction is
    // particular to any one command.
    std::optional<Value> JSONToValue(const std::string& json);
    std::string ValueToJSON(const Value& value);

    // Runs the service: reads packets from standard input until it ends or a
    // host asks it to stop, and writes every answer to standard output.
    //
    // The return value is what the process should exit with, which is 0 for a
    // service that ran until its input ended and 1 for one that did not
    // understand what it was sent.
    int RunService();

    // The version handshake, exposed for a test that wants to check that a
    // reader and a writer agree without starting a process.
    std::vector<uint8_t> EncodeVersionFrame(const char* version);

} // namespace guchho::service
