#include "Daemon.h"

#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

static const char* const kPidFile = "/tmp/lab1_daemon.pid";

volatile std::sig_atomic_t Daemon::terminate_ = 0;
volatile std::sig_atomic_t Daemon::reload_ = 0;

Daemon& Daemon::instance()
{
    static Daemon d;
    return d;
}

void Daemon::handleSignal(int sig)
{
    if (sig == SIGTERM) {
        terminate_ = 1;
    } else if (sig == SIGHUP) {
        reload_ = 1;
    }
}

bool Daemon::setup(const std::string& configArg, std::string& error)
{
    char absPath[PATH_MAX];
    if (realpath(configArg.c_str(), absPath) == nullptr) {
        error = "realpath: " + std::string(std::strerror(errno));
        return false;
    }
    configPath_ = absPath;
    return loadConfig(configPath_, config_, error);
}

void Daemon::daemonize()
{
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork");
        std::exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        std::exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        std::perror("setsid");
        std::exit(EXIT_FAILURE);
    }
    if (chdir("/") < 0) {
        std::perror("chdir");
        std::exit(EXIT_FAILURE);
    }

    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 4096) {
        maxFd = 4096;
    }
    for (int fd = 0; fd < maxFd; ++fd) {
        close(fd);
    }

    int fd0 = open("/dev/null", O_RDWR);
    if (fd0 != 0) {
        std::exit(EXIT_FAILURE);
    }
    dup2(0, 1);
    dup2(0, 2);
}

static std::string processName(pid_t pid)
{
    std::ifstream in("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(in, name);
    return name;
}

void Daemon::stopPreviousInstance()
{
    std::ifstream in(kPidFile);
    long old = 0;
    if (!(in >> old) || old <= 0 || old == getpid()) {
        return;
    }

    if (processName(old).empty() || processName(old) != processName(getpid())) {
        return;
    }

    kill(old, SIGTERM);
    for (int i = 0; i < 50 && !processName(old).empty(); ++i) {
        usleep(100000);
    }
}

void Daemon::writePidFile()
{
    std::ofstream out(kPidFile, std::ios::trunc);
    out << getpid() << "\n";
}

bool Daemon::appendHistory(std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::directory_iterator it(config_.dir1, ec);
    if (ec) {
        error = "cannot read " + config_.dir1 + ": " + ec.message();
        return false;
    }

    std::ofstream out(config_.dir2 + "/hist.log", std::ios::app);
    if (!out) {
        error = "cannot open " + config_.dir2 + "/hist.log";
        return false;
    }

    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    out << "[" << stamp << "]\n";

    for (const fs::directory_entry& entry : it) {
        out << "  " << entry.path().filename().string();
        if (entry.is_directory(ec)) {
            out << "/";
        }
        out << "\n";
    }
    return static_cast<bool>(out);
}

void Daemon::reloadConfig()
{
    Config fresh;
    std::string err;
    if (loadConfig(configPath_, fresh, err)) {
        config_ = fresh;
        syslog(LOG_INFO, "config reloaded: dir1=%s dir2=%s interval=%d",
               config_.dir1.c_str(), config_.dir2.c_str(), config_.interval);
    } else {
        syslog(LOG_ERR, "config reload failed: %s (keeping old settings)", err.c_str());
    }
}

void Daemon::run()
{
    stopPreviousInstance();
    daemonize();
    writePidFile();

    openlog("lab1_daemon", LOG_PID, LOG_DAEMON);

    struct sigaction sa {};
    sa.sa_handler = handleSignal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    syslog(LOG_INFO, "daemon started, config: %s", configPath_.c_str());
    syslog(LOG_INFO, "settings: dir1=%s dir2=%s interval=%d", config_.dir1.c_str(),
           config_.dir2.c_str(), config_.interval);

    while (!terminate_) {
        if (reload_) {
            reload_ = 0;
            reloadConfig();
        }

        std::string error;
        if (!appendHistory(error)) {
            syslog(LOG_ERR, "history failed: %s", error.c_str());
        }
        sleep(config_.interval);
    }

    syslog(LOG_INFO, "SIGTERM received, daemon exiting");
    unlink(kPidFile);
    closelog();
}