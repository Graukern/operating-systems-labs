#pragma once

#include <ctime>
#include <memory>
#include <string>

#include "config.h"
#include "pid_file.h"
#include "watcher.h"

struct Options {
    std::string configPath = "diskmon.conf";
    std::string pidPath;
    bool foreground = false;
};

class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    int run(const Options& options);

private:
    Daemon() = default;

    void daemonize();
    void reportStartup(const std::string& message);
    void setupSignals();
    void loop();
    void handleSignals();
    void reload();
    void logStats();

    Options options_;
    std::string configPath_;
    Config config_;
    std::unique_ptr<PidFile> pidFile_;
    std::unique_ptr<Watcher> watcher_;
    int signalFd_ = -1;
    int startupFd_ = -1;
    bool running_ = false;
    std::string stopReason_;
    std::time_t startedAt_ = 0;
};
