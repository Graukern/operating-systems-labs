#include <getopt.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "config.h"
#include "daemon.h"

namespace {

void usage(std::FILE* out) {
    std::fprintf(out,
                 "Usage: diskmon [options] [CONFIG]\n"
                 "Watches directories from CONFIG (default: ./diskmon.conf) with inotify\n"
                 "and logs every file event to syslog with facility LOCAL0.\n"
                 "\n"
                 "  -f, --foreground      do not daemonize, also print log to stderr\n"
                 "  -p, --pid-file PATH   pid file (default: %s)\n"
                 "  -c, --check-config    validate CONFIG, print the rules and exit\n"
                 "  -h, --help            show this help\n"
                 "\n"
                 "Signals: SIGHUP - reload config, SIGUSR1 - log statistics, SIGTERM - stop.\n",
                 "$XDG_RUNTIME_DIR/diskmon.pid");
}

std::string defaultPidPath() {
    if (geteuid() == 0) return "/run/diskmon.pid";
    if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR")) {
        return std::string(runtimeDir) + "/diskmon.pid";
    }
    return "/tmp/diskmon-" + std::to_string(geteuid()) + ".pid";
}

int checkConfig(const std::string& path) {
    Config config = loadConfig(path);
    std::printf("%s: OK\n", path.c_str());
    std::printf("debounce_ms %d\n", config.debounceMs);
    for (const auto& rule : config.rules) {
        std::printf("watch %s\n", describe(rule).c_str());
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    options.pidPath = defaultPidPath();
    bool check = false;

    const option longOptions[] = {
        {"foreground", no_argument, nullptr, 'f'},
        {"pid-file", required_argument, nullptr, 'p'},
        {"check-config", no_argument, nullptr, 'c'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "fp:ch", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 'f': options.foreground = true; break;
            case 'p': options.pidPath = optarg; break;
            case 'c': check = true; break;
            case 'h': usage(stdout); return EXIT_SUCCESS;
            default: usage(stderr); return 2;
        }
    }
    if (optind < argc) options.configPath = argv[optind++];
    if (optind < argc) {
        usage(stderr);
        return 2;
    }

    try {
        if (check) return checkConfig(options.configPath);
        return Daemon::instance().run(options);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "diskmon: %s\n", e.what());
        return EXIT_FAILURE;
    }
}
