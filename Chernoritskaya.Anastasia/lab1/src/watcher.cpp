#include "watcher.hpp"
#include "events.hpp"

#include <sys/inotify.h>
#include <unistd.h>
#include <cerrno>

namespace {
const uint32_t WATCH_MASK = IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_OPEN | IN_CLOSE_WRITE |
                            IN_CLOSE_NOWRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                            IN_MOVED_TO;
}

Watcher::Watcher() : fd_(inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {}

Watcher::~Watcher() {
    if (fd_ >= 0) close(fd_);
}

std::vector<std::pair<std::string, int>> Watcher::setDirs(const std::vector<std::string>& dirs) {
    std::vector<std::pair<std::string, int>> failed;
    if (fd_ < 0) return failed;
    for (const auto& w : dirs_) inotify_rm_watch(fd_, w.first);
    dirs_.clear();
    for (const auto& d : dirs) {
        int wd = inotify_add_watch(fd_, d.c_str(), WATCH_MASK);
        if (wd < 0)
            failed.emplace_back(d, errno);
        else
            dirs_[wd] = d;
    }
    return failed;
}

std::vector<std::string> Watcher::readEvents() {
    std::vector<std::string> out;
    if (fd_ < 0) return out;
    alignas(struct inotify_event) char buf[8192];
    for (;;) {
        ssize_t len = read(fd_, buf, sizeof(buf));
        if (len <= 0) break;
        for (char* p = buf; p < buf + len;) {
            const struct inotify_event* ev = reinterpret_cast<const struct inotify_event*>(p);
            p += sizeof(struct inotify_event) + ev->len;
            if (ev->mask & IN_Q_OVERFLOW) {
                out.push_back("inotify queue overflow, events lost");
                continue;
            }
            auto it = dirs_.find(ev->wd);
            if (it == dirs_.end() || (ev->mask & IN_IGNORED)) continue;
            out.push_back(events::format(it->second, ev->len ? ev->name : "", ev->mask));
        }
    }
    return out;
}