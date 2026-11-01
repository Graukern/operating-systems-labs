#pragma once

#include "config.hpp"

namespace diskmon {

class Daemon final {
public:
    static Daemon& instance();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    int run(const fs::path& configuration, const fs::path& pidFile);

private:
    Daemon() = default;
    ~Daemon() = default;
};

} // namespace diskmon
