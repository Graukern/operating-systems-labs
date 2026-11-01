#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace diskmon {
namespace fs = std::filesystem;

std::vector<fs::path> loadConfig(const fs::path& file);
bool contains(const fs::path& directory, const fs::path& path);
std::string quote(const std::string& value);
[[noreturn]] void systemError(const std::string& operation);

} // namespace diskmon
