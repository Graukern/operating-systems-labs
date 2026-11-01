#include "events.hpp"

#include <sys/inotify.h>
#include <utility>
#include <vector>

namespace events {

std::string names(uint32_t mask) {
    static const std::vector<std::pair<uint32_t, const char*>> table = {
        {IN_ACCESS, "ACCESS"},
        {IN_MODIFY, "MODIFY"},
        {IN_ATTRIB, "ATTRIB"},
        {IN_OPEN, "OPEN"},
        {IN_CLOSE_WRITE, "CLOSE_WRITE"},
        {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
        {IN_CREATE, "CREATE"},
        {IN_DELETE, "DELETE"},
        {IN_MOVED_FROM, "MOVED_FROM"},
        {IN_MOVED_TO, "MOVED_TO"},
    };
    std::string r;
    for (const auto& e : table) {
        if (mask & e.first) {
            if (!r.empty()) r += '|';
            r += e.second;
        }
    }
    return r.empty() ? "UNKNOWN" : r;
}

std::string format(const std::string& dir, const std::string& name, uint32_t mask) {
    std::string s = dir;
    if (!name.empty()) s += "/" + name;
    s += " " + names(mask);
    if (mask & IN_ISDIR) s += " (dir)";
    return s;
}
}  