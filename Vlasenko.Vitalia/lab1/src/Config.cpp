#include "Config.hpp"

#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

bool isInsideOrSame(const fs::path& child, const fs::path& parent) {
    auto current = child.begin();
    for (auto part = parent.begin(); part != parent.end(); ++part) {
        if (current == child.end() || *current != *part) {
            return false;
        }
        ++current;
    }
    return true;
}

bool pathsOverlap(const fs::path& a, const fs::path& b) {
    return isInsideOrSame(a, b) || isInsideOrSame(b, a);
}

// Относительные пути в правилах интерпретируются относительно папки конфига,
// поэтому смена рабочего каталога демоном не изменит их смысл.
fs::path resolvePath(const fs::path& path, const fs::path& configDirectory) {
    if (path.empty()) {
        throw std::runtime_error("Empty directory path");
    }
    return (path.is_absolute() ? path : configDirectory / path)
        .lexically_normal();
}

int parseInterval(const std::string& text) {
    if (text.empty()) {
        throw std::runtime_error("Interval is empty");
    }

    int seconds = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != end || seconds <= 0) {
        throw std::runtime_error("Interval must be a positive integer in seconds");
    }
    return seconds;
}

SortRule parseRule(const std::string& line, const fs::path& configDirectory) {
    std::istringstream input(line);
    std::string source;
    std::string destination;
    std::string extension;
    std::string subfolder;
    std::string extra;

    if (!(input >> source >> destination >> extension >> subfolder) ||
        (input >> extra)) {
        throw std::runtime_error(
            "Rule must contain exactly four fields: folder1 folder2 ext subfolder");
    }

    if (extension.size() < 2 || extension.front() != '.') {
        throw std::runtime_error("Extension must look like .png or .txt");
    }

    const fs::path subfolderPath(subfolder);
    if (subfolderPath.empty() || subfolderPath == "." ||
        subfolderPath == ".." || subfolderPath.is_absolute() ||
        subfolderPath.has_parent_path() || subfolderPath == "OTHERS") {
        throw std::runtime_error(
            "Subfolder must be one name different from OTHERS");
    }

    const fs::path srcPath = resolvePath(source, configDirectory);
    const fs::path dstPath = resolvePath(destination, configDirectory);
    if (fs::is_symlink(srcPath) || fs::is_symlink(dstPath)) {
        throw std::runtime_error("Source and destination must not be symlinks");
    }
    if (!fs::is_directory(srcPath)) {
        throw std::runtime_error("Source directory does not exist: " +
                                 srcPath.string());
    }
    if (fs::exists(dstPath) && !fs::is_directory(dstPath)) {
        throw std::runtime_error("Destination is not a directory: " +
                                 dstPath.string());
    }

    const fs::path normalizedSource = fs::canonical(srcPath);
    const fs::path normalizedDestination = fs::weakly_canonical(dstPath);
    if (normalizedDestination == normalizedDestination.root_path() ||
        pathsOverlap(normalizedSource, normalizedDestination)) {
        throw std::runtime_error(
            "Unsafe paths: source and destination must not overlap or be /");
    }

    return {normalizedSource, normalizedDestination, extension, subfolder};
}

// Для всех правил запрещаем пересечение любого destination с:
// 1) другими destination (иначе следующее правило удалит предыдущий результат);
// 2) любым source (иначе можно стереть исходные файлы другого правила).
// Пересечение двух source допустимо: они только читаются.
void validateRuleConflicts(const std::vector<SortRule>& rules) {
    for (std::size_t i = 0; i < rules.size(); ++i) {
        for (std::size_t j = 0; j < rules.size(); ++j) {
            if (pathsOverlap(rules[i].destination, rules[j].source)) {
                throw std::runtime_error(
                    "Rule conflict: destination overlaps a source directory "
                    "(rules " + std::to_string(i + 1) + " and " +
                    std::to_string(j + 1) + ")");
            }
        }
        for (std::size_t j = i + 1; j < rules.size(); ++j) {
            if (pathsOverlap(rules[i].destination, rules[j].destination)) {
                throw std::runtime_error(
                    "Rule conflict: destination directories overlap "
                    "(rules " + std::to_string(i + 1) + " and " +
                    std::to_string(j + 1) + ")");
            }
        }
    }
}

}  // namespace

Config::Config(const fs::path& filename) {
    if (filename.empty()) {
        throw std::runtime_error("Configuration file path is empty");
    }
    // Сохраняем абсолютный путь до возможного chdir("/") при демонизации.
    path_ = fs::absolute(filename).lexically_normal();
}

void Config::load() {
    std::ifstream file(path_);
    if (!file) {
        throw std::runtime_error("Cannot open configuration file: " +
                                 path_.string());
    }

    // Сначала полностью собираем новые настройки в локальных переменных.
    std::optional<int> newInterval;
    std::vector<SortRule> newRules;
    std::string rawLine;
    std::size_t lineNumber = 0;

    while (std::getline(file, rawLine)) {
        ++lineNumber;
        const std::string line = trim(rawLine);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        try {
            const std::size_t equals = line.find('=');
            if (equals != std::string::npos &&
                trim(line.substr(0, equals)) == "interval") {
                if (newInterval) {
                    throw std::runtime_error("Duplicate interval setting");
                }
                newInterval = parseInterval(trim(line.substr(equals + 1)));
            } else {
                newRules.push_back(parseRule(line, path_.parent_path()));
            }
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Configuration line " + std::to_string(lineNumber) + ": " +
                error.what());
        }
    }
    if (file.bad()) {
        throw std::runtime_error("Cannot read configuration file: " +
                                 path_.string());
    }
    if (!newInterval) {
        throw std::runtime_error("Missing interval setting in configuration");
    }
    if (newRules.empty()) {
        throw std::runtime_error("Configuration has no sorting rules");
    }
    validateRuleConflicts(newRules);

    // Не допускаем удаления самого конфига во время очистки destination.
    const fs::path canonicalConfigPath = fs::canonical(path_);
    for (const SortRule& rule : newRules) {
        if (isInsideOrSame(canonicalConfigPath, rule.destination)) {
            throw std::runtime_error(
                "Configuration file must not lie inside a destination directory");
        }
    }

    // Успешная проверка ВСЕХ правил: только теперь обновляем действующие.
    // Вектор заменяем через swap, чтобы не потерять старые правила при ошибке.
    rules_.swap(newRules);
    intervalSeconds_ = *newInterval;
}
