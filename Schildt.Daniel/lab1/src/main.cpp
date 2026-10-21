#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include "Config.h"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <ctime>
#include <filesystem>

// ---------- Демонизация ----------

static void daemonize()
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

// ---------- Сигналы ----------

static volatile std::sig_atomic_t g_terminate = 0;
static volatile std::sig_atomic_t g_reload = 0;

static void handleSignal(int sig)
{
    if (sig == SIGTERM) {
        g_terminate = 1;
    } else if (sig == SIGHUP) {
        g_reload = 1;
    }
}


// ---------- PID-файл ----------

static const char* const kPidFile = "/tmp/lab1_daemon.pid";

static pid_t readPidFile()
{
    std::ifstream in(kPidFile);
    long pid = 0;
    if (!(in >> pid) || pid <= 0) {
        return 0;
    }
    return static_cast<pid_t>(pid);
}

static std::string processName(pid_t pid)
{
    std::ifstream in("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(in, name);
    return name;
}

static void stopPreviousInstance()
{
    pid_t old = readPidFile();
    if (old == 0 || old == getpid()) {
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

static void writePidFile()
{
    std::ofstream out(kPidFile, std::ios::trunc);
    out << getpid() << "\n";
}

static bool appendHistory(const Config& cfg, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::directory_iterator it(cfg.dir1, ec);
    if (ec) {
        error = "cannot read " + cfg.dir1 + ": " + ec.message();
        return false;
    }

    std::ofstream out(cfg.dir2 + "/hist.log", std::ios::app);
    if (!out) {
        error = "cannot open " + cfg.dir2 + "/hist.log";
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

// ---------- main ----------

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <config>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char absPath[PATH_MAX];
    if (realpath(argv[1], absPath) == nullptr) {
        std::perror("realpath");
        return EXIT_FAILURE;
    }
    const std::string configPath = absPath;

    Config config;
    std::string error;
    if (!loadConfig(configPath, config, error)) {
        std::fprintf(stderr, "config error: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    stopPreviousInstance();
    daemonize();
    writePidFile();

    openlog("lab1_daemon", LOG_PID, LOG_DAEMON);

    struct sigaction sa {};
    sa.sa_handler = handleSignal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    syslog(LOG_INFO, "daemon started, config: %s", configPath.c_str());
    syslog(LOG_INFO, "settings: dir1=%s dir2=%s interval=%d", config.dir1.c_str(),
           config.dir2.c_str(), config.interval);

    while (!g_terminate) {
        if (g_reload) {
            g_reload = 0;
            Config fresh;
            std::string err;
            if (loadConfig(configPath, fresh, err)) {
                config = fresh;
                syslog(LOG_INFO, "config reloaded: dir1=%s dir2=%s interval=%d",
                       config.dir1.c_str(), config.dir2.c_str(), config.interval);
            } else {
                syslog(LOG_ERR, "config reload failed: %s (keeping old settings)",
                       err.c_str());
            }
        }
        std::string histError;
        if (!appendHistory(config, histError)) {
            syslog(LOG_ERR, "history failed: %s", histError.c_str());
        }
        sleep(config.interval);
    }

    syslog(LOG_INFO, "SIGTERM received, daemon exiting");
    unlink(kPidFile);
    closelog();
    return 0;
}