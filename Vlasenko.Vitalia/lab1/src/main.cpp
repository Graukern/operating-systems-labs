#include "Config.hpp"
#include "Daemon.hpp"
#include "FileSorter.hpp"

#include <charconv>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {
unsigned int parseNumber(const char* text, unsigned int upperBound,
                         const char* description) {
    const std::string value(text);
    unsigned int number = 0;
    const auto parsed = std::from_chars(value.data(),
                                        value.data() + value.size(), number);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() ||
        number < 1 || number > upperBound) {
        throw std::invalid_argument(description);
    }
    return number;
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        if ((argc == 3 || argc == 4) &&
            std::string(argv[1]) == "--daemon-demo") {
            // Все относительные пути преобразуем в абсолютные ДО fork()/setsid().
            const fs::path report = fs::absolute(argv[2]).lexically_normal();
            fs::create_directories(report.parent_path());
            const unsigned int seconds = (argc == 4)
                ? parseNumber(argv[3], 30, "Duration must be from 1 to 30")
                : 10U;
            Daemon::instance().runDemo(report, seconds);
            return 0;
        }

        if ((argc == 3 || argc == 4) &&
            std::string(argv[1]) == "--daemon") {
            const bool foreground = argc == 4;
            if (foreground && std::string(argv[3]) != "--foreground") {
                throw std::invalid_argument("Expected --foreground as last argument");
            }
            return Daemon::instance().runManaged(argv[2], !foreground);
        }

        if ((argc == 4 || argc == 5) &&
            std::string(argv[1]) == "--loop") {
            // Учебный режим: ограниченное число циклов для проверки основного цикла.
            // На переднем плане работает по умолчанию, --background — явно.
            const bool background = argc == 5;
            if (background && std::string(argv[4]) != "--background") {
                throw std::invalid_argument("Expected --background as last argument");
            }
            const unsigned int cycles = parseNumber(
                argv[3], 20, "Cycle count must be from 1 to 20");
            return Daemon::instance().runCycles(argv[2], cycles, background);
        }

        if (argc == 3 && std::string(argv[1]) == "--config") {
            // Разовая обработка всех правил из конфига.
            Config config(argv[2]);
            config.load();
            for (const SortRule& rule : config.rules()) {
                FileSorter::processRule(rule);
            }
            std::cout << "Processed " << config.rules().size()
                      << " rule(s). Configured interval: "
                      << config.intervalSeconds() << " seconds.\n";
            return 0;
        }

        if (argc == 5) {
            // Разовая обработка по одному правилу из командной строки.
            FileSorter::processRule({argv[1], argv[2], argv[3], argv[4]});
            std::cout << "Sorting completed successfully.\n";
            return 0;
        }

        std::cerr << "Usage:\n  " << argv[0]
                  << " --daemon <config-file> [--foreground]\n  "
                  << argv[0] << " --daemon-demo <report-file> [seconds: 1..30]\n  "
                  << argv[0] << " --loop <config-file> <cycles: 1..20> [--background]\n  "
                  << argv[0] << " --config <config-file>\n  "
                  << argv[0]
                  << " <source> <destination> <extension> <subfolder>\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
