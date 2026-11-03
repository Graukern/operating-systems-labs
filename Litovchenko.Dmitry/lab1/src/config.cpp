#include "config.h"

#include <sys/inotify.h>
#include <sys/stat.h>

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

struct EventName {
    const char* name;
    uint32_t mask;
};

const EventName kEvents[] = {
    {"access", IN_ACCESS},
    {"modify", IN_MODIFY},
    {"attrib", IN_ATTRIB},
    {"open", IN_OPEN},
    {"close", IN_CLOSE_WRITE | IN_CLOSE_NOWRITE},
    {"create", IN_CREATE},
    {"delete", IN_DELETE},
    {"move", IN_MOVED_FROM | IN_MOVED_TO},
};

uint32_t allEvents() {
    uint32_t mask = 0;
    for (const auto& e : kEvents) {
        mask |= e.mask;
    }
    return mask;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> words;
    std::string word;
    bool inQuotes = false;
    bool hasWord = false;
    for (char c : line) {
        if (inQuotes) {
            if (c == '"') {
                inQuotes = false;
            } else {
                word += c;
            }
        } else if (c == '"') {
            inQuotes = true;
            hasWord = true;
        } else if (c == '#') {
            break;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (hasWord) {
                words.push_back(word);
                word.clear();
                hasWord = false;
            }
        } else {
            word += c;
            hasWord = true;
        }
    }
    if (inQuotes) {
        throw std::runtime_error("unterminated quote");
    }
    if (hasWord) {
        words.push_back(word);
    }
    return words;
}

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> items;
    std::string item;
    for (char c : text) {
        if (c == ',') {
            if (!item.empty()) items.push_back(item);
            item.clear();
        } else {
            item += c;
        }
    }
    if (!item.empty()) items.push_back(item);
    return items;
}

uint32_t parseEvents(const std::string& text) {
    uint32_t mask = 0;
    for (const auto& name : splitList(text)) {
        if (name == "all") {
            mask |= allEvents();
            continue;
        }
        bool found = false;
        for (const auto& e : kEvents) {
            if (name == e.name) {
                mask |= e.mask;
                found = true;
            }
        }
        if (!found) {
            throw std::runtime_error("unknown event '" + name + "'");
        }
    }
    if (mask == 0) {
        throw std::runtime_error("empty event list");
    }
    return mask;
}

bool parseBool(const std::string& text) {
    if (text == "yes" || text == "true" || text == "1") return true;
    if (text == "no" || text == "false" || text == "0") return false;
    throw std::runtime_error("expected yes/no, got '" + text + "'");
}

std::string canonicalDir(const std::string& path, const std::string& baseDir) {
    std::string full = (!path.empty() && path[0] == '/') ? path : baseDir + "/" + path;
    char resolved[PATH_MAX];
    if (realpath(full.c_str(), resolved) == nullptr) {
        throw std::runtime_error(full + ": " + std::strerror(errno));
    }
    struct stat st;
    if (stat(resolved, &st) < 0 || !S_ISDIR(st.st_mode)) {
        throw std::runtime_error(std::string(resolved) + ": not a directory");
    }
    return resolved;
}

bool isInside(const std::string& path, const std::string& dir) {
    if (dir == "/") return true;
    return path == dir || path.compare(0, dir.size() + 1, dir + "/") == 0;
}

WatchRule parseWatch(const std::vector<std::string>& words, const std::string& baseDir) {
    if (words.size() < 2) {
        throw std::runtime_error("usage: watch <dir> [recursive=yes|no] [events=...] [exclude=...]");
    }
    WatchRule rule;
    rule.path = canonicalDir(words[1], baseDir);
    rule.mask = allEvents();
    for (size_t i = 2; i < words.size(); ++i) {
        const std::string& option = words[i];
        size_t eq = option.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("expected key=value, got '" + option + "'");
        }
        std::string key = option.substr(0, eq);
        std::string value = option.substr(eq + 1);
        if (key == "recursive") {
            rule.recursive = parseBool(value);
        } else if (key == "events") {
            rule.mask = parseEvents(value);
        } else if (key == "exclude") {
            rule.exclude = splitList(value);
        } else {
            throw std::runtime_error("unknown option '" + key + "'");
        }
    }
    return rule;
}

// inotify_add_watch() on an already watched directory replaces its mask,
// so rules must not share directories.
void checkOverlaps(const std::vector<WatchRule>& rules) {
    for (size_t i = 0; i < rules.size(); ++i) {
        for (size_t j = 0; j < rules.size(); ++j) {
            if (i == j) continue;
            const auto& a = rules[i];
            const auto& b = rules[j];
            if (a.path == b.path && i < j) {
                throw std::runtime_error("directory is listed twice: " + a.path);
            }
            if (a.path != b.path && a.recursive && isInside(b.path, a.path)) {
                throw std::runtime_error(b.path + " is already covered by recursive " + a.path);
            }
        }
    }
}

}  // namespace

bool WatchRule::operator==(const WatchRule& other) const {
    return path == other.path && recursive == other.recursive && mask == other.mask &&
           exclude == other.exclude;
}

Config loadConfig(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr) {
        throw std::runtime_error(path + ": " + std::strerror(errno));
    }
    std::string file = resolved;
    std::string baseDir = file.substr(0, file.rfind('/'));
    if (baseDir.empty()) baseDir = "/";

    std::ifstream in(file);
    if (!in) {
        throw std::runtime_error(file + ": " + std::strerror(errno));
    }

    Config config;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        try {
            auto words = tokenize(line);
            if (words.empty()) continue;
            if (words[0] == "watch") {
                config.rules.push_back(parseWatch(words, baseDir));
            } else if (words[0] == "debounce_ms") {
                if (words.size() != 2) throw std::runtime_error("usage: debounce_ms <N>");
                char* end = nullptr;
                long value = std::strtol(words[1].c_str(), &end, 10);
                if (*end != '\0' || value < 0 || value > 60000) {
                    throw std::runtime_error("debounce_ms must be 0..60000");
                }
                config.debounceMs = static_cast<int>(value);
            } else {
                throw std::runtime_error("unknown directive '" + words[0] + "'");
            }
        } catch (const std::runtime_error& e) {
            throw std::runtime_error(file + ":" + std::to_string(lineNo) + ": " + e.what());
        }
    }

    if (config.rules.empty()) {
        throw std::runtime_error(file + ": no 'watch' directives");
    }
    try {
        checkOverlaps(config.rules);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(file + ": " + e.what());
    }
    return config;
}

std::string eventNames(uint32_t mask) {
    if ((mask & allEvents()) == allEvents()) return "all";
    std::string names;
    for (const auto& e : kEvents) {
        if (mask & e.mask) {
            if (!names.empty()) names += ',';
            names += e.name;
        }
    }
    return names;
}

std::string describe(const WatchRule& rule) {
    std::string text = rule.path;
    text += rule.recursive ? " recursive=yes" : " recursive=no";
    text += " events=" + eventNames(rule.mask);
    if (!rule.exclude.empty()) {
        text += " exclude=";
        for (size_t i = 0; i < rule.exclude.size(); ++i) {
            if (i) text += ',';
            text += rule.exclude[i];
        }
    }
    return text;
}
