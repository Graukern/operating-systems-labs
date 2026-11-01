#pragma once
#include <functional>
#include <string>

class Watcher;

class Daemon {
public:
    using Sink = std::function<void(int priority, const std::string& msg)>;

    static const char* defaultPidFile() { return "/var/run/diskmon.pid"; }

    static Daemon& instance();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    void setPidFile(const std::string& path) { pidFile_ = path; }
    void setSink(Sink sink) { sink_ = std::move(sink); }

    const std::string& configPath() const { return config_; }
    const std::string& pidFilePath() const { return pidFile_; }
    
    int run(const std::string& configPath);

private:
    Daemon() : pidFile_(defaultPidFile()) {}

    void log(int priority, const std::string& msg);
    bool daemonize();
    void setupSignals();
    bool loadConfig(Watcher& w);
    void loop(Watcher& w);

    std::string config_;
    std::string pidFile_;
    Sink sink_;
};
