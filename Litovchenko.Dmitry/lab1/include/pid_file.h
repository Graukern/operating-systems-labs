#pragma once

#include <string>

class PidFile {
public:
    explicit PidFile(std::string path);
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    void stopPrevious(int timeoutMs = 5000);
    void acquire();
    void release();

    const std::string& path() const { return path_; }

private:
    std::string path_;
    int fd_ = -1;
};
