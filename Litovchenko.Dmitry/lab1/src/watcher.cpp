#include "watcher.h"

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <vector>

#include "log.h"

namespace {

const uint32_t kInternalMask = IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF |
                               IN_ONLYDIR | IN_DONT_FOLLOW | IN_EXCL_UNLINK;

std::runtime_error sysError(const std::string& what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}

int64_t nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool isUnder(const std::string& path, const std::string& dir) {
    return path == dir || path.compare(0, dir.size() + 1, dir + "/") == 0;
}

bool isExcluded(const WatchRule& rule, const std::string& name) {
    for (const auto& pattern : rule.exclude) {
        if (fnmatch(pattern.c_str(), name.c_str(), 0) == 0) return true;
    }
    return false;
}

// Escaping control characters: a file name with '\n' must not forge a log line.
std::string quote(const std::string& value) {
    bool plain = !value.empty();
    for (unsigned char c : value) {
        if (c <= ' ' || c == '"' || c == '\\' || c == '=' || c == 0x7f) plain = false;
    }
    if (plain) return value;
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < ' ' || c == 0x7f) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\x%02x", c);
            out += hex;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string field(const char* key, const std::string& value) {
    return std::string(" ") + key + "=" + quote(value);
}

const char* typeName(uint32_t mask) {
    if (mask & IN_CREATE) return "CREATE";
    if (mask & IN_DELETE) return "DELETE";
    if (mask & IN_MODIFY) return "MODIFY";
    if (mask & IN_ATTRIB) return "ATTRIB";
    if (mask & IN_OPEN) return "OPEN";
    if (mask & IN_ACCESS) return "ACCESS";
    if (mask & IN_CLOSE_WRITE) return "CLOSE_WRITE";
    if (mask & IN_CLOSE_NOWRITE) return "CLOSE_NOWRITE";
    return "UNKNOWN";
}

}  // namespace

Watcher::Watcher() {
    fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) throw sysError("inotify_init1");
}

Watcher::~Watcher() {
    if (fd_ >= 0) close(fd_);
}

void Watcher::apply(const Config& config) {
    flushDue(true);
    debounceMs_ = config.debounceMs;

    std::map<std::string, const WatchRule*> fresh;
    for (const auto& rule : config.rules) fresh[rule.path] = &rule;

    std::vector<std::string> changed;
    for (const auto& [root, rule] : rules_) {
        auto it = fresh.find(root);
        if (it == fresh.end() || *it->second != rule) changed.push_back(root);
    }
    for (const auto& root : changed) {
        removeRule(root);
        if (!fresh.count(root)) dlog::info("unwatch %s", root.c_str());
    }
    for (const auto& rule : config.rules) {
        if (rules_.count(rule.path)) continue;
        rules_[rule.path] = rule;
        size_t dirs = addTree(rule.path, rule);
        dlog::info("watch %s dirs=%zu", describe(rule).c_str(), dirs);
    }
}

size_t Watcher::addTree(const std::string& dir, const WatchRule& rule) {
    int wd = inotify_add_watch(fd_, dir.c_str(), rule.mask | kInternalMask);
    if (wd < 0) {
        if (errno == ENOSPC) {
            if (!limitReported_) {
                dlog::error("cannot watch %s: watch limit reached, raise fs.inotify.max_user_watches",
                            dir.c_str());
                limitReported_ = true;
            }
        } else {
            dlog::warning("cannot watch %s: %s", dir.c_str(), std::strerror(errno));
        }
        return 0;
    }
    watches_[wd] = {dir, rule.path};
    if (!rule.recursive) return 1;

    std::vector<std::string> subdirs;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* entry = readdir(d)) {
            std::string name = entry->d_name;
            if (name == "." || name == ".." || isExcluded(rule, name)) continue;
            bool isDir = entry->d_type == DT_DIR;
            if (entry->d_type == DT_UNKNOWN) {
                struct stat st;
                isDir = lstat((dir + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
            }
            if (isDir) subdirs.push_back(dir + "/" + name);
        }
        closedir(d);
    }
    size_t count = 1;
    for (const auto& sub : subdirs) count += addTree(sub, rule);
    return count;
}

void Watcher::removeTree(const std::string& dir) {
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (isUnder(it->second.dir, dir)) {
            inotify_rm_watch(fd_, it->first);
            it = watches_.erase(it);
        } else {
            ++it;
        }
    }
}

void Watcher::renameTree(const std::string& from, const std::string& to) {
    for (auto& [wd, watch] : watches_) {
        if (isUnder(watch.dir, from)) watch.dir = to + watch.dir.substr(from.size());
    }
}

void Watcher::removeRule(const std::string& root) {
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (it->second.root == root) {
            inotify_rm_watch(fd_, it->first);
            it = watches_.erase(it);
        } else {
            ++it;
        }
    }
    rules_.erase(root);
}

void Watcher::handleEvents() {
    alignas(inotify_event) char buf[64 * 1024];
    for (;;) {
        ssize_t len = read(fd_, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EAGAIN) return;
            if (errno == EINTR) continue;
            throw sysError("read(inotify)");
        }
        const char* end = buf + len;
        const inotify_event* skip = nullptr;
        for (const char* p = buf; p < end;) {
            const auto* ev = reinterpret_cast<const inotify_event*>(p);
            p += sizeof(inotify_event) + ev->len;
            const auto* next = p < end ? reinterpret_cast<const inotify_event*>(p) : nullptr;
            if (ev == skip) continue;
            if (process(*ev, next)) skip = next;
        }
    }
}

bool Watcher::process(const inotify_event& ev, const inotify_event* next) {
    if (ev.mask & IN_Q_OVERFLOW) {
        ++counters_["OVERFLOW"];
        dlog::warning("inotify queue overflow, events were lost (fs.inotify.max_queued_events)");
        return false;
    }
    auto wit = watches_.find(ev.wd);
    if (wit == watches_.end()) return false;
    if (ev.mask & IN_IGNORED) {
        watches_.erase(wit);
        return false;
    }
    const Watch watch = wit->second;
    const WatchRule& rule = rules_.at(watch.root);
    const bool isDir = ev.mask & IN_ISDIR;
    const std::string name = ev.len ? ev.name : "";

    if (ev.mask & IN_DELETE_SELF) {
        if (watch.dir == watch.root) dlog::warning("watched directory %s was removed", watch.dir.c_str());
        return false;
    }
    if (name.empty() && watch.dir != watch.root) return false;
    if (!name.empty() && isExcluded(rule, name)) return false;
    const std::string path = name.empty() ? watch.dir : watch.dir + "/" + name;

    if (ev.mask & IN_MOVED_FROM) {
        if (next && (next->mask & IN_MOVED_TO) && next->cookie == ev.cookie) {
            auto nit = watches_.find(next->wd);
            if (nit != watches_.end()) {
                const WatchRule& toRule = rules_.at(nit->second.root);
                const std::string to = nit->second.dir + "/" + next->name;
                if (isDir) {
                    if (nit->second.root == watch.root) {
                        renameTree(path, to);
                    } else {
                        removeTree(path);
                        if (toRule.recursive) addTree(to, toRule);
                    }
                }
                if ((rule.mask | toRule.mask) & IN_MOVED_FROM) {
                    flushPath(path);
                    emit("RENAME", isDir, field("from", path) + field("to", to));
                }
                return true;
            }
        }
        if (isDir) removeTree(path);
        if (rule.mask & IN_MOVED_FROM) {
            flushPath(path);
            emit("MOVE_OUT", isDir, field("path", path));
        }
        return false;
    }
    if (ev.mask & IN_MOVED_TO) {
        if (isDir && rule.recursive) addTree(path, rule);
        if (rule.mask & IN_MOVED_TO) emit("MOVE_IN", isDir, field("path", path));
        return false;
    }

    if ((ev.mask & IN_CREATE) && isDir && rule.recursive) addTree(path, rule);
    if (!(ev.mask & rule.mask)) return false;
    if (isDir && (ev.mask & (IN_OPEN | IN_ACCESS | IN_CLOSE_NOWRITE))) return false;

    if ((ev.mask & IN_MODIFY) && debounceMs_ > 0) {
        Pending& p = pending_[path];
        p.lastMs = nowMs();
        ++p.count;
        return false;
    }
    flushPath(path);
    emit(typeName(ev.mask), isDir, field("path", path));
    return false;
}

void Watcher::emit(const std::string& type, bool isDir, const std::string& fields, unsigned count) {
    counters_[type] += count;
    std::string line = "event=" + type + " kind=" + (isDir ? "dir" : "file") + fields;
    if (count > 1) line += " count=" + std::to_string(count);
    dlog::event(line);
}

void Watcher::flushPath(const std::string& path) {
    auto it = pending_.find(path);
    if (it == pending_.end()) return;
    emit("MODIFY", false, field("path", path), it->second.count);
    pending_.erase(it);
}

void Watcher::flushDue(bool all) {
    int64_t now = nowMs();
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (all || now - it->second.lastMs >= debounceMs_) {
            emit("MODIFY", false, field("path", it->first), it->second.count);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

int Watcher::pollTimeoutMs() const {
    if (pending_.empty()) return -1;
    int64_t now = nowMs();
    int64_t timeout = debounceMs_;
    for (const auto& [path, p] : pending_) {
        timeout = std::min<int64_t>(timeout, p.lastMs + debounceMs_ - now);
    }
    return static_cast<int>(std::max<int64_t>(timeout, 0));
}

std::string Watcher::stats() const {
    std::string text = "rules=" + std::to_string(rules_.size()) +
                       " watches=" + std::to_string(watches_.size());
    for (const auto& [type, count] : counters_) {
        text += " " + type + "=" + std::to_string(count);
    }
    return text;
}
