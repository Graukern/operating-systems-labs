#include "config.hpp"

#include <cerrno>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace diskmon {

void systemError(const std::string& operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

bool contains(const fs::path& directory, const fs::path& path) {
    auto child = path.begin();
    for (const auto& part : directory) {
        if (child == path.end() || *child++ != part) {
            return false;
        }
    }
    return true;
}

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char character : value) {
        if (character == '\\' || character == '"') {
            out << '\\' << character;
        } else if (character < 32 || character == 127) {
            out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned int>(character) << std::dec;
        } else {
            out << character;
        }
    }
    out << '"';
    return out.str();
}

std::vector<fs::path> loadConfig(const fs::path& file) {
    std::ifstream input(file);
    if (!input) {
        throw std::runtime_error("Cannot open configuration: " + file.string());
    }
    std::vector<fs::path> directories;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r");
        fs::path path(line.substr(first, last - first + 1));
        if (path.is_relative()) {
            path = file.parent_path() / path;
        }
        std::error_code error;
        path = fs::canonical(path, error);
        if (error || !fs::is_directory(path)) {
            throw std::runtime_error("Invalid directory at line " +
                                     std::to_string(lineNumber));
        }
        // An ancestor watch already includes its descendants. Keep only roots.
        bool covered = false;
        for (const auto& directory : directories) {
            covered = covered || contains(directory, path);
        }
        if (!covered) {
            auto it = directories.begin();
            while (it != directories.end()) {
                it = contains(path, *it) ? directories.erase(it) : std::next(it);
            }
            directories.push_back(path);
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Cannot read configuration: " + file.string());
    }
    if (directories.empty()) {
        throw std::runtime_error("Configuration must contain at least one directory");
    }
    return directories;
}

} // namespace diskmon
