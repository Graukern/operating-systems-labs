#include "config.hpp"

#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

bool within(const fs::path& child, const fs::path& parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c) {
        if (c == child.end() || *c != *p) return false;
    }
    return true;
}
}

Config Config::load(const fs::path& filename) {
    std::ifstream input(filename);
    if (!input) throw std::runtime_error("Cannot open configuration: " + filename.string());
    Config result;
    const auto base = fs::absolute(filename).parent_path();
    auto resolve = [&](const std::string& value) {
        const fs::path path(value);
        return fs::weakly_canonical(path.is_absolute() ? path : base / path);
    };
    std::set<std::string> keys;
    std::set<fs::path> watches;
    std::string line;
    unsigned number = 0;
    while (std::getline(input, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        auto fail = [&](const std::string& reason) {
            throw std::runtime_error("Configuration line " + std::to_string(number) + ": " + reason);
        };
        const auto separator = line.find('=');
        if (separator == std::string::npos) fail("expected key=value");
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (value.empty()) fail("empty value");
        if (value.find('\0') != std::string::npos) fail("NUL is not allowed in paths");
        if (key != "watch" && key != "event_log_dir" && key != "pid_file") fail("unknown key " + key);
        if (key != "watch" && !keys.insert(key).second) fail("duplicate key " + key);
        const fs::path raw_path = fs::path(value).is_absolute() ? fs::path(value) : base / value;
        if (key == "pid_file" && fs::is_symlink(raw_path)) fail("pid_file must not be a symlink");
        const auto path = resolve(value);
        if (key == "watch") {
            if (!fs::is_directory(path)) fail("watch is not an existing directory: " + path.string());
            if (!watches.insert(path).second) fail("duplicate watch directory");
            result.directories.push_back(path);
        } else if (key == "event_log_dir") {
            result.event_log_dir = path;
        } else {
            result.pid_file = path;
        }
    }
    if (input.bad()) throw std::runtime_error("Error reading configuration");
    if (result.directories.empty()) throw std::runtime_error("At least one watch is required");
    if (result.event_log_dir.empty() || !fs::is_directory(result.event_log_dir))
        throw std::runtime_error("event_log_dir must be an existing directory");
    if (result.pid_file.empty() || !fs::is_directory(result.pid_file.parent_path()))
        throw std::runtime_error("pid_file must have an existing parent directory");
    if (fs::is_directory(result.pid_file)) throw std::runtime_error("pid_file is a directory");
    for (const auto& directory : result.directories) {
        if (within(result.event_log_dir, directory) || within(result.pid_file, directory))
            throw std::runtime_error("Logs and PID file must be outside watched directories");
        if (within(fs::weakly_canonical("/var/log/disk-monitor.log"), directory))
            throw std::runtime_error("The supplied rsyslog output must be outside watched directories");
    }
    return result;
}

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c == 127)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
