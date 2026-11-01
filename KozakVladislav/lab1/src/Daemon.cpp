#include "Daemon.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#include <ctime>
#include <time.h>

volatile sig_atomic_t g_reload = 0;
volatile sig_atomic_t g_terminate = 0;

static void signalHandler(int sig) {
    if (sig == SIGHUP)      g_reload = 1;
    else if (sig == SIGTERM) g_terminate = 1;
}

Daemon& Daemon::getInstance() {
    static Daemon instance;
    return instance;
}

Daemon::Daemon()
    : pidPath_("/tmp/reminderd_" + std::to_string(getuid()) + ".pid"),
      terminalCmd_("xterm -e sh -c"),
      interval_(10) {}

void Daemon::setConfigPath(const std::string& path) {
    configPath_ = path;
}

void Daemon::run() {
    openlog("reminderd", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon starting");

    readConfig();              
    checkAndKillOldDaemon();   
    daemonize();               
    writePidFile();            
    setupSignalHandlers();     

    while (!g_terminate) {
        if (g_reload) {
            g_reload = 0;
            readConfig();
            syslog(LOG_INFO, "Configuration reloaded");
        }
        processEvents();

        struct timespec req{interval_, 0}, rem{};
        while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
            if (g_terminate || g_reload) break;
            req = rem;
        }
    }

    removePidFile();
    syslog(LOG_INFO, "Daemon exiting");
    closelog();
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) { syslog(LOG_ERR, "First fork failed: %s", strerror(errno)); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) { syslog(LOG_ERR, "setsid failed: %s", strerror(errno)); exit(EXIT_FAILURE); }

    pid = fork();
    if (pid < 0) { syslog(LOG_ERR, "Second fork failed: %s", strerror(errno)); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") < 0) syslog(LOG_ERR, "chdir failed: %s", strerror(errno));

    long maxfd = sysconf(_SC_OPEN_MAX);
    for (long fd = maxfd; fd >= 0; --fd) close(static_cast<int>(fd));

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        if (dup2(fd, STDIN_FILENO)  < 0 ||
            dup2(fd, STDOUT_FILENO) < 0 ||
            dup2(fd, STDERR_FILENO) < 0) {
            syslog(LOG_ERR, "dup2 failed: %s", strerror(errno));
        }
        if (fd > STDERR_FILENO) close(fd);
    }
}

void Daemon::checkAndKillOldDaemon() {
    std::ifstream pidFile(pidPath_);
    if (!pidFile) return;

    pid_t oldPid = 0;
    pidFile >> oldPid;
    pidFile.close();
    if (oldPid <= 0) { std::remove(pidPath_.c_str()); return; }

    std::string procPath = "/proc/" + std::to_string(oldPid);
    if (access(procPath.c_str(), F_OK) != 0) {
        std::remove(pidPath_.c_str());
        return;
    }

    std::ifstream commFile(procPath + "/comm");
    std::string comm;
    if (commFile) commFile >> comm;

    if (comm == "reminderd") {
        syslog(LOG_INFO, "Killing old daemon with PID %d", oldPid);
        if (kill(oldPid, SIGTERM) != 0) {
            syslog(LOG_WARNING, "kill(%d, SIGTERM) failed: %s", oldPid, strerror(errno));
        }
        sleep(1);
    } else {
        syslog(LOG_WARNING,
               "PID %d from %s is not reminderd (comm='%s') — not killing",
               oldPid, pidPath_.c_str(), comm.c_str());
    }
    std::remove(pidPath_.c_str());
}

void Daemon::writePidFile() {
    int fd = open(pidPath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        syslog(LOG_ERR, "Cannot open pid file %s: %s — exiting",
               pidPath_.c_str(), strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (fchmod(fd, 0600) != 0) {
        syslog(LOG_WARNING, "fchmod(%s) failed: %s", pidPath_.c_str(), strerror(errno));
    }
    int ret = dprintf(fd, "%d\n", static_cast<int>(getpid()));
    if (ret < 0) {
        syslog(LOG_ERR, "Cannot write pid file %s: %s — exiting",
               pidPath_.c_str(), strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

void Daemon::removePidFile() {
    std::remove(pidPath_.c_str());
}

void Daemon::setupSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    struct sigaction sc{};
    sc.sa_handler = SIG_IGN;
    sigemptyset(&sc.sa_mask);
    sigaction(SIGCHLD, &sc, nullptr);
}

void Daemon::readConfig() {
    std::ifstream file(configPath_);
    if (!file) {
        syslog(LOG_ERR, "Cannot open config file: %s", configPath_.c_str());
        return;
    }

    std::vector<Event> newEvents;
    std::string line;
    bool inEvents = false;

    while (std::getline(file, line)) {
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        auto end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line == "[settings]") { inEvents = false; continue; }
        if (line == "[events]")   { inEvents = true;  continue; }

        if (inEvents) {
            Event ev;
            if (Event::parse(line, ev)) newEvents.push_back(std::move(ev));
            else syslog(LOG_WARNING, "Invalid event line: %s", line.c_str());
        } else {
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            auto ke = key.find_last_not_of(" \t");
            if (ke != std::string::npos) key = key.substr(0, ke + 1);
            auto vs = value.find_first_not_of(" \t");
            value = (vs == std::string::npos) ? "" : value.substr(vs);

            try {
                if (key == "interval") {
                    int v = std::stoi(value);
                    if (v <= 0) syslog(LOG_ERR, "Invalid interval: %s (must be > 0)", value.c_str());
                    else        interval_ = v;
                } else if (key == "pid_file") pidPath_ = value;
                else if (key == "terminal")   terminalCmd_ = value;
            } catch (const std::exception& e) {
                syslog(LOG_WARNING, "Invalid value for %s: %s", key.c_str(), e.what());
            }
        }
    }

    std::time_t now = std::time(nullptr);
    std::set<std::string> newKeys;
    for (const auto& ev : newEvents) {
        newKeys.insert(ev.key());
        if (handled_.find(ev.key()) != handled_.end()) continue;

        if (ev.baseTime() < now) {
            if (ev.flag() == '\0') {
                handled_[ev.key()] = ev.baseTime();
            } else {
                long period = Event::periodForFlag(ev.flag());
                long n = (now - ev.baseTime()) / period;
                handled_[ev.key()] = ev.baseTime() + n * period;
            }
        }
    }
    for (auto it = handled_.begin(); it != handled_.end(); ) {
        if (newKeys.find(it->first) == newKeys.end()) it = handled_.erase(it);
        else ++it;
    }
    events_ = std::move(newEvents);
}

void Daemon::processEvents() {
    std::time_t now = std::time(nullptr);
    for (const auto& ev : events_) {
        const std::string key = ev.key();
        auto it = handled_.find(key);
        std::time_t next = (it == handled_.end()) ? ev.baseTime()
                                                  : ev.nextOccurrenceAfter(it->second);

        if (next != 0 && next <= now) {
            openTerminal(ev.text());
            syslog(LOG_INFO, "Reminder triggered: %s", ev.text().c_str());
            handled_[key] = next;
        }
    }
}

void Daemon::openTerminal(const std::string& text) const {
    std::string escaped;
    for (char c : text) {
        if (c == '\'') escaped += "'\\''";
        else           escaped += c;
    }
    std::string command = terminalCmd_ + " 'echo \"" + escaped + "\"; sleep 5'";

    pid_t pid = fork();
    if (pid == 0) {
        execlp("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
        _exit(1);
    }
}