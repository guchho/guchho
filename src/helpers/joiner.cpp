#include "guchho/helpers.hpp"
#include <algorithm>
#include <cstring>

namespace guchho::helpers {
    
    // Appends a string to the joiner.
    //
    // The string is copied into an internal buffer and its starting offset
    // is recorded so that Done() can place it at the correct position.
    // The last byte is remembered for EnsureNewlineAtEnd().
    //
    // Example:
    //   Joiner j;
    //   j.AddString("hello");  // length_ = 5
    //   j.AddString(" world"); // length_ = 11
    //   j.Done() => "hello world"
    void Joiner::AddString(std::string_view data)
    {
        if (!data.empty()) {
            lastByte_ = static_cast<uint8_t>(data.back());
        }
        strings_.push_back({std::string(data), length_});
        length_ += static_cast<uint32_t>(data.size());
    }

    // Appends a raw byte span to the joiner.
    //
    // Unlike AddString this does not copy the data - it stores a pointer
    // to the caller's memory.  The caller must ensure the span remains
    // valid until Done() is called.
    //
    // Example:
    //   char buf[] = {0x01, 0x02, 0x03};
    //   Joiner j;
    //   j.AddBytes(buf);   // length_ = 3
    //   j.Done() => "\x01\x02\x03"
    void Joiner::AddBytes(std::span<const char> data)
    {
        if (!data.empty()) {
            lastByte_ = static_cast<uint8_t>(data.back());
        }
        bytes_.push_back({data, length_});
        length_ += static_cast<uint32_t>(data.size());
    }

    // Ensures the accumulated output ends with a newline character.
    //
    // If the joiner is empty or already ends with '\n', this is a no-op.
    // Otherwise a single '\n' is appended.
    //
    // Example:
    //   Joiner j;
    //   j.AddString("line1");
    //   j.EnsureNewlineAtEnd(); // appends '\n'
    //   j.Done() => "line1\n"
    void Joiner::EnsureNewlineAtEnd()
    {
        if (length_ > 0 && lastByte_ != '\n') {
            AddString("\n");
        }
    }

    // Finalises the joiner and returns the concatenated result.
    //
    // All previously added strings and byte spans are assembled into a
    // single std::string in the order they were added.  For the common
    // case of a single byte span that was added first, the buffer is
    // returned directly without an extra copy.
    //
    // After calling Done() the joiner should not be reused - its
    // internal state is not cleared.
    //
    // Example:
    //   Joiner j;
    //   j.AddString("abc");
    //   j.AddString("def");
    //   j.Done() => "abcdef"
    std::string Joiner::Done() const
    {
        // Fast path: no strings and exactly one byte span starting at
        // offset 0 - avoid allocating a temporary buffer.
        if (strings_.empty() && bytes_.size() == 1 && bytes_[0].offset == 0) {
            return std::string(bytes_[0].data.data(), bytes_[0].data.size());
        }

        std::string buffer;
        buffer.resize(length_);

        for (const auto& item : strings_) {
            std::memcpy(&buffer[item.offset], item.data.data(), item.data.size());
        }
        for (const auto& item : bytes_) {
            std::memcpy(&buffer[item.offset], item.data.data(), item.data.size());
        }

        return buffer;
    }

    // Checks whether the concatenated output would contain a given
    // substring.
    //
    // Both string and byte-span segments are searched.  For strings a
    // simple find() is used; for byte spans a forward search via
    // std::search is performed.
    //
    // Returns true as soon as the first match is found, without
    // constructing the full output string.
    //
    // Example:
    //   Joiner j;
    //   j.AddString("hello world");
    //   j.Contains("world") => true
    //   j.Contains("xyz")   => false
    bool Joiner::Contains(std::string_view s, std::span<const char> b) const
    {
        for (const auto& item : strings_) {
            if (item.data.find(s) != std::string_view::npos) {
                return true;
            }
        }
        for (const auto& item : bytes_) {
            auto it = std::search(item.data.begin(), item.data.end(),
                                b.begin(), b.end());
            if (it != item.data.end()) {
                return true;
            }
        }
        return false;
    }
}