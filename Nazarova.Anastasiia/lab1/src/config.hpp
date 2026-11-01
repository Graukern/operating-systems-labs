#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Config {
    std::vector<std::filesystem::path> directories;
    std::filesystem::path event_log_dir;
    std::filesystem::path pid_file;
    static Config load(const std::filesystem::path& filename);
};

std::string quote(const std::string& value);
