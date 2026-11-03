#include "pid_file.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include "log.h"

namespace {

std::runtime_error sysError(const std::string& what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}

std::string exePath(const std::string& procDir) {
    char buf[PATH_MAX];
    ssize_t n = readlink((procDir + "/exe").c_str(), buf, sizeof(buf) - 1);
    if (n < 0) return {};
    std::string path(buf, static_cast<size_t>(n));
    const std::string deleted = " (deleted)";
    if (path.size() > deleted.size() &&
        path.compare(path.size() - deleted.size(), deleted.size(), deleted) == 0) {
        path.resize(path.size() - deleted.size());
    }
    return path;
}

char processState(pid_t pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string stat;
    if (!std::getline(in, stat)) return 0;
    size_t paren = stat.rfind(')');
    if (paren == std::string::npos || paren + 2 >= stat.size()) return 0;
    return stat[paren + 2];
}

bool isAlive(pid_t pid) {
    char state = processState(pid);
    return state != 0 && state != 'Z';
}

pid_t readPid(const std::string& path) {
    std::ifstream in(path);
    long value = 0;
    if (!(in >> value) || value <= 0 || value > INT_MAX) return 0;
    return static_cast<pid_t>(value);
}

void sleepMs(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

}  // namespace

PidFile::PidFile(std::string path) : path_(std::move(path)) {}

PidFile::~PidFile() {
    release();
}

void PidFile::stopPrevious(int timeoutMs) {
    pid_t pid = readPid(path_);
    if (pid <= 0 || pid == getpid() || !isAlive(pid)) return;

    // The pid may have been reused by an unrelated process.
    std::string self = exePath("/proc/self");
    std::string other = exePath("/proc/" + std::to_string(pid));
    if (other.empty() || other != self) {
        dlog::warning("pid file %s points to pid %d which is not diskmon, ignoring it",
                      path_.c_str(), pid);
        return;
    }

    dlog::info("stopping previous instance pid=%d", pid);
    if (kill(pid, SIGTERM) < 0 && errno != ESRCH) {
        throw sysError("kill(" + std::to_string(pid) + ")");
    }
    const int stepMs = 50;
    for (int waited = 0; isAlive(pid); waited += stepMs) {
        if (waited >= timeoutMs) {
            throw std::runtime_error("previous instance pid=" + std::to_string(pid) +
                                     " did not exit after SIGTERM");
        }
        sleepMs(stepMs);
    }
}

void PidFile::acquire() {
    int fd = open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) throw sysError("open " + path_);
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        int err = errno;
        ::close(fd);
        if (err == EWOULDBLOCK) {
            throw std::runtime_error("another instance holds " + path_);
        }
        errno = err;
        throw sysError("flock " + path_);
    }
    std::string text = std::to_string(getpid()) + "\n";
    if (ftruncate(fd, 0) < 0 ||
        pwrite(fd, text.data(), text.size(), 0) != static_cast<ssize_t>(text.size())) {
        int err = errno;
        ::close(fd);
        errno = err;
        throw sysError("write " + path_);
    }
    fd_ = fd;
}

void PidFile::release() {
    if (fd_ < 0) return;
    unlink(path_.c_str());
    ::close(fd_);
    fd_ = -1;
}
