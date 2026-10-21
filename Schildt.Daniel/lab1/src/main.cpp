#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

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

// ---------- Конфиг ----------

struct Config {
    std::string dir1;
    std::string dir2;
    int interval = 30;
};

static std::string trim(const std::string& s)
{
    const char* ws = " \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return "";
    }
    std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

static bool loadConfig(const std::string& path, Config& cfg, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }

    Config result;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(lineNo) + ": expected key=value";
            return false;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "dir1") {
            result.dir1 = value;
        } else if (key == "dir2") {
            result.dir2 = value;
        } else if (key == "interval") {
            char* end = nullptr;
            long v = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0' || v <= 0 || v > 86400) {
                error = "line " + std::to_string(lineNo) + ": bad interval";
                return false;
            }
            result.interval = static_cast<int>(v);
        } else {
            error = "line " + std::to_string(lineNo) + ": unknown key '" + key + "'";
            return false;
        }
    }

    if (result.dir1.empty() || result.dir2.empty()) {
        error = "dir1 and dir2 are required";
        return false;
    }
    cfg = result;
    return true;
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