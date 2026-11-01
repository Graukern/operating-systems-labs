#include "config.hpp"

#include <algorithm>
#include <fstream>

namespace config {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

Result parse(std::istream& in) {
    Result r;
    std::string raw;
    int n = 0;
    while (std::getline(in, raw)) {
        ++n;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;

        size_t sp = line.find_first_of(" \t");
        std::string key = (sp == std::string::npos) ? line : line.substr(0, sp);
        std::string val = (sp == std::string::npos) ? "" : trim(line.substr(sp));
        std::string where = "line " + std::to_string(n) + ": ";

        if (key != "dir") {
            r.warnings.push_back(where + "unknown directive '" + key + "'");
            continue;
        }
        if (val.empty()) {
            r.warnings.push_back(where + "dir without path");
            continue;
        }
        if (val[0] != '/') {
            // после chdir("/") относительные пути теряют смысл
            r.warnings.push_back(where + "path must be absolute: " + val);
            continue;
        }
        while (val.size() > 1 && val.back() == '/') val.pop_back();
        if (std::find(r.dirs.begin(), r.dirs.end(), val) != r.dirs.end()) {
            r.warnings.push_back(where + "duplicate dir " + val);
            continue;
        }
        r.dirs.push_back(val);
    }
    return r;
}

bool loadFile(const std::string& path, Result& out) {
    std::ifstream in(path);
    if (!in) return false;
    out = parse(in);
    return true;
}

}