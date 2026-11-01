#include "daemon.hpp"

#include <iostream>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    using namespace diskmon;
    openlog("diskmon", LOG_PID | LOG_NDELAY, LOG_LOCAL0);
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage: diskmon CONFIG [PID_FILE]\n       diskmon --check CONFIG\n";
            closelog();
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--check") {
            const auto roots = loadConfig(fs::absolute(argv[2]).lexically_normal());
            std::cout << "Configuration OK: " << roots.size() << " root(s)\n";
            closelog();
            return 0;
        }
        if (argc < 2 || argc > 3) {
            throw std::runtime_error("Usage: diskmon CONFIG [PID_FILE]; diskmon --check CONFIG");
        }
        const auto configuration = fs::absolute(argv[1]).lexically_normal();
        fs::path pidFile;
        if (argc == 3) {
            pidFile = fs::absolute(argv[2]).lexically_normal();
        } else {
            const auto runtime = fs::path("/tmp") / ("diskmon-" + std::to_string(geteuid()));
            if (mkdir(runtime.c_str(), 0700) < 0 && errno != EEXIST) {
                systemError("create runtime directory");
            }
            struct stat status {};
            if (lstat(runtime.c_str(), &status) < 0 || !S_ISDIR(status.st_mode) ||
                status.st_uid != geteuid() || (status.st_mode & 0077) != 0) {
                throw std::runtime_error("Unsafe runtime directory: " + runtime.string());
            }
            pidFile = runtime / "diskmon.pid";
        }
        const int result = Daemon::instance().run(configuration, pidFile);
        closelog();
        return result;
    } catch (const std::exception& exception) {
        syslog(LOG_ERR, "ERROR %s", exception.what());
        std::cerr << "ERROR " << exception.what() << '\n';
        closelog();
        return 1;
    }
}
