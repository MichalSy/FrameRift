// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
#include "runtime/elf_input.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <glib.h>

static void require(bool value) { if (!value) throw std::runtime_error("ELF input preparation check failed"); }
static void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
static std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
int main() {
    const auto* id = g_uuid_string_random();
    const auto root = std::filesystem::temp_directory_path() / ("framerift-elf-" + std::string(id));
    g_free(const_cast<char*>(id));
    std::filesystem::create_directory(root);
    const auto source = root / "original.bin";
    const auto output = root / "input.elf";
    std::vector<std::uint8_t> self(260);
    const auto word = [&](std::size_t offset, std::uint64_t value, std::size_t bytes) {
        for (std::size_t index = 0; index < bytes; ++index) self[offset + index] = value >> (index * 8u);
    };
    word(0, 0x1d3d154f, 4); self[5] = 1; self[6] = 1;
    word(12, 192, 2); word(14, 64, 2); word(24, 1, 2);
    word(32, 1u << 11u, 8); word(40, 256, 8); word(48, 4, 8); word(56, 4, 8);
    word(64, 0x464c457f, 4); self[68] = 2; self[69] = 1; self[70] = 1;
    word(82, 62, 2); word(84, 1, 4); word(96, 64, 8);
    word(116, 64, 2); word(118, 56, 2); word(120, 1, 2);
    word(128, 1, 4); word(136, 256, 8); word(144, 0x400000, 8); word(160, 4, 8);
    self[256] = 1; self[257] = 2; self[258] = 3; self[259] = 4;
    write(source, self);
    framerift::prepare_elf_input(source, output);
    const auto elf = read(output);
    require(elf.size() == 260 && std::equal(elf.begin(), elf.begin() + 120, self.begin() + 64));
    require(std::equal(elf.begin() + 256, elf.end(), self.begin() + 256) && read(source) == self);
    // Reproduce the PS5 SELF header used by GT7 and Wobbly Life's PS5Util.
    word(0, 0xeef51454, 4); self[4] = 0x10;
    word(8, 0x10000101, 4); word(26, 0x32, 2);
    write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == elf && read(source) == self);
    const auto ps5_self = self;
    for (const auto flag : {2u, 8u}) {
        word(32, (1u << 11u) | flag, 8); write(source, self);
        bool rejected = false;
        try { framerift::prepare_elf_input(source, output); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && read(source) == self && read(output) == elf);
    }
    self = ps5_self;
    self[4] = 0x11; self[5] = 2; write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == elf && read(source) == self);
    self = ps5_self;
    word(0, 0x1d3d154f, 4); self[4] = 0;
    word(8, 0, 4); word(26, 0, 2);
    word(104, 4096, 8); word(122, 64, 2); word(124, 1, 2); write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == elf && read(source) == self);
    const auto original_self = self;
    word(12, 256, 2); word(14, 0, 2); word(120, 2, 2);
    word(184, 4, 4); word(192, 0x80000000ull, 8); word(216, 8, 8);
    write(source, self);
    framerift::prepare_elf_input(source, output);
    const auto without_note = read(output);
    require(std::all_of(without_note.begin() + 120, without_note.begin() + 176, [](auto byte) { return byte == 0; }));
    require(std::equal(without_note.begin() + 256, without_note.end(), self.begin() + 256) && read(source) == self);
    // An unused signature entry must not reject otherwise readable program data.
    const auto note_self = self;
    self.insert(self.begin() + 64, 32, 0);
    word(24, 2, 2); word(12, 288, 2); word(40, 288, 8);
    word(64, 2u | 8u, 8); word(72, 0xffffffffffffffffull, 8);
    write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == without_note && read(source) == self);
    self = note_self;
    // Invalid data ranges and missing program data still fail before writing output.
    for (const auto invalid : {0, 1, 2}) {
        self = note_self;
        if (invalid == 0) word(40, self.size(), 8);
        if (invalid == 1) word(32, 0, 8);
        if (invalid == 2) self[6] = 2;
        write(source, self);
        bool rejected = false;
        try { framerift::prepare_elf_input(source, output); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && read(output) == without_note && read(source) == self);
    }
    self = original_self;
    // Container padding is not part of the ELF program segment.
    self.insert(self.end(), {0xaa, 0xbb});
    word(48, 6, 8); word(56, 6, 8); write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == elf && read(source) == self);
    // Duplicate mappings are harmless when they contain the same program bytes.
    self = original_self;
    self.insert(self.begin() + 64, 32, 0);
    word(24, 2, 2); word(12, 224, 2); word(40, 288, 8);
    word(64, 1u << 11u, 8); word(72, 292, 8); word(80, 4, 8); word(88, 4, 8);
    self.insert(self.end(), {1, 2, 3, 4}); write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == elf && read(source) == self);
    self.back() = 5; write(source, self);
    bool rejected_conflict = false;
    try { framerift::prepare_elf_input(source, output); } catch (const std::runtime_error&) { rejected_conflict = true; }
    require(rejected_conflict && read(output) == elf && read(source) == self);
    self = original_self;
    write(source, elf);
    framerift::prepare_elf_input(source, output);
    require(read(source) == elf && read(output) == elf);
    for (const auto flag : {2u, 8u}) {
        word(32, (1u << 11u) | flag, 8); write(source, self);
        bool rejected = false;
        try { framerift::prepare_elf_input(source, output); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && read(source) == self && read(output) == elf);
    }
    std::filesystem::remove_all(root);
}
