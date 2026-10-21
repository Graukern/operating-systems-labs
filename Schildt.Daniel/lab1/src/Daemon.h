#pragma once

#include <csignal>
#include <string>

#include "Config.h"

class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    bool setup(const std::string& configArg, std::string& error);

    void run();

private:
    Daemon() = default;

    void daemonize();
    void stopPreviousInstance();
    void writePidFile();
    bool appendHistory(std::string& error);
    void reloadConfig();

    static void handleSignal(int sig);

    Config config_;
    std::string configPath_;

    static volatile std::sig_atomic_t terminate_;
    static volatile std::sig_atomic_t reload_;
};