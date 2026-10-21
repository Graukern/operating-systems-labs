#pragma once

#include <string>

struct Config {
    std::string dir1;
    std::string dir2;
    int interval = 30;
};

bool loadConfig(const std::string& path, Config& cfg, std::string& error);