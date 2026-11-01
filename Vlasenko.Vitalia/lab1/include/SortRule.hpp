#pragma once

#include <filesystem>
#include <string>

struct SortRule {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::string extension;
    std::string subfolder;
};
