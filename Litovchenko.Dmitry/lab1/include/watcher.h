#pragma once

#include <sys/inotify.h>

#include <cstdint>
#include <map>
#include <string>

#include "config.h"

class Watcher {
public:
    Watcher();
    ~Watcher();

    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    int fd() const { return fd_; }

    void apply(const Config& config);
    void handleEvents();

    int pollTimeoutMs() const;
    void flushDue(bool all = false);

    std::string stats() const;

private:
    struct Watch {
        std::string dir;
        std::string root;
    };
    struct Pending {
        int64_t lastMs = 0;
        unsigned count = 0;
    };

    size_t addTree(const std::string& dir, const WatchRule& rule);
    void removeTree(const std::string& dir);
    void renameTree(const std::string& from, const std::string& to);
    void removeRule(const std::string& root);

    bool process(const inotify_event& ev, const inotify_event* next);
    void emit(const std::string& type, bool isDir, const std::string& fields, unsigned count = 1);
    void flushPath(const std::string& path);

    int fd_ = -1;
    int debounceMs_ = 0;
    std::map<std::string, WatchRule> rules_;
    std::map<int, Watch> watches_;
    std::map<std::string, Pending> pending_;
    std::map<std::string, uint64_t> counters_;
    bool limitReported_ = false;
};
