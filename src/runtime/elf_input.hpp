// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
#pragma once
#include <filesystem>

namespace framerift {
void prepare_elf_input(const std::filesystem::path& source, const std::filesystem::path& destination);
}
