#include "daemon.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "log.h"

namespace {

std::runtime_error sysError(const std::string& what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}

std::string absolutePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr) throw sysError(path);
    return resolved;
}

const char* signalName(int sig) {
    switch (sig) {
        case SIGHUP: return "SIGHUP";
        case SIGINT: return "SIGINT";
        case SIGTERM: return "SIGTERM";
        case SIGUSR1: return "SIGUSR1";
        default: return "signal";
    }
}

void closeAllExcept(int keep) {
    std::vector<int> fds;
    if (DIR* dir = opendir("/proc/self/fd")) {
        int self = dirfd(dir);
        while (dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            int fd = std::atoi(entry->d_name);
            if (fd != keep && fd != self) fds.push_back(fd);
        }
        closedir(dir);
    } else {
        for (int fd = 0; fd < 1024; ++fd) {
            if (fd != keep) fds.push_back(fd);
        }
    }
    for (int fd : fds) close(fd);
}

void redirectStdioToDevNull() {
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return;
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
}

int waitForStartup(int fd) {
    std::string message;
    char buf[256];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            message.append(buf, static_cast<size_t>(n));
        } else if (n == 0 || errno != EINTR) {
            break;
        }
    }
    close(fd);
    if (message.compare(0, 3, "OK ") == 0) {
        std::printf("diskmon started, pid %s\n", message.c_str() + 3);
        return EXIT_SUCCESS;
    }
    if (message.compare(0, 4, "ERR ") == 0) {
        std::fprintf(stderr, "diskmon: %s\n", message.c_str() + 4);
    } else {
        std::fprintf(stderr, "diskmon: daemon exited during startup, see syslog\n");
    }
    return EXIT_FAILURE;
}

}  // namespace

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

int Daemon::run(const Options& options) {
    options_ = options;
    configPath_ = absolutePath(options.configPath);
    config_ = loadConfig(configPath_);

    dlog::open(options.foreground);
    pidFile_ = std::make_unique<PidFile>(options.pidPath);
    pidFile_->stopPrevious();

    if (!options.foreground) {
        daemonize();
    }

    try {
        setupSignals();
        pidFile_->acquire();
        watcher_ = std::make_unique<Watcher>();
        watcher_->apply(config_);
    } catch (const std::exception& e) {
        dlog::error("startup failed: %s", e.what());
        reportStartup(std::string("ERR ") + e.what());
        return EXIT_FAILURE;
    }

    startedAt_ = std::time(nullptr);
    dlog::info("started pid=%d config=%s rules=%zu", getpid(), configPath_.c_str(),
               config_.rules.size());
    reportStartup("OK " + std::to_string(getpid()));

    int code = EXIT_SUCCESS;
    try {
        loop();
        watcher_->flushDue(true);
        dlog::info("exiting: %s", stopReason_.c_str());
    } catch (const std::exception& e) {
        dlog::error("fatal error, exiting: %s", e.what());
        code = EXIT_FAILURE;
    }
    pidFile_->release();
    dlog::close();
    return code;
}

void Daemon::daemonize() {
    int pipeFds[2];
    if (pipe2(pipeFds, O_CLOEXEC) < 0) throw sysError("pipe2");

    pid_t pid = fork();
    if (pid < 0) throw sysError("fork");
    if (pid > 0) {
        close(pipeFds[1]);
        waitpid(pid, nullptr, 0);
        std::exit(waitForStartup(pipeFds[0]));
    }

    close(pipeFds[0]);
    if (setsid() < 0) _exit(EXIT_FAILURE);

    // Second fork: not a session leader, cannot acquire a terminal.
    pid = fork();
    if (pid < 0) _exit(EXIT_FAILURE);
    if (pid > 0) _exit(EXIT_SUCCESS);

    umask(022);
    if (chdir("/") < 0) _exit(EXIT_FAILURE);

    // Close the syslog socket properly, otherwise its fd number may get reused.
    dlog::close();
    closeAllExcept(pipeFds[1]);
    redirectStdioToDevNull();
    dlog::open(false);
    startupFd_ = pipeFds[1];
}

void Daemon::reportStartup(const std::string& message) {
    if (startupFd_ < 0) return;
    if (write(startupFd_, message.data(), message.size()) < 0) {
        dlog::warning("cannot report startup status: %s", std::strerror(errno));
    }
    close(startupFd_);
    startupFd_ = -1;
}

// signalfd: signals are handled in the main loop, where syslog() and reload are safe.
void Daemon::setupSignals() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &set, nullptr) < 0) throw sysError("sigprocmask");
    signalFd_ = signalfd(-1, &set, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signalFd_ < 0) throw sysError("signalfd");
}

void Daemon::loop() {
    running_ = true;
    while (running_) {
        pollfd fds[] = {{signalFd_, POLLIN, 0}, {watcher_->fd(), POLLIN, 0}};
        int n = poll(fds, 2, watcher_->pollTimeoutMs());
        if (n < 0) {
            if (errno == EINTR) continue;
            throw sysError("poll");
        }
        if (fds[1].revents & POLLIN) watcher_->handleEvents();
        if (fds[0].revents & POLLIN) handleSignals();
        watcher_->flushDue();
    }
}

void Daemon::handleSignals() {
    signalfd_siginfo info;
    while (read(signalFd_, &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))) {
        int sig = static_cast<int>(info.ssi_signo);
        switch (sig) {
            case SIGHUP:
                dlog::info("SIGHUP from pid %u, reloading %s", info.ssi_pid, configPath_.c_str());
                reload();
                break;
            case SIGTERM:
            case SIGINT:
                stopReason_ = std::string(signalName(sig)) + " from pid " +
                              std::to_string(info.ssi_pid);
                running_ = false;
                break;
            case SIGUSR1:
                logStats();
                break;
        }
    }
}

void Daemon::reload() {
    try {
        Config fresh = loadConfig(configPath_);
        config_ = std::move(fresh);
        watcher_->apply(config_);
        dlog::info("config reloaded: rules=%zu debounce_ms=%d", config_.rules.size(),
                   config_.debounceMs);
    } catch (const std::exception& e) {
        dlog::error("reload failed, keeping previous config: %s", e.what());
    }
}

void Daemon::logStats() {
    dlog::info("stats: uptime=%lds %s", static_cast<long>(std::time(nullptr) - startedAt_),
               watcher_->stats().c_str());
}
