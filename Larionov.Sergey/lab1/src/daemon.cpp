#include "daemon.hpp"
#include "monitor.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

namespace diskmon {
namespace {

class Descriptor {
public:
    explicit Descriptor(int value = -1) : value_(value) {}
    ~Descriptor() { if (value_ >= 0) { close(value_); } }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const { return value_; }
private:
    int value_;
};

std::string executable(pid_t pid) {
    std::error_code error;
    auto path = fs::read_symlink("/proc/" + std::to_string(pid) + "/exe", error).string();
    const std::string suffix = " (deleted)";
    if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
        path.resize(path.size() - suffix.size());
    }
    return error ? "" : path;
}

pid_t readPid(int descriptor) {
    char buffer[64] {};
    const auto count = pread(descriptor, buffer, sizeof(buffer) - 1, 0);
    if (count < 0) { systemError("read pid file"); }
    if (count == 0) { return 0; }
    std::istringstream input(std::string(buffer, static_cast<std::size_t>(count)));
    long long number = 0;
    std::string extra;
    if (!(input >> number) || (input >> extra) || number <= 1 ||
        number > std::numeric_limits<pid_t>::max()) {
        throw std::runtime_error("Invalid PID in pid file");
    }
    return static_cast<pid_t>(number);
}

class PidFile {
public:
    explicit PidFile(const fs::path& path)
        : descriptor_(open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600)) {
        if (descriptor_.get() < 0) { systemError("open pid file"); }
        struct stat status {};
        if (fstat(descriptor_.get(), &status) < 0) { systemError("stat pid file"); }
        if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
            status.st_nlink != 1 || (status.st_mode & 0022) != 0) {
            throw std::runtime_error("Pid file must be a private regular file owned by this user");
        }
        // Serialize the entire replacement handshake. Without this, several
        // simultaneous launchers can all signal the same predecessor and then
        // wait forever for an intervening instance that nobody signalled.
        const auto launchPath = pidFileLockPath(path);
        Descriptor launch(open(launchPath.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW |
                                                  O_CLOEXEC | O_NONBLOCK, 0600));
        if (launch.get() < 0) { systemError("open launch lock"); }
        if (fstat(launch.get(), &status) < 0) { systemError("stat launch lock"); }
        if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
            status.st_nlink != 1 || (status.st_mode & 0022) != 0) {
            throw std::runtime_error("Unsafe launch lock");
        }
        const auto launchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (flock(launch.get(), LOCK_EX | LOCK_NB) < 0) {
            if (errno != EWOULDBLOCK) { systemError("lock launch file"); }
            if (std::chrono::steady_clock::now() >= launchDeadline) {
                throw std::runtime_error("Timed out waiting for another launcher");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool requested = false;
        while (flock(descriptor_.get(), LOCK_EX | LOCK_NB) < 0) {
            if (errno != EWOULDBLOCK) { systemError("lock pid file"); }
            const auto oldPid = readPid(descriptor_.get());
            if (oldPid > 1 && !requested) {
                stopPrevious(oldPid);
                requested = true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("Previous instance did not release pid file within 5 seconds");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        // A stale file can remain after SIGKILL. Check /proc before reusing it.
        const auto oldPid = readPid(descriptor_.get());
        if (oldPid > 1 && !requested && fs::exists("/proc/" + std::to_string(oldPid))) {
            stopPrevious(oldPid);
        }
        const auto text = std::to_string(getpid()) + "\n";
        if (ftruncate(descriptor_.get(), 0) < 0 ||
            pwrite(descriptor_.get(), text.data(), text.size(), 0) != static_cast<ssize_t>(text.size())) {
            systemError("write pid file");
        }
        owns_ = true;
    }

    ~PidFile() {
        // Keep the inode stable: unlinking a locked file permits two independent locks.
        if (owns_ && ftruncate(descriptor_.get(), 0) < 0) {
            syslog(LOG_ERR, "Cannot clear pid file: %m");
        }
    }

private:
    Descriptor descriptor_;
    bool owns_ = false;

    static fs::path pidFileLockPath(const fs::path& path) {
        return fs::path(path.string() + ".lock");
    }

    static void stopPrevious(pid_t pid) {
        const auto proc = fs::path("/proc") / std::to_string(pid);
        if (!fs::exists(proc)) { return; }
        struct stat status {};
        if (pid == getpid() || stat(proc.c_str(), &status) < 0 || status.st_uid != geteuid() ||
            executable(pid).empty() || executable(pid) != executable(getpid())) {
            throw std::runtime_error("Pid file refers to an unrelated process; refusing to send SIGTERM");
        }
        if (kill(pid, SIGTERM) < 0 && errno != ESRCH) { systemError("SIGTERM previous instance"); }
        syslog(LOG_INFO, "REPLACE previous_pid=%ld", static_cast<long>(pid));
    }
};

void closeInherited(int keep) {
    DIR* directory = opendir("/proc/self/fd");
    if (directory == nullptr) { systemError("list inherited descriptors"); }
    const int listing = dirfd(directory);
    while (const auto* entry = readdir(directory)) {
        char* end = nullptr;
        const long value = std::strtol(entry->d_name, &end, 10);
        if (*end == '\0' && value >= 0 && value != keep && value != listing) {
            close(static_cast<int>(value));
        }
    }
    closedir(directory);
    const int null = open("/dev/null", O_RDWR);
    if (null < 0) { systemError("open /dev/null"); }
    for (int target = 0; target < 3; ++target) {
        if (dup2(null, target) < 0) { systemError("redirect standard descriptors"); }
    }
    if (null > 2) { close(null); }
}

void notify(int descriptor, const std::string& message) {
    const auto count = write(descriptor, message.data(), message.size());
    if (count < 0) { syslog(LOG_ERR, "Cannot report startup status: %m"); }
}

void loop(const fs::path& configuration, std::unique_ptr<Monitor>& monitor, int signals) {
    for (;;) {
        pollfd descriptors[] = {{signals, POLLIN, 0}, {monitor->descriptor(), POLLIN, 0}};
        if (poll(descriptors, 2, monitor->timeout()) < 0) {
            if (errno == EINTR) { continue; }
            systemError("poll");
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            signalfd_siginfo information {};
            while (read(signals, &information, sizeof(information)) == sizeof(information)) {
                if (information.ssi_signo == SIGTERM || information.ssi_signo == SIGINT) {
                    syslog(LOG_INFO, "STOP signal=%u", information.ssi_signo);
                    return;
                }
                if (information.ssi_signo == SIGHUP) {
                    try {
                        auto replacement = std::make_unique<Monitor>(loadConfig(configuration));
                        monitor.swap(replacement);
                        syslog(LOG_INFO, "RELOAD_OK config=%s", quote(configuration.string()).c_str());
                    } catch (const std::exception& exception) {
                        syslog(LOG_ERR, "RELOAD_FAILED keeping previous configuration: %s", exception.what());
                    }
                }
            }
            // The monitor descriptor may have changed after SIGHUP.
            continue;
        }
        if ((descriptors[1].revents & POLLIN) != 0) { monitor->consume(); }
        monitor->expireMoves();
        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            throw std::runtime_error("inotify descriptor failed");
        }
    }
}

} // namespace

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

int Daemon::run(const fs::path& configuration, const fs::path& pidFile) {
    // Validate before stopping a working instance.
    const auto roots = loadConfig(configuration);
    sigset_t mask;
    sigset_t previous;
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, &previous) < 0) { systemError("block signals"); }
    int pipeFds[2];
    if (pipe2(pipeFds, O_CLOEXEC) < 0) { systemError("startup pipe"); }
    // Avoid a fatal SIGPIPE if the launcher is interrupted.
    signal(SIGPIPE, SIG_IGN);
    closelog();
    const auto child = fork();
    if (child < 0) { systemError("fork"); }
    if (child > 0) {
        close(pipeFds[1]);
        std::string result;
        char buffer[1024];
        ssize_t count;
        while ((count = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
            result.append(buffer, static_cast<std::size_t>(count));
        }
        close(pipeFds[0]);
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        sigprocmask(SIG_SETMASK, &previous, nullptr);
        if (result.rfind("OK ", 0) == 0) {
            std::cout << result;
            return 0;
        }
        std::cerr << (result.empty() ? "Daemon failed before reporting startup\n" : result);
        return 1;
    }
    close(pipeFds[0]);
    int result = 1;
    try {
        if (setsid() < 0) { systemError("setsid"); }
        const auto grandchild = fork();
        if (grandchild < 0) { systemError("second fork"); }
        if (grandchild > 0) { _exit(0); }
        umask(0077);
        if (chdir("/") < 0) { systemError("chdir /"); }
        closeInherited(pipeFds[1]);
        openlog("diskmon", LOG_PID | LOG_NDELAY, LOG_LOCAL0);
        Descriptor signals(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
        if (signals.get() < 0) { systemError("signalfd"); }
        auto monitor = std::make_unique<Monitor>(roots);
        PidFile pid(pidFile);
        syslog(LOG_INFO, "START pid=%ld config=%s", static_cast<long>(getpid()),
               quote(configuration.string()).c_str());
        notify(pipeFds[1], "OK pid=" + std::to_string(getpid()) + "\n");
        close(pipeFds[1]);
        pipeFds[1] = -1;
        loop(configuration, monitor, signals.get());
        result = 0;
    } catch (const std::exception& exception) {
        openlog("diskmon", LOG_PID | LOG_NDELAY, LOG_LOCAL0);
        syslog(LOG_ERR, "ERROR %s", exception.what());
        if (pipeFds[1] >= 0) {
            notify(pipeFds[1], "ERROR " + std::string(exception.what()) + "\n");
            close(pipeFds[1]);
        }
    }
    closelog();
    _exit(result);
}

} // namespace diskmon
