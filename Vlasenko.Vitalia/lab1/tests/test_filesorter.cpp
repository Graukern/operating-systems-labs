#include "FileSorter.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Каждый тест использует собственный временный каталог в /tmp.
// После теста каталог автоматически удаляется.
class TempDir {
public:
    TempDir() {
        std::string pattern =
            (fs::temp_directory_path() / "os_lab1_test_XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char* created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary test directory");
        }
        path_ = created;
    }

    ~TempDir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file || !(file << contents)) {
        throw std::runtime_error("Cannot write: " + path.string());
    }
}

std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot read: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

// Проверяет, что FileSorter отклоняет некорректное правило исключением.
void expectRejected(const SortRule& rule) {
    bool rejected = false;
    try {
        FileSorter::processRule(rule);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid rule was accepted");
}

// Тест рекурсивной сортировки файлов.
// Ожидаем: файлы с расширением .png попадают в IMAGES, остальные — в OTHERS;
// относительные пути вложенных файлов сохраняются, содержимое файлов не меняется.
// Кроме того, старые файлы назначения удаляются, а исходные остаются нетронутыми.
// Проверяем также, что сравнение расширений учитывает регистр (.PNG != .png).
void testRecursiveSorting() {
    TempDir tmp;
    const fs::path source = tmp.path() / "source";
    const fs::path dest = tmp.path() / "destination";

    writeFile(source / "photo.png", "root-png");
    writeFile(source / "animals" / "photo.png", "animal-png");
    writeFile(source / "docs" / "report.pdf", "pdf-data");
    writeFile(source / "upper.PNG", "uppercase-extension");
    writeFile(dest / "obsolete.txt", "must-be-removed");

    FileSorter::processRule({source, dest, ".png", "IMAGES"});

    require(readFile(dest / "IMAGES" / "photo.png") == "root-png",
            "Root PNG should go to IMAGES");
    require(readFile(dest / "IMAGES" / "animals" / "photo.png") == "animal-png",
            "Nested PNG should retain its relative path");
    require(readFile(dest / "OTHERS" / "docs" / "report.pdf") == "pdf-data",
            "Other extensions should go to OTHERS");
    require(readFile(dest / "OTHERS" / "upper.PNG") == "uppercase-extension",
            "Extension matching is case-sensitive");
    require(!fs::exists(dest / "obsolete.txt"),
            "Destination must be cleared before copying");
    require(readFile(source / "animals" / "photo.png") == "animal-png",
            "Source file should stay unchanged");
}

// Тест повторного запуска сортировки.
// Сначала сортируем один набор файлов, затем меняем содержимое источника.
// Ожидаем: при втором запуске старый результат исчезает,
// а новые файлы появляются в соответствующих подпапках.
void testSecondRunRefreshesDestination() {
    TempDir tmp;
    const fs::path source = tmp.path() / "source";
    const fs::path dest = tmp.path() / "destination";
    writeFile(source / "first.png", "old-content");
    FileSorter::processRule({source, dest, ".png", "IMAGES"});

    fs::remove(source / "first.png");
    writeFile(source / "new.txt", "new-content");
    FileSorter::processRule({source, dest, ".png", "IMAGES"});

    require(!fs::exists(dest / "IMAGES" / "first.png"),
            "Removed source file must disappear after next run");
    require(readFile(dest / "OTHERS" / "new.txt") == "new-content",
            "New source file should appear after next run");
}

// Тест независимого выполнения двух правил с РАЗНЫМИ целевыми каталогами.
// Ожидаем: каждое правило помещает файлы в собственный destination,
// а обработка второго правила не удаляет результат первого.
// Совпадающие целевые каталоги здесь НЕ проверяются — их запретим в Config.
void testIndependentRules() {
    TempDir tmp;
    const fs::path src1 = tmp.path() / "src1";
    const fs::path dst1 = tmp.path() / "dst1";
    const fs::path src2 = tmp.path() / "src2";
    const fs::path dst2 = tmp.path() / "dst2";
    writeFile(src1 / "picture.png", "png-one");
    writeFile(src2 / "report.txt", "txt-two");

    FileSorter::processRule({src1, dst1, ".png", "PICTURES"});
    FileSorter::processRule({src2, dst2, ".txt", "TEXT"});

    require(readFile(dst1 / "PICTURES" / "picture.png") == "png-one",
            "First rule output is wrong");
    require(readFile(dst2 / "TEXT" / "report.txt") == "txt-two",
            "Second rule output is wrong");
    require(fs::exists(dst1 / "PICTURES" / "picture.png"),
            "Second rule must not erase first rule's destination");
}

// Тест валидации опасных и некорректных правил.
// Ожидаем: совпадающие/вложенные пути, корень '/' в качестве назначения,
// неверное расширение и небезопасное имя подпапки вызывают исключение.
// Ни исходные, ни уже существующие целевые данные не должны пострадать.
void testInvalidRulesDoNotEraseData() {
    TempDir tmp;
    const fs::path source = tmp.path() / "source";
    const fs::path dest = tmp.path() / "destination";
    writeFile(source / "keep.txt", "source-protected");
    writeFile(dest / "keep.txt", "destination-protected");

    expectRejected({source, source, ".txt", "TEXT"});
    expectRejected({source, source / "inside", ".txt", "TEXT"});
    expectRejected({source, dest, "txt", "TEXT"});
    expectRejected({source, dest, ".txt", "../escape"});
    expectRejected({source, fs::path("/"), ".txt", "TEXT"});

    require(readFile(source / "keep.txt") == "source-protected",
            "Invalid rule modified the source");
    require(readFile(dest / "keep.txt") == "destination-protected",
            "Invalid rule erased the destination");
}

// Тест обработки пустого исходного каталога.
// Ожидаем: старые файлы в destination удаляются даже при пустом source;
// при этом создаются обе папки для сортировки — IMAGES и OTHERS.
void testEmptySource() {
    TempDir tmp;
    const fs::path source = tmp.path() / "source";
    const fs::path dest = tmp.path() / "destination";
    fs::create_directories(source);
    writeFile(dest / "old.txt", "obsolete");

    FileSorter::processRule({source, dest, ".png", "IMAGES"});

    require(!fs::exists(dest / "old.txt"),
            "Old destination content must be removed for empty source");
    require(fs::is_directory(dest / "IMAGES"),
            "Matching group directory should be created");
    require(fs::is_directory(dest / "OTHERS"),
            "OTHERS group directory should be created");
}

// Тест обработки символических ссылок.
// Ожидаем: обычный файл копируется, а ссылка на него пропускается,
// чтобы не копировать один и тот же файл через ссылку повторно.
void testFileSymlinkIsSkipped() {
    TempDir tmp;
    const fs::path source = tmp.path() / "source";
    const fs::path dest = tmp.path() / "destination";
    writeFile(source / "actual.png", "data");
    fs::create_symlink(source / "actual.png", source / "alias.png");

    FileSorter::processRule({source, dest, ".png", "IMAGES"});

    require(fs::exists(dest / "IMAGES" / "actual.png"),
            "Regular file was not copied");
    require(!fs::exists(dest / "IMAGES" / "alias.png"),
            "Symlink should be skipped");
}

using Test = std::pair<const char*, std::function<void()>>;

}  // namespace

int main() {
    const std::vector<Test> tests = {
        {"recursive sorting and preserved paths", testRecursiveSorting},
        {"refresh after a second run", testSecondRunRefreshesDestination},
        {"independent rules with different destinations", testIndependentRules},
        {"invalid inputs do not erase files", testInvalidRulesDoNotEraseData},
        {"empty source clears old destination", testEmptySource},
        {"file symlinks are skipped", testFileSymlinkIsSkipped},
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
    std::cout << tests.size() - failed << '/' << tests.size()
              << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
