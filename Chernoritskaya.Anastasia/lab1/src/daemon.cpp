#include "daemon.hpp"
#include "config.hpp"
#include "pidfile.hpp"
#include "watcher.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

volatile sig_atomic_t g_reload = 0;
volatile sig_atomic_t g_term = 0;

void onSignal(int sig) {
    if (sig == SIGHUP)
        g_reload = 1;
    else if (sig == SIGTERM)
        g_term = 1;
}

const int EVENT_PRIO = LOG_LOCAL0 | LOG_INFO;  
const int DAEMON_INFO = LOG_DAEMON | LOG_INFO;
const int DAEMON_WARN = LOG_DAEMON | LOG_WARNING;
const int DAEMON_ERR = LOG_DAEMON | LOG_ERR;

}  

Daemon& Daemon::instance() {
    static Daemon d;
    return d;
}

void Daemon::log(int priority, const std::string& msg) {
    if (sink_)
        sink_(priority, msg);
    else
        syslog(priority, "%s", msg.c_str());
}

bool Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork");
        return false;
    }
    if (pid > 0) _exit(0);  
    if (setsid() < 0) return false;  
    umask(0);
    if (chdir("/") != 0) return false;  
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096) maxfd = 4096;
    for (int i = 0; i < maxfd; ++i) close(i);
    int fd = open("/dev/null", O_RDWR);  
    if (fd >= 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
    }
    return true;
}

void Daemon::setupSignals() {
    g_reload = 0;
    g_term = 0;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool Daemon::loadConfig(Watcher& w) {
    config::Result r;
    if (!config::loadFile(config_, r)) {
        log(DAEMON_ERR, "cannot open config " + config_);
        return false;
    }
    for (const auto& warn : r.warnings) log(DAEMON_WARN, "config: " + warn);
    auto failed = w.setDirs(r.dirs);
    for (const auto& d : r.dirs) {
        auto it = std::find_if(failed.begin(), failed.end(),
                               [&](const std::pair<std::string, int>& f) { return f.first == d; });
        if (it == failed.end())
            log(DAEMON_INFO, "watching " + d);
        else
            log(DAEMON_ERR, "cannot watch " + d + ": " + std::strerror(it->second));
    }
    return true;
}

void Daemon::loop(Watcher& w) {
    struct pollfd p;
    p.fd = w.fd();
    p.events = POLLIN;
    p.revents = 0;
    while (!g_term) {
        if (g_reload) {
            g_reload = 0;
            log(DAEMON_INFO, "SIGHUP: reloading config");
            loadConfig(w);
        }
        int r = poll(&p, 1, 200);  
        if (r > 0 && (p.revents & POLLIN))
            for (const auto& line : w.readEvents()) log(EVENT_PRIO, line);
    }
}

int Daemon::run(const std::string& configPath) {
    char abs[PATH_MAX];
    if (!realpath(configPath.c_str(), abs)) {
        std::cerr << "diskmon: config not found: " << configPath << std::endl;
        return 1;
    }
    config_ = abs;

    openlog("diskmon", LOG_PID, LOG_LOCAL0);

    pidfile::Prev prev = pidfile::terminatePrevious(pidFile_, getpid());
    if (prev == pidfile::Prev::Failed) {
        std::cerr << "diskmon: cannot stop previous instance (see " << pidFile_ << ")"
                  << std::endl;
        closelog();
        return 1;
    }

    if (!daemonize()) {
        closelog();
        return 1;
    }

    if (!pidfile::write(pidFile_, getpid())) {
        log(DAEMON_ERR, "cannot write pid file " + pidFile_);
        closelog();
        return 1;
    }
    setupSignals();

    Watcher watcher;
    if (!watcher.ok()) {
        log(DAEMON_ERR, std::string("inotify_init failed: ") + std::strerror(errno));
        pidfile::removeIfOwner(pidFile_, getpid());
        closelog();
        return 1;
    }

    log(DAEMON_INFO, "daemon started, config: " + config_);
    if (prev == pidfile::Prev::Terminated) log(DAEMON_INFO, "previous instance terminated");
    loadConfig(watcher);

    loop(watcher);

    log(DAEMON_INFO, "daemon exiting (SIGTERM)");
    pidfile::removeIfOwner(pidFile_, getpid());
    closelog();
    return 0;
}