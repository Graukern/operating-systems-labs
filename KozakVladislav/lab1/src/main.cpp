#include "Daemon.h"
#include <filesystem>
#include <cstdlib>
#include <syslog.h>

int main(int argc, char* argv[]) {
    std::string configPath = "config.ini";
    if (argc > 1) configPath = argv[1];

    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::canonical(configPath, ec);
    if (ec) {
        openlog("reminderd", LOG_PID | LOG_CONS, LOG_DAEMON);
        syslog(LOG_ERR, "Cannot resolve config path: %s", configPath.c_str());
        closelog();
        return 1;
    }

    Daemon& daemon = Daemon::getInstance();
    daemon.setConfigPath(absPath.string());
    daemon.run();
    return 0;
}