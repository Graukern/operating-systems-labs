#pragma once

#include "SortRule.hpp"

#include <filesystem>
#include <vector>

class FileSorter {
public:
    static void processRule(const SortRule& rule);

private:
    static void validateRule(const SortRule& rule);
    static std::vector<std::filesystem::path> collectFiles(
        const std::filesystem::path& source);
    static void clearDestination(const std::filesystem::path& destination);
    static std::filesystem::path getDestinationPath(
        const SortRule& rule, const std::filesystem::path& sourceFile);
    static void copyFiles(
        const SortRule& rule,
        const std::vector<std::filesystem::path>& files);
};
