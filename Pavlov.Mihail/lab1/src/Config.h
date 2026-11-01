#pragma once

#include <string>

struct Config {
    std::string dir1;
    int interval = 20;
};

bool loadConfig(const std::string& path, Config& cfg, std::string& error);