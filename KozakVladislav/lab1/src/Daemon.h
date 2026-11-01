#pragma once

#include "Event.h"
#include <string>
#include <vector>
#include <map>
#include <ctime>

class Daemon {
public:
    static Daemon& getInstance();
    void setConfigPath(const std::string& path);
    void run();

private:
    Daemon();
    ~Daemon() = default;
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    void daemonize();
    void readConfig();
    void writePidFile();
    void removePidFile();
    void setupSignalHandlers();
    void processEvents();
    void openTerminal(const std::string& text) const;
    void checkAndKillOldDaemon();

    std::string configPath_;
    std::string pidPath_;
    std::string terminalCmd_;
    int interval_;
    std::vector<Event> events_;
    std::map<std::string, std::time_t> handled_;
};