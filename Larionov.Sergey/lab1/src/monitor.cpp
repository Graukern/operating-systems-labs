#include "monitor.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>

namespace diskmon {

Monitor::Monitor(std::vector<fs::path> roots) : roots_(std::move(roots)) {
    descriptor_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (descriptor_ < 0) {
        systemError("inotify_init1");
    }
    try {
        for (const auto& root : roots_) {
            addTree(root);
        }
    } catch (...) {
        clear();
        throw;
    }
}

Monitor::~Monitor() { clear(); }

void Monitor::clear() {
    watches_.clear();
    if (descriptor_ >= 0) {
        close(descriptor_);
        descriptor_ = -1;
    }
}

void Monitor::addTree(const fs::path& path) {
    const int watch = inotify_add_watch(descriptor_, path.c_str(),
                                       IN_ALL_EVENTS | IN_ONLYDIR | IN_DONT_FOLLOW);
    if (watch < 0) {
        systemError("inotify_add_watch " + path.string());
    }
    const auto existing = watches_.find(watch);
    if (existing != watches_.end()) {
        const auto old = existing->second;
        if (old != path) {
            for (auto& entry : watches_) {
                if (contains(old, entry.second)) {
                    const auto relative = entry.second.lexically_relative(old);
                    entry.second = relative == "." ? path : path / relative;
                }
            }
        }
        return;
    }
    watches_.emplace(watch, path);
    for (const auto& entry : fs::directory_iterator(path)) {
        // Do not follow symlink directories, including links back to an ancestor.
        if (fs::is_directory(entry.symlink_status())) {
            addTree(entry.path());
        }
    }
}

void Monitor::remove(int watch) {
    const auto found = watches_.find(watch);
    if (found != watches_.end()) {
        watches_.erase(found);
        inotify_rm_watch(descriptor_, watch);
    }
}

void Monitor::removeTree(const fs::path& path) {
    auto it = watches_.begin();
    while (it != watches_.end()) {
        const int watch = it->first;
        const bool child = contains(path, it->second);
        ++it;
        if (child) { remove(watch); }
    }
}

int Monitor::timeout() const {
    return moves_.empty() ? -1 : 100;
}

void Monitor::expireMoves() {
    const auto now = std::chrono::steady_clock::now();
    auto it = moves_.begin();
    while (it != moves_.end()) {
        if (now - it->second.time >= std::chrono::milliseconds(100)) {
            removeTree(it->second.path);
            it = moves_.erase(it);
        } else {
            ++it;
        }
    }
}

void Monitor::event(int watch, std::uint32_t mask, std::uint32_t cookie,
                    const std::string& name) {
    if ((mask & IN_Q_OVERFLOW) != 0) {
        // Continuing silently would falsely claim that all interactions were recorded.
        throw std::runtime_error("IN_Q_OVERFLOW: events lost; restart the monitor");
    }
    const auto found = watches_.find(watch);
    if (found == watches_.end()) {
        return;
    }
    if ((mask & IN_IGNORED) != 0) {
        syslog(LOG_WARNING, "WATCH_REMOVED path=%s", quote(found->second.string()).c_str());
        watches_.erase(found);
        return;
    }
    const auto directory = found->second;
    for (const auto& move : moves_) {
        // Until its matching MOVED_TO arrives, a moved directory may already
        // be outside our roots. Do not report its descendants under stale names.
        if (contains(move.second.path, directory)) { return; }
    }
    const fs::path path = name.empty() ? directory : directory / name;
    const bool isDirectory = (mask & IN_ISDIR) != 0 || name.empty();
    const std::array<std::pair<std::uint32_t, const char*>, 13> types {{
        {IN_ACCESS, "ACCESS"}, {IN_MODIFY, "MODIFY"}, {IN_ATTRIB, "ATTRIB"},
        {IN_CLOSE_WRITE, "CLOSE_WRITE"}, {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
        {IN_OPEN, "OPEN"}, {IN_MOVED_FROM, "MOVED_FROM"}, {IN_MOVED_TO, "MOVED_TO"},
        {IN_CREATE, "CREATE"}, {IN_DELETE, "DELETE"}, {IN_DELETE_SELF, "DELETE_SELF"},
        {IN_MOVE_SELF, "MOVE_SELF"}, {IN_UNMOUNT, "UNMOUNT"}
    }};
    for (const auto& type : types) {
        // Our recursive scans open/read/close directories. Excluding those
        // three directory-only events prevents logging our own traversal.
        const bool traversal = isDirectory &&
            (type.first == IN_OPEN || type.first == IN_ACCESS || type.first == IN_CLOSE_NOWRITE);
        if ((mask & type.first) != 0 && !traversal) {
            syslog(LOG_INFO, "event=%s path=%s kind=%s cookie=%u", type.second,
                   quote(path.string()).c_str(), isDirectory ? "directory" : "file", cookie);
        }
    }
    if ((mask & (IN_DELETE_SELF | IN_UNMOUNT)) != 0) {
        removeTree(directory);
        return;
    }
    if (isDirectory && (mask & IN_MOVED_FROM) != 0) {
        moves_[cookie] = Move{path, std::chrono::steady_clock::now()};
    }
    if (isDirectory && (mask & IN_MOVED_TO) != 0) {
        const auto moved = moves_.find(cookie);
        if (moved != moves_.end()) {
            const auto old = moved->second.path;
            for (auto& entry : watches_) {
                if (contains(old, entry.second)) {
                    const auto relative = entry.second.lexically_relative(old);
                    entry.second = relative == "." ? path : path / relative;
                }
            }
            moves_.erase(moved);
        }
    }
    if ((mask & IN_MOVE_SELF) != 0) {
        for (const auto& root : roots_) {
            if (directory == root) {
                syslog(LOG_WARNING, "ROOT_MOVED monitoring removed; update configuration and send SIGHUP");
                removeTree(directory);
                return;
            }
        }
    }
    if (isDirectory && (mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
        try {
            addTree(path);
            syslog(LOG_INFO, "WATCH_ADDED path=%s", quote(path.string()).c_str());
        } catch (const std::exception& exception) {
            syslog(LOG_ERR, "Cannot watch new directory: %s", exception.what());
        }
    }
}

void Monitor::consume() {
    alignas(inotify_event) std::array<char, 64 * 1024> buffer {};
    // Bound one batch so a busy filesystem cannot starve SIGTERM/SIGHUP.
    for (int batch = 0; batch < 16; ++batch) {
        const auto size = read(descriptor_, buffer.data(), buffer.size());
        if (size < 0) {
            if (errno == EAGAIN) { return; }
            if (errno == EINTR) { continue; }
            systemError("read inotify");
        }
        if (size == 0) { return; }
        std::size_t offset = 0;
        while (offset + sizeof(inotify_event) <= static_cast<std::size_t>(size)) {
            inotify_event notification {};
            std::memcpy(&notification, buffer.data() + offset, sizeof(notification));
            const auto recordSize = sizeof(notification) + notification.len;
            if (offset + recordSize > static_cast<std::size_t>(size)) {
                throw std::runtime_error("Truncated inotify event");
            }
            const char* name = buffer.data() + offset + sizeof(notification);
            event(notification.wd, notification.mask, notification.cookie,
                  std::string(name, strnlen(name, notification.len)));
            offset += recordSize;
        }
    }
}

} // namespace diskmon
