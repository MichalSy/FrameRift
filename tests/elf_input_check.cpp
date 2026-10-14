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
    word(104, 4096, 8); word(122, 64, 2); word(124, 1, 2); write(source, self);
    framerift::prepare_elf_input(source, output);
    require(read(output) == elf && read(source) == self);
    const auto original_self = self;
    word(12, 256, 2); word(14, 0, 2); word(120, 2, 2);
    word(184, 4, 4); word(192, 1024, 8); word(216, 8, 8);
    write(source, self);
    framerift::prepare_elf_input(source, output);
    const auto without_note = read(output);
    require(std::all_of(without_note.begin() + 120, without_note.begin() + 176, [](auto byte) { return byte == 0; }));
    require(std::equal(without_note.begin() + 256, without_note.end(), self.begin() + 256) && read(source) == self);
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
