#include "Config.hpp"
#include "FileSorter.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Каждый тест получает собственную папку в /tmp. После выполнения теста
// все созданные файлы автоматически удаляются (даже при исключении).
class TempDir {
public:
    TempDir() {
        std::string pattern =
            (fs::temp_directory_path() / "os_lab1_config_XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const char* created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary test directory");
        }
        path_ = created;
    }

    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// Восстанавливаем рабочую директорию после теста, даже если тест упадёт.
class RestoreWorkingDirectory {
public:
    RestoreWorkingDirectory() : original_(fs::current_path()) {}
    ~RestoreWorkingDirectory() {
        std::error_code ignored;
        fs::current_path(original_, ignored);
    }

    RestoreWorkingDirectory(const RestoreWorkingDirectory&) = delete;
    RestoreWorkingDirectory& operator=(const RestoreWorkingDirectory&) = delete;

private:
    fs::path original_;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out || !(out << text)) {
        throw std::runtime_error("Cannot write test file: " + path.string());
    }
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read test file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

// Проверяем именно ожидаемый отказ с диагностическим сообщением.
void expectLoadFailure(const fs::path& configPath,
                       const std::string& expectedText) {
    Config config(configPath);
    try {
        config.load();
    } catch (const std::exception& error) {
        require(std::string(error.what()).find(expectedText) != std::string::npos,
                "Unexpected error text: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Invalid configuration was accepted");
}

// Тест корректного конфига с двумя правилами.
// Условия: интервал, комментарии, пустые строки и пути относительно файла.
// Ожидаем: оба правила загрузились в исходном порядке, interval равен 60,
// а пути превратились в абсолютные, пригодные после смены cwd.
void testValidConfig() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "srcA");
    fs::create_directories(tmp.path() / "srcB");
    const fs::path filename = tmp.path() / "config.txt";
    writeFile(filename,
              "# Комментарий\n\n interval = 60 \n"
              "srcA resultA .png IMAGES\n"
              "  srcB resultB .pdf DOCUMENTS  \n");

    Config config(filename);
    config.load();

    require(config.intervalSeconds() == 60, "Interval was parsed incorrectly");
    require(config.rules().size() == 2, "Expected two rules");
    require(config.rules()[0].source == fs::canonical(tmp.path() / "srcA"),
            "Relative source path was not resolved from the config directory");
    require(config.rules()[0].destination == tmp.path() / "resultA",
            "Destination path was not made absolute");
    require(config.rules()[0].extension == ".png" &&
                config.rules()[0].subfolder == "IMAGES",
            "First rule fields are wrong");
    require(config.rules()[1].extension == ".pdf" &&
                config.rules()[1].subfolder == "DOCUMENTS",
            "Second rule fields/order are wrong");
}

// Тест сохранения абсолютного пути к конфигурации.
// Условия: объект Config создан по относительному имени, затем cwd меняется.
// Ожидаем: load() и повторный load() читают прежний файл, а относительные
// пути в правилах по-прежнему вычисляются относительно папки конфига.
void testAbsoluteConfigPathAfterChdir() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src");
    writeFile(tmp.path() / "config.txt",
              "interval=60\nsrc dest .png IMAGES\n");

    RestoreWorkingDirectory restore;
    fs::current_path(tmp.path());
    Config config("config.txt");
    require(config.path() == tmp.path() / "config.txt",
            "Config must remember the absolute path");

    fs::current_path(fs::temp_directory_path());
    config.load();
    require(config.rules().at(0).source == fs::canonical(tmp.path() / "src"),
            "Config resolved source relative to current cwd");
    writeFile(tmp.path() / "config.txt",
              "interval=12\nsrc dest .txt TEXT\n");
    config.load();
    require(config.intervalSeconds() == 12 &&
                config.rules().at(0).extension == ".txt",
            "Reload after chdir did not read the original file");
}

// Тест безопасной перезагрузки конфигурации.
// Условия: сначала загружаем корректную конфигурацию, затем портим её.
// Ожидаем: load() сообщает об ошибке и сохраняет старые настройки;
// после исправления следующий load() успешно применяет новые настройки.
void testReloadRollback() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src");
    const fs::path file = tmp.path() / "config.txt";
    writeFile(file, "interval=60\nsrc dest .png IMAGES\n");
    Config config(file);
    config.load();

    writeFile(file, "interval=5\nsrc dest .png IMAGES\nsrc dest2 wrong TEXT\n");
    bool failed = false;
    try {
        config.load();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid reload was accepted");
    require(config.intervalSeconds() == 60, "Interval changed after failed reload");
    require(config.rules().size() == 1 &&
                config.rules()[0].extension == ".png",
            "Rules changed after failed reload");

    writeFile(file, "interval=30\nsrc dest2 .txt TEXT\n");
    config.load();
    require(config.intervalSeconds() == 30, "Corrected interval was not loaded");
    require(config.rules().size() == 1 &&
                config.rules()[0].extension == ".txt",
            "Corrected rule was not loaded");
}

// Тест ошибок интервала.
// Условия: нет интервала, интервал повторён, равен нулю, отрицателен,
// не является целым числом или не помещается в тип int.
// Ожидаем: каждая такая конфигурация отклоняется до применения правил.
void testInvalidIntervals() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src");
    const fs::path file = tmp.path() / "config.txt";
    const std::string rule = "src dest .png IMAGES\n";
    const std::vector<std::pair<std::string, std::string>> cases = {
        {rule, "Missing interval"},
        {"interval=0\n" + rule, "positive integer"},
        {"interval=-5\n" + rule, "positive integer"},
        {"interval=abc\n" + rule, "positive integer"},
        {"interval=12s\n" + rule, "positive integer"},
        {"interval=99999999999999\n" + rule, "positive integer"},
        {"interval=60\ninterval=30\n" + rule, "Duplicate interval"},
    };
    for (const auto& testCase : cases) {
        writeFile(file, testCase.first);
        expectLoadFailure(file, testCase.second);
    }
}

// Тест количества полей и отсутствия правил.
// Условия: правило с тремя/пятью полями и конфиг только с интервалом.
// Ожидаем: файл отклоняется, а ошибка указывает на неверный формат
// или отсутствие необходимых правил сортировки.
void testInvalidRuleFieldCount() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src");
    const fs::path file = tmp.path() / "config.txt";
    writeFile(file, "interval=60\nsrc dest .png\n");
    expectLoadFailure(file, "exactly four fields");
    writeFile(file, "interval=60\nsrc dest .png IMAGES EXTRA\n");
    expectLoadFailure(file, "exactly four fields");
    writeFile(file, "# only comments\ninterval=60\n");
    expectLoadFailure(file, "no sorting rules");
}

// Тест неверных имён групп и расширений.
// Условия: расширение без точки, путь вместо имени подпапки и имя OTHERS.
// Ожидаем: Config не принимает значения, которые позже отверг бы FileSorter.
void testInvalidRuleValues() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src");
    const fs::path file = tmp.path() / "config.txt";
    writeFile(file, "interval=60\nsrc dest png IMAGES\n");
    expectLoadFailure(file, "Extension");
    writeFile(file, "interval=60\nsrc dest .png ../escape\n");
    expectLoadFailure(file, "Subfolder");
    writeFile(file, "interval=60\nsrc dest .png OTHERS\n");
    expectLoadFailure(file, "Subfolder");
}

// Тест несуществующих/неподходящих путей.
// Условия: отсутствует конфигурационный файл или source, а destination
// является обычным файлом. Ожидаем: Config выдаёт ошибку и не удаляет данные.
void testInvalidFileSystemPaths() {
    TempDir tmp;
    const fs::path file = tmp.path() / "config.txt";
    expectLoadFailure(file, "Cannot open configuration");
    writeFile(file, "interval=60\nmissing dest .png IMAGES\n");
    expectLoadFailure(file, "Source directory does not exist");

    fs::create_directories(tmp.path() / "src");
    writeFile(tmp.path() / "already-file", "protected");
    writeFile(file, "interval=60\nsrc already-file .png IMAGES\n");
    expectLoadFailure(file, "Destination is not a directory");
    require(readFile(tmp.path() / "already-file") == "protected",
            "Validation damaged existing file");
}

// Тест конфликтующих целевых каталогов.
// Условия: два правила используют один destination или вложенные назначения.
// Ожидаем: оба варианта запрещаются, чтобы второе правило не удаляло
// результат первого при очистке destination.
void testDestinationConflicts() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src1");
    fs::create_directories(tmp.path() / "src2");
    const fs::path file = tmp.path() / "config.txt";
    writeFile(file, "interval=60\nsrc1 result .png IMAGES\n"
                    "src2 result .txt TEXT\n");
    expectLoadFailure(file, "destination directories overlap");

    writeFile(file, "interval=60\nsrc1 result .png IMAGES\n"
                    "src2 result/nested .txt TEXT\n");
    expectLoadFailure(file, "destination directories overlap");
}

// Тест опасного пересечения source и destination между правилами.
// Условия: один destination попадает внутрь источника другого правила
// либо является исходным каталогом другого правила.
// Ожидаем: конфликт отклоняется для защиты исходных файлов от удаления.
void testCrossRuleSourceConflicts() {
    TempDir tmp;
    const fs::path file = tmp.path() / "config.txt";
    fs::create_directories(tmp.path() / "src1");
    fs::create_directories(tmp.path() / "src2");
    fs::create_directories(tmp.path() / "shared");

    writeFile(file, "interval=60\nsrc1 shared .png IMAGES\n"
                    "shared result2 .txt TEXT\n");
    expectLoadFailure(file, "destination overlaps a source");

    writeFile(file, "interval=60\nsrc1 result1 .png IMAGES\n"
                    "src2 src1/nested .txt TEXT\n");
    expectLoadFailure(file, "destination overlaps a source");
}

// Тест защиты самого конфигурационного файла.
// Условия: config.txt расположен внутри каталога, который правило очистит.
// Ожидаем: загрузка запрещена, чтобы следующий цикл не удалил свой конфиг.
void testConfigIsNotInsideDestination() {
    TempDir tmp;
    fs::create_directories(tmp.path() / "src");
    const fs::path file = tmp.path() / "result" / "config.txt";
    writeFile(file, "interval=60\n../src . .png IMAGES\n");

    expectLoadFailure(file, "Configuration file must not lie inside");
    require(fs::exists(file), "Config file disappeared during validation");
}

// Интеграционный тест Config + FileSorter.
// Условия: один source, два разных destination и два правила в конфиге.
// Ожидаем: Config разрешает общий source, а FileSorter последовательно
// создаёт оба независимых результата без взаимного удаления файлов.
void testConfigAndSorterTogether() {
    TempDir tmp;
    writeFile(tmp.path() / "src" / "nested" / "photo.png", "image-data");
    writeFile(tmp.path() / "src" / "notes.txt", "text-data");
    const fs::path file = tmp.path() / "config.txt";
    writeFile(file, "interval=60\nsrc images .png IMAGES\n"
                    "src docs .txt TEXT\n");

    Config config(file);
    config.load();
    for (const SortRule& rule : config.rules()) {
        FileSorter::processRule(rule);
    }

    require(readFile(tmp.path() / "images" / "IMAGES" / "nested" /
                     "photo.png") == "image-data",
            "First rule did not preserve nested image");
    require(readFile(tmp.path() / "docs" / "TEXT" / "notes.txt") == "text-data",
            "Second rule did not sort TXT");
    require(readFile(tmp.path() / "images" / "OTHERS" / "notes.txt") ==
                "text-data",
            "First rule's OTHERS contents were unexpectedly removed");
}

using Test = std::pair<const char*, std::function<void()>>;

}  // namespace

int main() {
    const std::vector<Test> tests = {
        {"valid configuration with two rules", testValidConfig},
        {"absolute path survives chdir", testAbsoluteConfigPathAfterChdir},
        {"failed reload preserves previous config", testReloadRollback},
        {"invalid intervals are rejected", testInvalidIntervals},
        {"invalid rule field counts are rejected", testInvalidRuleFieldCount},
        {"invalid rule values are rejected", testInvalidRuleValues},
        {"missing files and bad paths are rejected", testInvalidFileSystemPaths},
        {"overlapping destinations are rejected", testDestinationConflicts},
        {"destinations overlapping sources are rejected", testCrossRuleSourceConflicts},
        {"config file cannot be inside destination", testConfigIsNotInsideDestination},
        {"Config and FileSorter work together", testConfigAndSorterTogether},
    };

    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": " << error.what()
                      << '\n';
        }
    }
    std::cout << (tests.size() - static_cast<std::size_t>(failed)) << '/'
              << tests.size() << " tests passed\n";
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
