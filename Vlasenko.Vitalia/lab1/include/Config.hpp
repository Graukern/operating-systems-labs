#pragma once

#include "SortRule.hpp"

#include <filesystem>
#include <vector>

// Хранит последние корректно загруженные настройки демона.
// При ошибке повторного чтения старые настройки сохраняются.
class Config {
public:
    explicit Config(const std::filesystem::path& filename);

    // Считывает настройки из сохранённого абсолютного пути.
    // При любой ошибке бросает исключение и не меняет действующие настройки.
    void load();

    int intervalSeconds() const noexcept { return intervalSeconds_; }
    const std::vector<SortRule>& rules() const noexcept { return rules_; }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    int intervalSeconds_ = 0;
    std::vector<SortRule> rules_;
};
