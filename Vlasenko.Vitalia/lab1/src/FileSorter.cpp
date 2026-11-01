#include "FileSorter.hpp"

#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
// Проверяет, совпадает ли путь с родительским или находится внутри него.
bool isInsideOrSame(const fs::path& child, const fs::path& parent) {
    auto childIt = child.begin();
    for (auto parentIt = parent.begin(); parentIt != parent.end(); ++parentIt) {
        if (childIt == child.end() || *childIt != *parentIt) {
            return false;
        }
        ++childIt;
    }
    return true;
}
}  // namespace

void FileSorter::validateRule(const SortRule& rule) {
    if (!fs::exists(rule.source) || !fs::is_directory(rule.source)) {
        throw std::runtime_error("Source directory does not exist: " +
                                 rule.source.string());
    }
    if (fs::is_symlink(rule.source) || fs::is_symlink(rule.destination)) {
        throw std::runtime_error("Source and destination must not be symlinks");
    }
    if (fs::exists(rule.destination) && !fs::is_directory(rule.destination)) {
        throw std::runtime_error("Destination is not a directory: " +
                                 rule.destination.string());
    }
    if (rule.extension.size() < 2 || rule.extension.front() != '.') {
        throw std::runtime_error("Extension must look like .png or .txt");
    }

    const fs::path subfolder(rule.subfolder);
    if (subfolder.empty() || subfolder == "." || subfolder == ".." ||
        subfolder.is_absolute() || subfolder.has_parent_path() ||
        subfolder == "OTHERS") {
        throw std::runtime_error(
            "Subfolder must be a single directory name other than OTHERS");
    }

    const fs::path source = fs::canonical(rule.source);
    const fs::path destination = fs::weakly_canonical(rule.destination);
    if (destination == destination.root_path() ||
        isInsideOrSame(destination, source) ||
        isInsideOrSame(source, destination)) {
        throw std::runtime_error(
            "Unsafe paths: destination must not overlap source or be /");
    }
}

std::vector<fs::path> FileSorter::collectFiles(const fs::path& source) {
    std::vector<fs::path> files;

    // По умолчанию рекурсивный итератор не переходит по ссылкам на каталоги.
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_symlink()) {
            continue;  // Пропускаем также ссылки на обычные файлы.
        }
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    return files;
}

void FileSorter::clearDestination(const fs::path& destination) {
    std::vector<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(destination)) {
        entries.push_back(entry.path());
    }

    // Сохраняем сам каталог назначения, удаляем только его содержимое.
    for (const auto& entry : entries) {
        fs::remove_all(entry);
    }
}

fs::path FileSorter::getDestinationPath(const SortRule& rule,
                                        const fs::path& sourceFile) {
    const fs::path relativePath = sourceFile.lexically_relative(rule.source);
    const fs::path group = (sourceFile.extension() == rule.extension)
                               ? fs::path(rule.subfolder)
                               : fs::path("OTHERS");
    return rule.destination / group / relativePath;
}

void FileSorter::copyFiles(const SortRule& rule,
                           const std::vector<fs::path>& files) {
    for (const auto& sourceFile : files) {
        const fs::path target = getDestinationPath(rule, sourceFile);
        fs::create_directories(target.parent_path());
        fs::copy_file(sourceFile, target);
    }
}

void FileSorter::processRule(const SortRule& rule) {
    validateRule(rule);

    // Нормализуем абсолютные пути перед расчётом относительных путей.
    SortRule normalized = rule;
    normalized.source = fs::canonical(rule.source);
    normalized.destination = fs::weakly_canonical(rule.destination);

    // Сначала обходим источник, затем очищаем каталог назначения.
    const auto files = collectFiles(normalized.source);

    fs::create_directories(normalized.destination);
    clearDestination(normalized.destination);
    fs::create_directories(normalized.destination / normalized.subfolder);
    fs::create_directories(normalized.destination / "OTHERS");
    copyFiles(normalized, files);
}
