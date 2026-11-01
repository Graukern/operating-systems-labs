#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

class Watcher {
public:
    Watcher();
    ~Watcher();
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    bool ok() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    size_t watchCount() const { return dirs_.size(); }

    std::vector<std::pair<std::string, int>> setDirs(const std::vector<std::string>& dirs);
    std::vector<std::string> readEvents();

private:
    int fd_;
    std::map<int, std::string> dirs_;  
};