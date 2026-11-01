#include "../src/config.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    char pattern[] = "/tmp/disk-monitor-config-XXXXXX";
    const char* created = ::mkdtemp(pattern);
    if (!created) return 1;
    const fs::path root = fs::canonical(created);
    try {
        fs::create_directories(root / "watch with spaces");
        fs::create_directories(root / "watch2");
        fs::create_directories(root / "runtime/events");
        const auto filename = root / "monitor.conf";
        const std::string valid = "# comment\n\nwatch = ./watch with spaces\nwatch=watch2\n"
            "event_log_dir=runtime/events\npid_file=runtime/service.pid\n";
        auto save = [&](const std::string& text) { std::ofstream(filename) << text; };
        auto require = [](bool condition, const std::string& message) {
            if (!condition) throw std::runtime_error(message);
        };
        save(valid);
        const auto old = fs::current_path();
        fs::current_path("/");
        const auto config = Config::load(filename);
        fs::current_path(old);
        require(config.directories.size() == 2, "multiple watches");
        require(config.directories[0] == root / "watch with spaces", "relative paths/spaces");
        require(config.pid_file == root / "runtime/service.pid", "relative PID path");
        unsigned checks = 3;
        auto invalid = [&](const std::string& text) {
            save(text);
            bool rejected = false;
            try { (void)Config::load(filename); } catch (const std::exception&) { rejected = true; }
            require(rejected, "invalid configuration accepted: " + text);
            ++checks;
        };
        invalid("");
        invalid(valid + "unknown=value\n");
        invalid(valid + "watch=watch2\n");
        invalid(valid + "pid_file=runtime/other.pid\n");
        invalid(valid + "watch=missing\n");
        invalid(valid + "line without equals\n");
        invalid(valid + "watch=\n");
        invalid("watch=watch2\nevent_log_dir=watch2\npid_file=runtime/service.pid\n");
        invalid("watch=watch2\nevent_log_dir=runtime/events\npid_file=watch2/service.pid\n");
        invalid("watch=watch2\nevent_log_dir=runtime/events\npid_file=absent/service.pid\n");
        invalid("watch=watch2\nevent_log_dir=missing\npid_file=runtime/service.pid\n");
        fs::create_directory_symlink(root / "watch2", root / "alias");
        invalid(valid + "watch=alias\n");
        require(quote("a\n\"\\\t") == "\"a\\u000a\\\"\\\\\\u0009\"", "log escaping");
        ++checks;
        fs::remove_all(root);
        std::cout << "PASS: " << checks << " configuration/escaping checks\n";
        return 0;
    } catch (const std::exception& error) {
        fs::remove_all(root);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
