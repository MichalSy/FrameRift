#pragma once

#include "library.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace framerift {

std::vector<Game> download_demo_games(const std::string& api_key,
    const std::filesystem::path& artwork_directory);

} // namespace framerift
