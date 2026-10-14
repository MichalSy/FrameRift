// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
#include "elf_input.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace framerift {
namespace {
std::uint64_t Read(const std::vector<std::uint8_t>& source, std::size_t offset, std::size_t bytes) {
    if (offset > source.size() || bytes > source.size() - offset) throw std::runtime_error("Truncated SELF field");
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < bytes; ++index) value |= std::uint64_t(source[offset + index]) << (index * 8u);
    return value;
}
std::uint16_t ReadU16(const std::vector<std::uint8_t>& source, std::size_t offset) { return Read(source, offset, 2); }
std::uint32_t ReadU32(const std::vector<std::uint8_t>& source, std::size_t offset) { return Read(source, offset, 4); }
std::uint64_t ReadU64(const std::vector<std::uint8_t>& source, std::size_t offset) { return Read(source, offset, 8); }

std::vector<std::uint8_t> DecodeSelf(const std::vector<std::uint8_t>& source, const std::string& path) {
    const auto fail = [&](const std::string& message) { throw std::runtime_error(path + ": " + message); };
    const auto range = [&](std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
        if (offset > limit || size > limit - offset) fail("SELF range exceeds file bounds");
    };
    range(0, 32, source.size());
    if (source[4] != 0 || source[5] != 1 || source[6] != 1) fail("Unsupported SELF header; expected a plaintext little-endian container");
    const auto entryCount = ReadU16(source, 24);
    const std::uint64_t elfOffset = 32 + static_cast<std::uint64_t>(entryCount) * 32;
    const auto headerSize = ReadU16(source, 12);
    const std::uint64_t payloadStart = static_cast<std::uint64_t>(headerSize) + ReadU16(source, 14);
    range(0, payloadStart, source.size());
    range(elfOffset, 64, headerSize);
    if (ReadU32(source, elfOffset) != 0x464c457f || source[elfOffset + 4] != 2 || source[elfOffset + 5] != 1 || source[elfOffset + 6] != 1 || ReadU16(source, elfOffset + 18) != 62 || ReadU32(source, elfOffset + 20) != 1) fail("SELF does not contain a little-endian ELF64 x86-64 image");
    const auto phOffset = ReadU64(source, elfOffset + 32);
    const auto phCount = ReadU16(source, elfOffset + 56);
    if (ReadU16(source, elfOffset + 52) != 64 || ReadU16(source, elfOffset + 54) != 56 || phCount == 0 || phOffset < 64) fail("Invalid SELF ELF program header table");
    range(phOffset, static_cast<std::uint64_t>(phCount) * 56, headerSize - elfOffset);
    const auto elfHeaderSize = phOffset + static_cast<std::uint64_t>(phCount) * 56;

    struct Segment {
        std::uint32_t type;
        std::uint64_t address;
        std::uint64_t offset;
        std::uint64_t size;
        std::uint64_t sourceOffset = 0;
    };
    std::vector<Segment> segments;
    constexpr std::uint64_t maxImageSize = 2ull * 1024 * 1024 * 1024;
    for (std::uint16_t index = 0; index < phCount; ++index) {
        const auto position = elfOffset + phOffset + static_cast<std::uint64_t>(index) * 56;
        Segment segment{ReadU32(source, position), ReadU64(source, position + 16), ReadU64(source, position + 8), ReadU64(source, position + 32)};
        if (segment.offset > maxImageSize || segment.size > maxImageSize - segment.offset) fail("SELF program image exceeds the supported 2 GiB size limit");
        segments.push_back(segment);
    }
    for (std::uint16_t index = 0; index < entryCount; ++index) {
        const auto position = 32 + static_cast<std::uint64_t>(index) * 32;
        const auto properties = ReadU64(source, position);
        if ((properties & 2) != 0) fail("Encrypted SELF segments are unsupported; provide plaintext input");
        if ((properties & 8) != 0) fail("Compressed SELF segments are unsupported; provide plaintext input");
        const auto offset = ReadU64(source, position + 8);
        const auto size = ReadU64(source, position + 16);
        range(offset, size, source.size());
        if (offset < payloadStart || size != ReadU64(source, position + 24)) fail("Invalid plaintext SELF segment size or offset");
        if ((properties & (1ull << 11)) == 0) continue;
        const auto segmentIndex = (properties >> 20) & 0xffff;
        if (segmentIndex >= segments.size()) fail("Invalid SELF program segment index");
        auto& segment = segments[segmentIndex];
        if (segment.sourceOffset != 0) fail("Duplicate SELF program segment");
        if (size != segment.size || segment.offset < elfHeaderSize) fail("Invalid SELF program segment size or offset");
        for (const auto& other : segments) {
            if (other.sourceOffset != 0 && segment.offset < other.offset + other.size && other.offset < segment.offset + segment.size) fail("Overlapping SELF program segments");
        }
        segment.sourceOffset = offset;
    }
    for (const auto& segment : segments) {
        if (segment.size == 0 || segment.type == 0x6fffff00 || segment.type == 0x6fffff01 || (segment.type == 4 && segment.address == 0)) continue;
        const bool present = std::any_of(segments.begin(), segments.end(), [&](const auto& other) {
            return other.sourceOffset != 0 && segment.offset >= other.offset && segment.offset - other.offset <= other.size && segment.size <= other.size - (segment.offset - other.offset);
        });
        if (!present) fail("SELF is missing required program segment data");
    }
    std::uint64_t imageSize = elfHeaderSize;
    for (const auto& segment : segments) {
        if (segment.sourceOffset != 0) imageSize = std::max(imageSize, segment.offset + segment.size);
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(imageSize));
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(elfOffset), static_cast<std::size_t>(elfHeaderSize), image.begin());
    for (const auto& segment : segments) {
        if (segment.sourceOffset != 0) std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(segment.sourceOffset), static_cast<std::size_t>(segment.size), image.begin() + static_cast<std::ptrdiff_t>(segment.offset));
    }
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto& segment = segments[index];
        const bool retained = std::any_of(segments.begin(), segments.end(), [&](const auto& other) {
            return other.sourceOffset != 0 && segment.offset >= other.offset && segment.offset - other.offset <= other.size &&
                segment.size <= other.size - (segment.offset - other.offset);
        });
        if (segment.size != 0 && !retained) {
            // Optional ELF notes/comments can be absent from SELF. Mark their unavailable
            // headers PT_NULL so a strict ELF reader does not follow dangling file offsets.
            const auto position = phOffset + index * 56u;
            std::fill(image.begin() + position, image.begin() + position + 56u, 0u);
        }
    }
    // SELF retains program segments, but its embedded ELF may still refer to an absent
    // section table. Remove that unavailable table without changing any program data.
    const auto sections = ReadU64(image, 40);
    const auto sectionBytes = static_cast<std::uint64_t>(ReadU16(image, 58)) * std::max<std::uint16_t>(ReadU16(image, 60), 1u);
    if (sections != 0 && (sections > image.size() || sectionBytes > image.size() - sections)) {
        std::fill(image.begin() + 40, image.begin() + 48, 0u);
        std::fill(image.begin() + 58, image.begin() + 64, 0u);
    }
    return image;
}
} // namespace

void prepare_elf_input(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::ifstream file(source, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 0 || size > 2ll * 1024 * 1024 * 1024) throw std::runtime_error("Cannot read conversion input: " + source.string());
    file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("Cannot read conversion input: " + source.string());
    if (bytes.size() >= 4 && (ReadU32(bytes, 0) == 0x1d3d154f || ReadU32(bytes, 0) == 0xeef51454)) bytes = DecodeSelf(bytes, source.string());
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream output(destination, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    output.close();
    if (!output) throw std::runtime_error("Cannot write temporary ELF input: " + destination.string());
}

} // namespace framerift
