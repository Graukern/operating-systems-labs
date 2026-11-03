#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WatchRule {
    std::string path;
    bool recursive = true;
    uint32_t mask = 0;
    std::vector<std::string> exclude;

    bool operator==(const WatchRule& other) const;
    bool operator!=(const WatchRule& other) const { return !(*this == other); }
};

struct Config {
    std::vector<WatchRule> rules;
    int debounceMs = 0;
};

Config loadConfig(const std::string& path);

std::string describe(const WatchRule& rule);

std::string eventNames(uint32_t mask);
