#pragma once

#include "config.hpp"
#include <chrono>
#include <cstdint>
#include <map>

namespace diskmon {

class Monitor {
public:
    explicit Monitor(std::vector<fs::path> roots);
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    int descriptor() const { return descriptor_; }
    void consume();
    int timeout() const;
    void expireMoves();

private:
    struct Move {
        fs::path path;
        std::chrono::steady_clock::time_point time;
    };
    int descriptor_ = -1;
    std::vector<fs::path> roots_;
    std::map<int, fs::path> watches_;
    std::map<std::uint32_t, Move> moves_;
    void addTree(const fs::path& path);
    void event(int watch, std::uint32_t mask, std::uint32_t cookie,
               const std::string& name);
    void remove(int watch);
    void removeTree(const fs::path& path);
    void clear();
};

} // namespace diskmon
