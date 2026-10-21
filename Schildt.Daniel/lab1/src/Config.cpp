#include "Config.h"

#include <cstdlib>
#include <fstream>

static std::string trim(const std::string& s)
{
    const char* ws = " \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return "";
    }
    std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

bool loadConfig(const std::string& path, Config& cfg, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }

    Config result;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(lineNo) + ": expected key=value";
            return false;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "dir1") {
            result.dir1 = value;
        } else if (key == "dir2") {
            result.dir2 = value;
        } else if (key == "interval") {
            char* end = nullptr;
            long v = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0' || v <= 0 || v > 86400) {
                error = "line " + std::to_string(lineNo) + ": bad interval";
                return false;
            }
            result.interval = static_cast<int>(v);
        } else {
            error = "line " + std::to_string(lineNo) + ": unknown key '" + key + "'";
            return false;
        }
    }

    if (result.dir1.empty() || result.dir2.empty()) {
        error = "dir1 and dir2 are required";
        return false;
    }
    cfg = result;
    return true;
}