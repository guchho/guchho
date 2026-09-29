// JSON, for the few things that cross the protocol as text rather than as
// values.
//
// The protocol itself does not use JSON — it uses the tagged union in
// protocol.cpp — and this is not a general-purpose JSON library. It writes and
// reads four shapes: a diagnostic, a list of output files, a metafile, and a
// value. Those are the shapes that a host has to see as a document rather than
// as a value, either because it will parse them itself or because it wrote them.
//
// The asymmetry is the point. Output files travel as base64 inside a value,
// because their bytes are large and have no text form. The metafile travels as
// the JSON text the engine already produced, because the engine produced exactly
// that and parsing it back into a value here would be a second implementation of
// a document format that is already written — one that would then have to be
// kept in step with the writer in bundler_compile.cpp. The same is true of a
// diagnostic: an editor integration wants the location and the notes as fields,
// so those are written out as JSON, and "detail" stays an int because it is
// whatever the host stashed and only the host can interpret it.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "guchho/helpers.hpp"
#include "guchho/service.hpp"

namespace guchho::service {

    // -------------------------------------------------------------------------
    // The same shapes, as values rather than as JSON
    // -------------------------------------------------------------------------
    //
    // Both directions are needed and they are not the same thing. A value is
    // what travels inside a packet, so this is the path every diagnostic and
    // every output file takes on its way to a host. The JSON below is for the
    // one case where text is wanted instead: a host that has formatted a message
    // itself and wants the engine to render it, and nothing else.
    //
    // Keeping both in one file is deliberate. They are the same three shapes
    // described twice, and describing them in two files would be two places for
    // a field to be added to one of them and not the other — which is the same
    // reason the JSON writer calls the engine's own quoter rather than having
    // one of its own.

    namespace {

        // "detail" is an int32 handle and nothing more.
        constexpr int32_t kNoDetail = -1;

        std::vector<Value> NotesToValue(const std::vector<api::Note>& notes) {
            std::vector<Value> out;
            out.reserve(notes.size());
            for (const api::Note& note : notes) {
                out.push_back(Value::Object({
                    {"text", Value::String(note.text)},
                    {"location", note.location.has_value()
                        ? LocationToValue(*note.location)
                        : Value::Null()},
                }));
            }
            return out;
        }

    } // namespace

    Value LocationToValue(const api::Location& location) {
        return Value::Object({
            {"file", Value::String(location.file)},
            {"namespace", Value::String(location.namespace_)},
            {"line", Value::Number(location.line)},
            {"column", Value::Number(location.column)},
            {"length", Value::Number(location.length)},
            {"lineText", Value::String(location.line_text)},
            {"suggestion", Value::String(location.suggestion)},
        });
    }

    Value MessageToValue(const api::Message& message) {
        return Value::Object({
            {"id", Value::String(message.id)},
            {"pluginName", Value::String(message.plugin_name)},
            {"text", Value::String(message.text)},
            // Always present and sometimes null, rather than absent: a host
            // reading "location" on every message should not have to ask whether
            // the key is there.
            {"location", message.location.has_value()
                ? LocationToValue(*message.location)
                : Value::Null()},
            {"notes", Value::Array(NotesToValue(message.notes))},
            {"detail", Value::Number(kNoDetail)},
        });
    }

    std::vector<Value> MessagesToValue(const std::vector<api::Message>& messages) {
        std::vector<Value> out;
        out.reserve(messages.size());
        for (const api::Message& message : messages) {
            out.push_back(MessageToValue(message));
        }
        return out;
    }

    Value OutputFileToValue(const api::OutputFile& file) {
        return Value::Object({
            {"path", Value::String(file.path)},
            {"contents", Value::Bytes(file.contents)},
            {"hash", Value::String(file.hash)},
        });
    }

    namespace {

        // Quotes a string as a JSON string literal.
        //
        // The engine's own quoter is used rather than a new one, so that a
        // control character in a path is escaped the same way here as it is in
        // the metafile and in a diagnostic printed to a terminal. Two escapers
        // would eventually disagree, and the disagreement would show up as a
        // file name that survives one and not the other.
        std::string Quoted(std::string_view text) {
            return helpers::QuoteForJSON(text, false);
        }

        // Writes a number, or nothing at all when the value is outside the range
        // a JSON number can hold exactly.
        //
        // A byte count off a 64-bit build is the case that matters here: the
        // metafile reports one, and a silently wrong or rounded figure is worse
        // than an absent one because a caller that sums them gets a plausible
        // total. Everything this protocol sends is signed 32-bit, so this only
        // ever fires for a value that came from somewhere else.
        void AppendNumber(std::string& out, int64_t value) {
            if (value < INT64_MIN || value > INT64_MAX) return;
            char buffer[32];
            const int written = std::snprintf(buffer, sizeof(buffer), "%lld",
                static_cast<long long>(value));
            if (written > 0) out.append(buffer, static_cast<size_t>(written));
        }

        // The location as a JSON object, or null when there is none.
        //
        // Null rather than an absent key, because the field is always part of
        // the shape: a host that reads "location" on every message should not
        // have to ask whether the key is there. "line" counts from 1 and
        // "column" and "length" are byte counts, which is the same convention
        // the C++ Location uses and the one an editor expects.
        std::string LocationToJSON(const api::Location& location) {
            std::string out = "{";
            out += "\"file\":" + Quoted(location.file);
            out += ",\"namespace\":" + Quoted(location.namespace_);
            out += ",\"line\":";
            AppendNumber(out, location.line);
            out += ",\"column\":";
            AppendNumber(out, location.column);
            out += ",\"length\":";
            AppendNumber(out, location.length);
            out += ",\"lineText\":" + Quoted(location.line_text);
            out += ",\"suggestion\":" + Quoted(location.suggestion);
            out += "}";
            return out;
        }

        // "detail" is the same int32 handle the value form uses, and the comment
        // explaining it is up there with the value writer.

        std::string MessageToJSONImpl(const api::Message& message) {
            std::string out = "{";
            out += "\"id\":" + Quoted(message.id);
            out += ",\"pluginName\":" + Quoted(message.plugin_name);
            out += ",\"text\":" + Quoted(message.text);

            out += ",\"location\":";
            if (message.location.has_value()) {
                out += LocationToJSON(*message.location);
            } else {
                out += "null";
            }

            out += ",\"notes\":[";
            for (size_t i = 0; i < message.notes.size(); i++) {
                if (i > 0) out += ",";
                const api::Note& note = message.notes[i];
                out += "{\"text\":" + Quoted(note.text);
                out += ",\"location\":";
                if (note.location.has_value()) {
                    out += LocationToJSON(*note.location);
                } else {
                    out += "null";
                }
                out += "}";
            }
            out += "]";

            out += ",\"detail\":" + std::to_string(kNoDetail);
            out += "}";
            return out;
        }

        // A minimal reader, for the one direction that needs one.
        //
        // Only the "error" command reads JSON back, and the document it reads is
        // a message a host formatted itself — four fields it just wrote. So this
        // is a small recursive-descent parser over the subset that covers that,
        // and it says so when it meets something else. Pulling in a JSON
        // library for it would be a dependency the protocol otherwise does not
        // have, which is why this exists rather than that.
        class JSONReader {
            public:
                explicit JSONReader(std::string_view text) : text_(text) {}

                // Parses one document and reports whether the whole of it was
                // consumed. Trailing text is a failure rather than a shrug,
                // because a document with a tail means the two sides do not
                // agree on where it ends.
                bool Parse(Value& out) {
                    SkipSpace();
                    if (!ParseValue(out)) return false;
                    SkipSpace();
                    return offset_ == text_.size();
                }

            private:
                std::string_view text_;
                size_t offset_ = 0;

                void SkipSpace() {
                    while (offset_ < text_.size()) {
                        const char c = text_[offset_];
                        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
                        offset_++;
                    }
                }

                bool AtEnd() const { return offset_ >= text_.size(); }
                char Peek() const { return text_[offset_]; }

                bool Expect(char c) {
                    if (AtEnd() || Peek() != c) return false;
                    offset_++;
                    return true;
                }

                bool ParseString(std::string& out) {
                    if (!Expect('"')) return false;
                    std::string result;

                    while (true) {
                        if (AtEnd()) return false;
                        const char c = text_[offset_++];

                        if (c == '"') break;

                        if (c != '\\') {
                            result.push_back(c);
                            continue;
                        }

                        if (AtEnd()) return false;
                        const char escape = text_[offset_++];
                        switch (escape) {
                            case '"': result.push_back('"'); break;
                            case '\\': result.push_back('\\'); break;
                            case '/': result.push_back('/'); break;
                            case 'b': result.push_back('\b'); break;
                            case 'f': result.push_back('\f'); break;
                            case 'n': result.push_back('\n'); break;
                            case 'r': result.push_back('\r'); break;
                            case 't': result.push_back('\t'); break;
                            case 'u': {
                                // A \u escape, which is four hex digits. Only the
                                // characters that need it are written by the
                                // quoter, and they are all below 0x80 once
                                // encoded, so this handles the BMP and leaves
                                // anything else as the replacement character
                                // rather than as a wrong answer.
                                if (offset_ + 4 > text_.size()) return false;
                                unsigned code = 0;
                                for (int i = 0; i < 4; i++) {
                                    const char digit = text_[offset_++];
                                    code <<= 4;
                                    if (digit >= '0' && digit <= '9') code |= static_cast<unsigned>(digit - '0');
                                    else if (digit >= 'a' && digit <= 'f') code |= static_cast<unsigned>(digit - 'a' + 10);
                                    else if (digit >= 'A' && digit <= 'F') code |= static_cast<unsigned>(digit - 'A' + 10);
                                    else return false;
                                }
                                if (code < 0x80) {
                                    result.push_back(static_cast<char>(code));
                                } else if (code < 0x800) {
                                    result.push_back(static_cast<char>(0xC0 | (code >> 6)));
                                    result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                                } else {
                                    result.push_back(static_cast<char>(0xE0 | (code >> 12)));
                                    result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                                    result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                                }
                                break;
                            }
                            default: return false;
                        }
                    }

                    out = std::move(result);
                    return true;
                }

                bool ParseValue(Value& out) {
                    if (AtEnd()) return false;

                    switch (Peek()) {
                        case 'n': {
                            if (text_.compare(offset_, 4, "null") != 0) return false;
                            offset_ += 4;
                            out = Value::Null();
                            return true;
                        }
                        case 't': {
                            if (text_.compare(offset_, 4, "true") != 0) return false;
                            offset_ += 4;
                            out = Value::Bool(true);
                            return true;
                        }
                        case 'f': {
                            if (text_.compare(offset_, 5, "false") != 0) return false;
                            offset_ += 5;
                            out = Value::Bool(false);
                            return true;
                        }
                        case '"': {
                            std::string text;
                            if (!ParseString(text)) return false;
                            out = Value::String(std::move(text));
                            return true;
                        }
                        case '[': return ParseArray(out);
                        case '{': return ParseObject(out);
                        default: return ParseNumber(out);
                    }
                }

                bool ParseNumber(Value& out) {
                    const size_t start = offset_;
                    if (!AtEnd() && (Peek() == '-' || Peek() == '+')) offset_++;

                    bool any_digits = false;
                    while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
                        offset_++;
                        any_digits = true;
                    }
                    if (!any_digits) return false;

                    // A fractional or exponent part is read and refused. Nothing
                    // this protocol carries is a real number, so a document with
                    // one in it is a document from somewhere that does not match,
                    // and accepting it by truncating would be a lie about the
                    // value.
                    if (!AtEnd() && (Peek() == '.' || Peek() == 'e' || Peek() == 'E')) {
                        return false;
                    }

                    const std::string_view digits = text_.substr(start, offset_ - start);
                    int32_t number = 0;
                    for (const char c : digits) {
                        if (c == '-' || c == '+') continue;
                        const int32_t digit = static_cast<int32_t>(c - '0');
                        if (number > (INT32_MAX - digit) / 10) return false;
                        number = number * 10 + digit;
                    }
                    if (!digits.empty() && digits.front() == '-') number = -number;

                    out = Value::Number(number);
                    return true;
                }

                bool ParseArray(Value& out) {
                    if (!Expect('[')) return false;
                    SkipSpace();

                    std::vector<Value> items;
                    if (!AtEnd() && Peek() == ']') {
                        offset_++;
                        out = Value::Array(std::move(items));
                        return true;
                    }

                    while (true) {
                        SkipSpace();
                        Value item;
                        if (!ParseValue(item)) return false;
                        items.push_back(std::move(item));
                        SkipSpace();
                        if (Expect(',')) continue;
                        if (Expect(']')) break;
                        return false;
                    }

                    out = Value::Array(std::move(items));
                    return true;
                }

                bool ParseObject(Value& out) {
                    if (!Expect('{')) return false;
                    SkipSpace();

                    std::vector<std::pair<std::string, Value>> entries;
                    if (!AtEnd() && Peek() == '}') {
                        offset_++;
                        out = Value::Object(std::move(entries));
                        return true;
                    }

                    while (true) {
                        SkipSpace();
                        std::string key;
                        if (!ParseString(key)) return false;
                        SkipSpace();
                        if (!Expect(':')) return false;
                        SkipSpace();

                        Value value;
                        if (!ParseValue(value)) return false;
                        entries.emplace_back(std::move(key), std::move(value));

                        SkipSpace();
                        if (Expect(',')) continue;
                        if (Expect('}')) break;
                        return false;
                    }

                    out = Value::Object(std::move(entries));
                    return true;
                }
        };

    } // namespace

    // -------------------------------------------------------------------------
    // Writing
    // -------------------------------------------------------------------------

    std::string MessageToJSON(const api::Message& message) {
        return MessageToJSONImpl(message);
    }

    std::string MessagesToJSON(const std::vector<api::Message>& messages) {
        std::string out = "[";
        for (size_t i = 0; i < messages.size(); i++) {
            if (i > 0) out += ",";
            out += MessageToJSONImpl(messages[i]);
        }
        out += "]";
        return out;
    }

    std::string OutputFilesToJSON(const std::vector<api::OutputFile>& files) {
        std::string out = "[";

        for (size_t i = 0; i < files.size(); i++) {
            if (i > 0) out += ",";

            const api::OutputFile& file = files[i];
            out += "{\"path\":" + Quoted(file.path);

            // base64 rather than an escaped string, because these are bytes and
            // the whole point of OutputFile holding a vector rather than a string
            // is that an output may be a PNG. Escaping would work for text and
            // would be a silent corruption for everything else.
            const std::string encoded = helpers::Base64StdEncode(std::string_view(
                reinterpret_cast<const char*>(file.contents.data()), file.contents.size()));
            out += ",\"contents\":" + Quoted(encoded);

            out += ",\"hash\":" + Quoted(file.hash);
            out += "}";
        }

        out += "]";
        return out;
    }

    std::string ValueToJSON(const Value& value) {
        switch (value.GetKind()) {
            case Value::Kind::kNull: return "null";
            case Value::Kind::kBool: return value.AsBool() ? "true" : "false";
            case Value::Kind::kNumber: return std::to_string(value.AsNumber());
            case Value::Kind::kString: return Quoted(value.AsString());
            case Value::Kind::kBytes: {
                const std::vector<uint8_t>& bytes = value.AsBytes();
                return Quoted(helpers::Base64StdEncode(std::string_view(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size())));
            }
            case Value::Kind::kArray: {
                std::string out = "[";
                const std::vector<Value>& items = value.AsArray();
                for (size_t i = 0; i < items.size(); i++) {
                    if (i > 0) out += ",";
                    out += ValueToJSON(items[i]);
                }
                out += "]";
                return out;
            }
            case Value::Kind::kObject: {
                std::string out = "{";
                const auto& entries = value.AsObject();
                for (size_t i = 0; i < entries.size(); i++) {
                    if (i > 0) out += ",";
                    out += Quoted(entries[i].first);
                    out += ":";
                    out += ValueToJSON(entries[i].second);
                }
                out += "}";
                return out;
            }
        }
        return "null";
    }

    // -------------------------------------------------------------------------
    // Reading
    // -------------------------------------------------------------------------

    std::optional<Value> JSONToValue(const std::string& json) {
        JSONReader reader(json);
        Value value;
        if (!reader.Parse(value)) return std::nullopt;
        return value;
    }

    // A helper for the one place that needs it: a message arriving as a value
    // rather than as text, which is the "error" command — a host that
    // formatted a diagnostic itself and is handing it to the engine to render.
    //
    // It reads the same four fields the writer above produces, and refuses
    // anything else rather than skipping it, because a message with a field the
    // reader does not understand is a message from a different version and
    // rendering half of it would say something the host never wrote.
    std::optional<api::Message> MessageFromValue(const Value& document) {
        if (!document.IsObject()) return std::nullopt;

        api::Message message;

        if (const Value* text = document.Find("text")) {
            if (!text->IsString()) return std::nullopt;
            message.text = text->AsString();
        }
        if (const Value* id = document.Find("id")) {
            if (!id->IsString()) return std::nullopt;
            message.id = id->AsString();
        }
        if (const Value* plugin = document.Find("pluginName")) {
            if (!plugin->IsString()) return std::nullopt;
            message.plugin_name = plugin->AsString();
        }

        if (const Value* location = document.Find("location")) {
            if (location->IsNull()) {
                // Explicitly null, which is the same as saying nothing.
            } else if (location->IsObject()) {
                api::Location read;
                if (const Value* file = location->Find("file")) {
                    if (file->IsString()) read.file = file->AsString();
                }
                if (const Value* ns = location->Find("namespace")) {
                    if (ns->IsString()) read.namespace_ = ns->AsString();
                }
                if (const Value* line = location->Find("line")) {
                    if (line->IsNumber()) read.line = line->AsNumber();
                }
                if (const Value* column = location->Find("column")) {
                    if (column->IsNumber()) read.column = column->AsNumber();
                }
                if (const Value* length = location->Find("length")) {
                    if (length->IsNumber()) read.length = length->AsNumber();
                }
                if (const Value* line_text = location->Find("lineText")) {
                    if (line_text->IsString()) read.line_text = line_text->AsString();
                }
                if (const Value* suggestion = location->Find("suggestion")) {
                    if (suggestion->IsString()) read.suggestion = suggestion->AsString();
                }
                message.location = std::move(read);
            } else {
                return std::nullopt;
            }
        }

        if (const Value* notes = document.Find("notes")) {
            if (notes->IsArray()) {
                for (const Value& entry : notes->AsArray()) {
                    if (!entry.IsObject()) return std::nullopt;
                    api::Note note;
                    if (const Value* text = entry.Find("text")) {
                        if (text->IsString()) note.text = text->AsString();
                    }
                    if (const Value* location = entry.Find("location")) {
                        if (location->IsObject()) {
                            api::Location read;
                            if (const Value* file = location->Find("file")) {
                                if (file->IsString()) read.file = file->AsString();
                            }
                            if (const Value* ns = location->Find("namespace")) {
                                if (ns->IsString()) read.namespace_ = ns->AsString();
                            }
                            if (const Value* line = location->Find("line")) {
                                if (line->IsNumber()) read.line = line->AsNumber();
                            }
                            if (const Value* column = location->Find("column")) {
                                if (column->IsNumber()) read.column = column->AsNumber();
                            }
                            if (const Value* length = location->Find("length")) {
                                if (length->IsNumber()) read.length = length->AsNumber();
                            }
                            if (const Value* line_text = location->Find("lineText")) {
                                if (line_text->IsString()) read.line_text = line_text->AsString();
                            }
                            if (const Value* suggestion = location->Find("suggestion")) {
                                if (suggestion->IsString()) read.suggestion = suggestion->AsString();
                            }
                            note.location = std::move(read);
                        }
                    }
                    message.notes.push_back(std::move(note));
                }
            }
        }

        // "detail" is not read. A handle is only meaningful to the host that
        // issued it, and this side has nothing to look it up in.
        return message;
    }

    std::optional<api::Message> MessageFromJSON(const std::string& json) {
        const std::optional<Value> document = JSONToValue(json);
        if (!document.has_value() || !document->IsObject()) return std::nullopt;

        api::Message message;

        if (const Value* text = document->Find("text")) {
            if (!text->IsString()) return std::nullopt;
            message.text = text->AsString();
        }
        if (const Value* id = document->Find("id")) {
            if (id->IsString()) message.id = id->AsString();
        }
        if (const Value* plugin = document->Find("pluginName")) {
            if (plugin->IsString()) message.plugin_name = plugin->AsString();
        }

        if (const Value* location = document->Find("location")) {
            if (location->IsObject()) {
                api::Location read;
                if (const Value* file = location->Find("file")) {
                    if (file->IsString()) read.file = file->AsString();
                }
                if (const Value* ns = location->Find("namespace")) {
                    if (ns->IsString()) read.namespace_ = ns->AsString();
                }
                if (const Value* line = location->Find("line")) {
                    if (line->IsNumber()) read.line = line->AsNumber();
                }
                if (const Value* column = location->Find("column")) {
                    if (column->IsNumber()) read.column = column->AsNumber();
                }
                if (const Value* length = location->Find("length")) {
                    if (length->IsNumber()) read.length = length->AsNumber();
                }
                if (const Value* line_text = location->Find("lineText")) {
                    if (line_text->IsString()) read.line_text = line_text->AsString();
                }
                if (const Value* suggestion = location->Find("suggestion")) {
                    if (suggestion->IsString()) read.suggestion = suggestion->AsString();
                }
                message.location = std::move(read);
            }
        }

        if (const Value* notes = document->Find("notes")) {
            if (notes->IsArray()) {
                for (const Value& entry : notes->AsArray()) {
                    if (!entry.IsObject()) continue;
                    api::Note note;
                    if (const Value* text = entry.Find("text")) {
                        if (text->IsString()) note.text = text->AsString();
                    }
                    if (const Value* location = entry.Find("location")) {
                        if (location->IsObject()) {
                            api::Location read;
                            if (const Value* file = location->Find("file")) {
                                if (file->IsString()) read.file = file->AsString();
                            }
                            if (const Value* ns = location->Find("namespace")) {
                                if (ns->IsString()) read.namespace_ = ns->AsString();
                            }
                            if (const Value* line = location->Find("line")) {
                                if (line->IsNumber()) read.line = line->AsNumber();
                            }
                            if (const Value* column = location->Find("column")) {
                                if (column->IsNumber()) read.column = column->AsNumber();
                            }
                            if (const Value* length = location->Find("length")) {
                                if (length->IsNumber()) read.length = length->AsNumber();
                            }
                            if (const Value* line_text = location->Find("lineText")) {
                                if (line_text->IsString()) read.line_text = line_text->AsString();
                            }
                            if (const Value* suggestion = location->Find("suggestion")) {
                                if (suggestion->IsString()) read.suggestion = suggestion->AsString();
                            }
                            note.location = std::move(read);
                        }
                    }
                    message.notes.push_back(std::move(note));
                }
            }
        }

        return message;
    }

} // namespace guchho::service
