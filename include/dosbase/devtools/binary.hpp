// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dosbase::devtools {

// A malformed artifact. Inspectors report the offset because the caller is
// usually diffing two artifacts and needs the first divergent position.
class FormatError : public std::runtime_error {
public:
    FormatError(std::size_t offset, const std::string& message)
        : std::runtime_error("offset 0x" + hex(offset) + ": " + message), offset_(offset) {}

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

private:
    [[nodiscard]] static std::string hex(std::size_t value);
    std::size_t offset_{};
};

// Bounds-checked little-endian cursor over a byte range.
class ByteCursor {
public:
    explicit ByteCursor(std::span<const std::uint8_t> bytes, std::size_t base = 0) noexcept
        : bytes_(bytes), base_(base) {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t absolute() const noexcept { return base_ + position_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    [[nodiscard]] bool empty() const noexcept { return remaining() == 0; }

    std::uint8_t byte();
    std::uint16_t word();
    std::uint32_t dword();
    std::span<const std::uint8_t> take(std::size_t count);
    // OMF length-prefixed name.
    std::string name();
    // OMF index: one byte below 0x80, otherwise a 15-bit big-endian pair.
    std::uint16_t index();
    void seek(std::size_t position);

private:
    void require(std::size_t count) const;
    std::span<const std::uint8_t> bytes_;
    std::size_t base_{};
    std::size_t position_{};
};

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path);
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept;
// Lowercase, fixed-width hex; the dump format never varies with value width.
[[nodiscard]] std::string hex8(std::uint8_t value);
[[nodiscard]] std::string hex16(std::uint16_t value);
[[nodiscard]] std::string hex32(std::uint32_t value);
[[nodiscard]] std::string hex_bytes(std::span<const std::uint8_t> bytes);
// Printable rendering of a DOS name: the tools' inputs are CP437 and a raw
// byte in a dump line would make the line unstable under a text diff.
[[nodiscard]] std::string quoted(const std::string& text);

}  // namespace dosbase::devtools
