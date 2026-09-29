// The wire format, and the reader that pulls it back out of a stream.
//
// Three parts, in the order they are used: the value type, the framing, and the
// reader that reassembles framed packets from a stream that arrives in pieces.
// The framing is here rather than in the service loop because the loop is the
// only place that can decide what to do about a broken frame, and it cannot do
// that without being able to recognise one.


#include <cstring>
#include <functional>
#include <stdexcept>

#include "guchho/service.hpp"

namespace guchho::service {

    namespace {

        // The tag byte that introduces every value, one per Kind.
        //
        // The numbers are the protocol and not an implementation detail: a host
        // written in another language has to write the same byte, so changing
        // one is a protocol change and not a refactor. They start at 0 rather
        // than 1 because a zero-filled buffer and a valid null are the same
        // thing here, and a decoder that had to tell them apart would have to
        // be told which of the two a truncated frame was.
        constexpr uint8_t kTagNull = 0;
        constexpr uint8_t kTagBool = 1;
        constexpr uint8_t kTagNumber = 2;
        constexpr uint8_t kTagString = 3;
        constexpr uint8_t kTagBytes = 4;
        constexpr uint8_t kTagArray = 5;
        constexpr uint8_t kTagObject = 6;

        // How many bytes the length prefix and the id are. Both are uint32, both
        // little endian, and both are written by hand rather than through a
        // reinterpret_cast: the bytes on the wire are not the bytes in memory on
        // a big-endian machine, and a cast would make the format depend on the
        // host.
        constexpr size_t kLengthBytes = 4;
        constexpr size_t kIdBytes = 4;

        // The most a single length prefix can describe. A packet claiming to be
        // longer than this is not one that is going to arrive intact, and
        // allocating for it would be a way for a corrupt stream to take the
        // process down. It is generous next to any real build: a metafile for a
        // large project is the biggest thing that normally crosses, and that is
        // megabytes rather than gigabytes.
        constexpr uint32_t kMaxPacketSize = 512u * 1024u * 1024u;

        void WriteUint32(std::vector<uint8_t>& out, uint32_t value) {
            out.push_back(static_cast<uint8_t>(value & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        }

        uint32_t ReadUint32(const uint8_t* data) {
            return static_cast<uint32_t>(data[0])
                 | (static_cast<uint32_t>(data[1]) << 8)
                 | (static_cast<uint32_t>(data[2]) << 16)
                 | (static_cast<uint32_t>(data[3]) << 24);
        }

        // Writes a value as a count and then the contents, which is how strings,
        // blobs, arrays and objects are all framed.
        void WriteLengthPrefixed(std::vector<uint8_t>& out, const uint8_t* data, size_t length) {
            WriteUint32(out, static_cast<uint32_t>(length));
            out.insert(out.end(), data, data + length);
        }

        // Reads a value of any kind starting at *ptr, advancing it past what it
        // read. The bounds are checked against "end" rather than against a
        // claimed length, so a frame that lies about its contents is refused
        // instead of being followed into whatever memory is next.
        bool ReadValue(const uint8_t*& ptr, const uint8_t* end, Value& out) {
            if (ptr >= end) return false;

            const uint8_t tag = *ptr++;

            switch (tag) {
                case kTagNull: {
                    out = Value::Null();
                    return true;
                }
                case kTagBool: {
                    if (ptr >= end) return false;
                    out = Value::Bool(*ptr++ != 0);
                    return true;
                }
                case kTagNumber: {
                    if (end - ptr < 4) return false;
                    // Read as unsigned and then reinterpret, so that a negative
                    // int32 survives the trip through a uint32 without the
                    // conversion being implementation-defined.
                    const uint32_t raw = ReadUint32(ptr);
                    ptr += 4;
                    int32_t signed_value = 0;
                    std::memcpy(&signed_value, &raw, sizeof(signed_value));
                    out = Value::Number(signed_value);
                    return true;
                }
                case kTagString:
                case kTagBytes: {
                    if (end - ptr < 4) return false;
                    const uint32_t length = ReadUint32(ptr);
                    ptr += 4;
                    if (static_cast<uint64_t>(end - ptr) < length) return false;
                    if (tag == kTagString) {
                        out = Value::String(std::string(reinterpret_cast<const char*>(ptr), length));
                    } else {
                        out = Value::Bytes(std::vector<uint8_t>(ptr, ptr + length));
                    }
                    ptr += length;
                    return true;
                }
                case kTagArray: {
                    if (end - ptr < 4) return false;
                    const uint32_t count = ReadUint32(ptr);
                    ptr += 4;

                    // Every element is at least one byte, so a count larger than
                    // what is left cannot be satisfied. Checking here is what
                    // stops a corrupt count turning into a huge reservation.
                    if (count > static_cast<uint32_t>(end - ptr)) return false;

                    std::vector<Value> items;
                    items.reserve(count);
                    for (uint32_t i = 0; i < count; i++) {
                        Value item;
                        if (!ReadValue(ptr, end, item)) return false;
                        items.push_back(std::move(item));
                    }
                    out = Value::Array(std::move(items));
                    return true;
                }
                case kTagObject: {
                    if (end - ptr < 4) return false;
                    const uint32_t count = ReadUint32(ptr);
                    ptr += 4;

                    // Each entry is a key and a value, and a key is at least a
                    // four-byte count, so this is the loosest bound that is
                    // still a bound.
                    if (static_cast<uint64_t>(count) * 5 > static_cast<uint64_t>(end - ptr)) return false;

                    std::vector<std::pair<std::string, Value>> entries;
                    entries.reserve(count);
                    for (uint32_t i = 0; i < count; i++) {
                        if (end - ptr < 4) return false;
                        const uint32_t key_length = ReadUint32(ptr);
                        ptr += 4;
                        if (static_cast<uint64_t>(end - ptr) < key_length) return false;
                        std::string key(reinterpret_cast<const char*>(ptr), key_length);
                        ptr += key_length;

                        Value value;
                        if (!ReadValue(ptr, end, value)) return false;
                        entries.emplace_back(std::move(key), std::move(value));
                    }
                    out = Value::Object(std::move(entries));
                    return true;
                }
                default: {
                    // An unknown tag means the sender speaks a different
                    // protocol. There is no way to skip a value whose size is
                    // not known, so the frame cannot be recovered from.
                    return false;
                }
            }
        }

    } // namespace

    // -------------------------------------------------------------------------
    // Value
    // -------------------------------------------------------------------------

    Value Value::Bool(bool value) {
        Value v;
        v.kind_ = Kind::kBool;
        v.bool_ = value;
        return v;
    }

    Value Value::Number(int32_t value) {
        Value v;
        v.kind_ = Kind::kNumber;
        v.number_ = value;
        return v;
    }

    Value Value::String(std::string value) {
        Value v;
        v.kind_ = Kind::kString;
        v.string_ = std::move(value);
        return v;
    }

    Value Value::Bytes(std::vector<uint8_t> value) {
        Value v;
        v.kind_ = Kind::kBytes;
        v.bytes_ = std::move(value);
        return v;
    }

    Value Value::Array(std::vector<Value> value) {
        Value v;
        v.kind_ = Kind::kArray;
        v.array_ = std::move(value);
        return v;
    }

    Value Value::Object(std::vector<std::pair<std::string, Value>> value) {
        Value v;
        v.kind_ = Kind::kObject;
        v.object_ = std::move(value);
        return v;
    }

    Value Value::Array(std::initializer_list<Value> value) {
        return Array(std::vector<Value>(value));
    }

    Value Value::Object(std::initializer_list<std::pair<std::string, Value>> value) {
        return Object(std::vector<std::pair<std::string, Value>>(value));
    }

    namespace {

        // Every accessor below is a throw rather than a silent default. A caller
        // that asked for the wrong kind has a bug in it that a zero would hide
        // until somewhere much later, and the value crossing the wire is the
        // last cheap place to notice.
        [[noreturn]] void WrongKind(Value::Kind expected, Value::Kind got) {
            static const char* names[] = {
                "null", "a boolean", "a number", "a string",
                "bytes", "an array", "an object",
            };
            throw std::runtime_error(
                std::string("the service protocol expected ") + names[static_cast<int>(expected)]
                + " and got " + names[static_cast<int>(got)]
            );
        }

    } // namespace

    bool Value::AsBool() const {
        if (!IsBool()) WrongKind(Kind::kBool, kind_);
        return bool_;
    }

    int32_t Value::AsNumber() const {
        if (!IsNumber()) WrongKind(Kind::kNumber, kind_);
        return number_;
    }

    const std::string& Value::AsString() const {
        if (!IsString()) WrongKind(Kind::kString, kind_);
        return string_;
    }

    const std::vector<uint8_t>& Value::AsBytes() const {
        if (!IsBytes()) WrongKind(Kind::kBytes, kind_);
        return bytes_;
    }

    const std::vector<Value>& Value::AsArray() const {
        if (!IsArray()) WrongKind(Kind::kArray, kind_);
        return array_;
    }

    const std::vector<std::pair<std::string, Value>>& Value::AsObject() const {
        if (!IsObject()) WrongKind(Kind::kObject, kind_);
        return object_;
    }

    const Value* Value::Find(std::string_view key) const {
        if (!IsObject()) return nullptr;
        for (const auto& [name, value] : object_) {
            if (name == key) return &value;
        }
        return nullptr;
    }

    const Value* Value::At(size_t index) const {
        if (!IsArray() || index >= array_.size()) return nullptr;
        return &array_[index];
    }

    size_t Value::Size() const {
        if (IsArray()) return array_.size();
        if (IsObject()) return object_.size();
        return 0;
    }

    // -------------------------------------------------------------------------
    // Framing
    // -------------------------------------------------------------------------

    std::vector<uint8_t> EncodePacket(const Packet& packet) {
        std::vector<uint8_t> body;

        // The id and the direction share one word, with the direction in the
        // low bit. An even id is a request from whoever sent it, an odd one is a
        // response, so the two directions cannot be confused for one another
        // even by a reader that does not track who it is talking to.
        const uint32_t id = (packet.id << 1) | (packet.is_request ? 0u : 1u);
        WriteUint32(body, id);

        std::vector<uint8_t> payload;

        // The payload is written by hand rather than through the Value accessors
        // above, because a value of the wrong kind is a bug in the code building
        // it and should be caught while it is being built — where the stack
        // still says which command built it — not on the way out of the process.
        std::function<void(const Value&)> visit = [&](const Value& value) {
            switch (value.GetKind()) {
                case Value::Kind::kNull: {
                    payload.push_back(kTagNull);
                    break;
                }
                case Value::Kind::kBool: {
                    payload.push_back(kTagBool);
                    payload.push_back(value.AsBool() ? 1 : 0);
                    break;
                }
                case Value::Kind::kNumber: {
                    payload.push_back(kTagNumber);
                    const int32_t number = value.AsNumber();
                    uint32_t raw = 0;
                    std::memcpy(&raw, &number, sizeof(raw));
                    WriteUint32(payload, raw);
                    break;
                }
                case Value::Kind::kString: {
                    payload.push_back(kTagString);
                    const std::string& text = value.AsString();
                    WriteLengthPrefixed(payload,
                        reinterpret_cast<const uint8_t*>(text.data()), text.size());
                    break;
                }
                case Value::Kind::kBytes: {
                    payload.push_back(kTagBytes);
                    const std::vector<uint8_t>& bytes = value.AsBytes();
                    WriteLengthPrefixed(payload, bytes.data(), bytes.size());
                    break;
                }
                case Value::Kind::kArray: {
                    payload.push_back(kTagArray);
                    const std::vector<Value>& items = value.AsArray();
                    WriteUint32(payload, static_cast<uint32_t>(items.size()));
                    for (const Value& item : items) visit(item);
                    break;
                }
                case Value::Kind::kObject: {
                    payload.push_back(kTagObject);
                    const auto& entries = value.AsObject();
                    WriteUint32(payload, static_cast<uint32_t>(entries.size()));
                    for (const auto& [name, item] : entries) {
                        WriteLengthPrefixed(payload,
                            reinterpret_cast<const uint8_t*>(name.data()), name.size());
                        visit(item);
                    }
                    break;
                }
            }
        };

        visit(packet.value);

        body.insert(body.end(), payload.begin(), payload.end());

        // The length goes in front, once the body is known, and covers the body
        // only. A reader adds four to what it reads to find the next frame.
        std::vector<uint8_t> out;
        out.reserve(body.size() + kLengthBytes);
        WriteUint32(out, static_cast<uint32_t>(body.size()));
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    std::vector<uint8_t> EncodeVersionFrame(const char* version) {
        // The one frame that is not a packet.
        //
        // It is a length prefix and the version text, and nothing else, because
        // it is the first thing on the wire and it is what a host checks before
        // it has decided whether the process it started is one it can talk to.
        // A packet here would need the reader to already be willing to decode
        // something from a process it has not yet spoken to.
        std::vector<uint8_t> out;
        const size_t length = std::strlen(version);
        WriteUint32(out, static_cast<uint32_t>(length));
        out.insert(out.end(), version, version + length);
        return out;
    }

    // -------------------------------------------------------------------------
    // PacketReader
    // -------------------------------------------------------------------------

    std::vector<Packet> PacketReader::Feed(const uint8_t* data, size_t length) {
        std::vector<Packet> packets;

        if (broken_) return packets;

        buffer_.insert(buffer_.end(), data, data + length);

        while (true) {
            // Whatever was read in an earlier call and not consumed yet is still
            // at the front, so the frame is only complete when four bytes of
            // length have arrived.
            const size_t available = buffer_.size() - offset_;
            if (available < kLengthBytes) break;

            const uint32_t body_length = ReadUint32(buffer_.data() + offset_);

            if (body_length > kMaxPacketSize) {
                broken_ = true;
                error_ = "the service sent a frame claiming to be " + std::to_string(body_length)
                       + " bytes, which is more than this protocol allows";
                return packets;
            }

            if (available - kLengthBytes < body_length) break;

            const uint8_t* body = buffer_.data() + offset_ + kLengthBytes;
            const uint8_t* end = body + body_length;

            if (body_length < kIdBytes) {
                broken_ = true;
                error_ = "the service sent a frame with no room for a message id";
                return packets;
            }

            const uint32_t raw_id = ReadUint32(body);
            body += kIdBytes;

            Packet packet;
            packet.is_request = (raw_id & 1u) == 0u;
            packet.id = raw_id >> 1;

            if (!ReadValue(body, end, packet.value)) {
                broken_ = true;
                error_ = "the service sent a frame this version cannot read, so the"
                       " two are not speaking the same protocol";
                return packets;
            }

            if (body != end) {
                // Trailing bytes mean the frame is not what its length said it
                // was. Reading it anyway would let one malformed packet be
                // interpreted as a valid one plus a second packet, which is the
                // kind of thing that turns into a very confusing bug report.
                broken_ = true;
                error_ = "the service sent a frame with "
                       + std::to_string(static_cast<size_t>(end - body))
                       + " bytes more than it declared";
                return packets;
            }

            offset_ += kLengthBytes + body_length;
            packets.push_back(std::move(packet));
        }

        // Everything consumed is dropped, so the buffer holds only what is still
        // owed. This is why a long-lived reader does not grow without bound: the
        // bytes of a build are not held twice.
        if (offset_ > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<ptrdiff_t>(offset_));
            offset_ = 0;
        }

        return packets;
    }

} // namespace guchho::service
