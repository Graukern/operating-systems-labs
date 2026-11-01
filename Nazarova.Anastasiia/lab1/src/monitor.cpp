#include "config.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
[[noreturn]] void system_error(const std::string& operation) {
    throw std::runtime_error(operation + ": " + std::strerror(errno));
}

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    int get() const { return fd_; }
private:
    int fd_;
};

void write_all(int fd, const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto n = ::write(fd, text.data() + offset, text.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) system_error("write");
        offset += static_cast<std::size_t>(n);
    }
}

void log_message(int priority, const std::string& text) {
    ::syslog(priority, "%s", text.c_str());
}

std::string executable(pid_t pid) {
    std::array<char, 4096> path{};
    const auto link = "/proc/" + std::to_string(pid) + "/exe";
    const auto n = ::readlink(link.c_str(), path.data(), path.size() - 1);
    if (n < 0) system_error("readlink " + link);
    std::string result(path.data(), static_cast<std::size_t>(n));
    const std::string suffix = " (deleted)";
    if (result.size() >= suffix.size() && result.compare(result.size() - suffix.size(), suffix.size(), suffix) == 0)
        result.resize(result.size() - suffix.size());
    return result;
}

class PidFile {
public:
    explicit PidFile(const fs::path& path)
        : fd_(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)) {
        if (fd_.get() < 0) system_error("open PID file");
        struct stat info{};
        if (::fstat(fd_.get(), &info) < 0) system_error("fstat PID file");
        if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1)
            throw std::runtime_error("PID file must be a regular file owned by this user, without hard links");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool signalled = false;
        for (;;) {
            if (::flock(fd_.get(), LOCK_EX | LOCK_NB) == 0) {
                owned_ = true;
                if (!signalled) terminate_previous(read_pid());
                break;
            }
            if (errno != EWOULDBLOCK) system_error("flock PID file");
            const auto pid = read_pid();
            if (!signalled && pid > 1) {
                terminate_previous(pid);
                signalled = true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Previous instance did not release the PID file within 5 seconds");
            ::poll(nullptr, 0, 25);
        }
    }
    ~PidFile() {
        // Не удаляем файл, чтобы сохранить общую блокировку.
        if (owned_ && published_) {
            if (::ftruncate(fd_.get(), 0) < 0) log_message(LOG_ERR, "Cannot clear PID file");
        }
    }
    int fd() const { return fd_.get(); }
    void publish() {
        if (::ftruncate(fd_.get(), 0) < 0 || ::lseek(fd_.get(), 0, SEEK_SET) < 0) system_error("reset PID file");
        write_all(fd_.get(), std::to_string(::getpid()) + "\n");
        published_ = true;
        if (::fsync(fd_.get()) < 0) system_error("fsync PID file");
    }
private:
    Fd fd_;
    bool owned_ = false;
    bool published_ = false;

    pid_t read_pid() const {
        std::array<char, 64> buffer{};
        ssize_t size;
        do { size = ::pread(fd_.get(), buffer.data(), buffer.size(), 0); } while (size < 0 && errno == EINTR);
        if (size < 0) system_error("read PID file");
        if (size == 0) return 0;
        std::istringstream input(std::string(buffer.data(), static_cast<std::size_t>(size)));
        long long value = 0;
        std::string extra;
        if (!(input >> value) || (input >> extra) || value <= 1 || value > 2147483647)
            throw std::runtime_error("Invalid PID file; refusing to signal an unknown process");
        return static_cast<pid_t>(value);
    }

    static void terminate_previous(pid_t pid) {
        if (pid == 0) return;
        if (pid == ::getpid()) throw std::runtime_error("PID file refers to this process");
        const auto proc = "/proc/" + std::to_string(pid);
        if (!fs::exists(proc)) return;
        // pidfd защищает от повторного использования номера PID.
        Fd process(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
        if (process.get() < 0) {
            if (errno == ESRCH) return;
            system_error("pidfd_open (Linux >= 5.3 is required)");
        }
        pollfd state{process.get(), POLLIN, 0};
        if (::poll(&state, 1, 0) > 0) return;
        if (executable(pid) != executable(::getpid()))
            throw std::runtime_error("PID file refers to a different executable; no signal sent");
        if (::syscall(SYS_pidfd_send_signal, process.get(), SIGTERM, nullptr, 0) < 0 && errno != ESRCH)
            system_error("SIGTERM previous instance");
        int result;
        do { result = ::poll(&state, 1, 5000); } while (result < 0 && errno == EINTR);
        if (result < 0) system_error("wait previous instance");
        if (result == 0) throw std::runtime_error("Previous instance ignored SIGTERM; no SIGKILL sent");
    }
};

void close_inherited(int lock, int ready) {
    DIR* directory = ::opendir("/proc/self/fd");
    if (!directory) system_error("opendir /proc/self/fd");
    const int listing = ::dirfd(directory);
    std::vector<int> descriptors;
    while (const auto* entry = ::readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        const int fd = std::stoi(entry->d_name);
        if (fd > 2 && fd != lock && fd != ready && fd != listing) descriptors.push_back(fd);
    }
    ::closedir(directory);
    for (int fd : descriptors) ::close(fd);
}

// Родитель ждёт готовности демона.
Fd detach(int lock) {
    int pipe_fds[2];
    if (::pipe2(pipe_fds, O_CLOEXEC) < 0) system_error("pipe2");
    const pid_t child = ::fork();
    if (child < 0) {
        const int saved = errno;
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        errno = saved;
        system_error("fork");
    }
    if (child > 0) {
        ::close(pipe_fds[1]);
        ::close(lock);
        char answer = 'E';
        ssize_t size;
        do { size = ::read(pipe_fds[0], &answer, 1); } while (size < 0 && errno == EINTR);
        ::close(pipe_fds[0]);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        if (size != 1 || answer != 'R') {
            const char message[] = "Daemon startup failed; see syslog (local0).\n";
            const auto ignored = ::write(STDERR_FILENO, message, sizeof(message) - 1);
            (void)ignored;
            ::_exit(1);
        }
        ::_exit(0);
    }
    ::close(pipe_fds[0]);
    Fd ready(pipe_fds[1]);
    if (::setsid() < 0) system_error("setsid");
    const pid_t second = ::fork();
    if (second < 0) system_error("second fork");
    if (second > 0) ::_exit(0);
    ::umask(0077);
    if (::chdir("/") < 0) system_error("chdir /");
    close_inherited(lock, ready.get());
    Fd null(::open("/dev/null", O_RDWR));
    if (null.get() < 0) system_error("open /dev/null");
    for (int fd = 0; fd <= 2; ++fd) {
        if (::dup2(null.get(), fd) < 0) system_error("dup2 /dev/null");
    }
    return ready;
}

std::string event_names(std::uint32_t mask) {
    const std::pair<std::uint32_t, const char*> names[] = {
        {IN_ACCESS, "ACCESS"}, {IN_MODIFY, "MODIFY"}, {IN_ATTRIB, "ATTRIB"},
        {IN_CLOSE_WRITE, "CLOSE_WRITE"}, {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
        {IN_OPEN, "OPEN"}, {IN_MOVED_FROM, "MOVED_FROM"}, {IN_MOVED_TO, "MOVED_TO"},
        {IN_CREATE, "CREATE"}, {IN_DELETE, "DELETE"}, {IN_DELETE_SELF, "DELETE_SELF"},
        {IN_MOVE_SELF, "MOVE_SELF"}, {IN_UNMOUNT, "UNMOUNT"},
        {IN_Q_OVERFLOW, "Q_OVERFLOW"}, {IN_IGNORED, "IGNORED"}
    };
    std::string result;
    for (const auto& [bit, name] : names) {
        if ((mask & bit) == 0) continue;
        if (!result.empty()) result += '|';
        result += name;
    }
    return result;
}

class EventLog {
public:
    explicit EventLog(const fs::path& directory)
        : directory_(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) {
        if (directory_.get() < 0) system_error("open event_log_dir");
        const auto probe = ".disk-monitor-probe-" + std::to_string(::getpid());
        Fd file(::openat(directory_.get(), probe.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
        if (file.get() < 0) system_error("event_log_dir is not writable");
        if (::unlinkat(directory_.get(), probe.c_str(), 0) < 0) system_error("remove event log probe");
    }
    void record(std::uint32_t mask, std::uint32_t cookie, const fs::path& path) {
        const auto now = std::chrono::system_clock::now();
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
        if (!::gmtime_r(&time, &utc)) throw std::runtime_error("gmtime_r failed");
        char timestamp[32];
        if (std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
            throw std::runtime_error("strftime failed");
        std::string name;
        Fd file;
        for (;;) {
            name = std::to_string(microseconds) + "-" + std::to_string(::getpid()) + "-" + std::to_string(++sequence_) + ".log";
            file = Fd(::openat(directory_.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
            if (file.get() >= 0) break;
            if (errno != EEXIST) system_error("create event log");
        }
        const auto line = std::string("{\"time\":") + quote(timestamp) +
            ",\"time_us\":" + std::to_string(microseconds) + ",\"pid\":" + std::to_string(::getpid()) +
            ",\"event\":" + quote(event_names(mask)) + ",\"mask\":" + std::to_string(mask) +
            ",\"cookie\":" + std::to_string(cookie) + ",\"is_dir\":" + ((mask & IN_ISDIR) ? "true" : "false") +
            ",\"path\":" + quote(path.string()) + "}";
        write_all(file.get(), line + "\n");
        log_message((mask & IN_Q_OVERFLOW) ? LOG_ERR : LOG_INFO, "event " + line);
    }
private:
    Fd directory_;
    std::uint64_t sequence_ = 0;
};

class Watches {
public:
    explicit Watches(const Config& config) : fd_(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {
        if (fd_.get() < 0) system_error("inotify_init1");
        for (const auto& directory : config.directories) {
            const int wd = ::inotify_add_watch(fd_.get(), directory.c_str(), IN_ALL_EVENTS | IN_ONLYDIR | IN_DONT_FOLLOW);
            if (wd < 0) system_error("inotify_add_watch " + directory.string());
            if (!paths_.emplace(wd, directory).second)
                throw std::runtime_error("Two watch paths refer to the same directory inode");
        }
    }
    int fd() const { return fd_.get(); }
    void consume(EventLog& log) {
        alignas(inotify_event) std::array<char, 65536> buffer{};
        // Ограничиваем чтение, чтобы не задерживать сигналы.
        for (int batch = 0; batch < 16; ++batch) {
            const auto size = ::read(fd_.get(), buffer.data(), buffer.size());
            if (size < 0 && errno == EINTR) continue;
            if (size < 0 && errno == EAGAIN) return;
            if (size <= 0) system_error("read inotify");
            std::size_t offset = 0;
            while (offset < static_cast<std::size_t>(size)) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                const auto next = offset + sizeof(inotify_event) + event->len;
                if (next > static_cast<std::size_t>(size)) throw std::runtime_error("Truncated inotify record");
                if (event->mask & IN_Q_OVERFLOW) {
                    log.record(event->mask, event->cookie, {});
                    log_message(LOG_ERR, "inotify queue overflow: some events were lost; restart required");
                    throw std::runtime_error("inotify queue overflow");
                }
                const auto found = paths_.find(event->wd);
                if (found != paths_.end()) {
                    auto path = found->second;
                    if (event->len > 0) path /= event->name;
                    log.record(event->mask, event->cookie, path);
                    if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED))
                        throw std::runtime_error("Watched directory became unavailable: " + path.string());
                }
                offset = next;
            }
        }
    }
private:
    Fd fd_;
    std::map<int, fs::path> paths_;
};

class SignalSource {
public:
    SignalSource() {
        ::sigemptyset(&mask_);
        for (int signal : {SIGTERM, SIGHUP}) ::sigaddset(&mask_, signal);
        if (::sigprocmask(SIG_BLOCK, &mask_, nullptr) < 0) system_error("sigprocmask");
    }
    Fd open() const {
        Fd result(::signalfd(-1, &mask_, SFD_NONBLOCK | SFD_CLOEXEC));
        if (result.get() < 0) system_error("signalfd");
        return result;
    }
private:
    sigset_t mask_{};
};
}

class Daemon {
public:
    static Daemon& instance() {
        static Daemon value;
        return value;
    }
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    int run(const fs::path& filename) {
        // Запоминаем путь до chdir("/").
        const auto config_path = fs::absolute(filename).lexically_normal();
        Config config = Config::load(config_path);
        {
            EventLog writable(config.event_log_dir);
            Watches available(config);
        }
        SignalSource signals;
        PidFile pid_file(config.pid_file);
        ::closelog();
        Fd ready = detach(pid_file.fd());
        ::openlog("disk-monitor", LOG_PID | LOG_NDELAY, LOG_LOCAL0);
        auto signal_fd = signals.open();
        auto log = std::make_unique<EventLog>(config.event_log_dir);
        auto watches = std::make_unique<Watches>(config);
        pid_file.publish();
        log_message(LOG_INFO, "started config=" + quote(config_path.string()) + " directories=" + std::to_string(config.directories.size()));
        write_all(ready.get(), "R");
        ready = Fd();
        bool stop = false;
        while (!stop) {
            pollfd descriptors[] = {{signal_fd.get(), POLLIN, 0}, {watches->fd(), POLLIN, 0}};
            const int result = ::poll(descriptors, 2, -1);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) system_error("poll");
            for (const auto& descriptor : descriptors) {
                if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP))
                    throw std::runtime_error("Event source became unavailable");
            }
            bool reload = false;
            if (descriptors[0].revents & POLLIN) {
                signalfd_siginfo info{};
                while (::read(signal_fd.get(), &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))) {
                    if (info.ssi_signo == SIGTERM) stop = true;
                    else if (info.ssi_signo == SIGHUP) reload = true;
                }
            }
            if (stop) {
                watches->consume(*log);
                log_message(LOG_INFO, "stopping: SIGTERM received");
                break;
            }
            if (descriptors[1].revents & POLLIN) watches->consume(*log);
            if (reload) {
                Config replacement;
                std::unique_ptr<EventLog> next_log;
                std::unique_ptr<Watches> next_watches;
                try {
                    replacement = Config::load(config_path);
                    if (replacement.pid_file != config.pid_file)
                        throw std::runtime_error("pid_file cannot change during reload");
                    // Заменяем настройки только после проверки.
                    next_log = std::make_unique<EventLog>(replacement.event_log_dir);
                    next_watches = std::make_unique<Watches>(replacement);
                } catch (const std::exception& error) {
                    log_message(LOG_ERR, "reload rejected; previous configuration kept: " + std::string(error.what()));
                    continue;
                }
                watches->consume(*log);
                config = std::move(replacement);
                log = std::move(next_log);
                watches = std::move(next_watches);
                log_message(LOG_INFO, "configuration reloaded directories=" + std::to_string(config.directories.size()));
            }
        }
        return 0;
    }
private:
    Daemon() = default;
};

int main(int argc, char** argv) {
    for (int fd = 0; fd <= 2; ++fd) {
        if (::fcntl(fd, F_GETFD) < 0 && errno == EBADF) {
            if (::open("/dev/null", O_RDWR) < 0) return 1;
        }
    }
    ::openlog("disk-monitor", LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_LOCAL0);
    int result = 0;
    try {
        if (argc > 2) throw std::runtime_error("Usage: disk-monitor [configuration]");
        const fs::path config = argc == 2 ? argv[1] : "disk-monitor.conf";
        result = Daemon::instance().run(config);
    } catch (const std::exception& error) {
        log_message(LOG_ERR, "fatal: " + std::string(error.what()));
        result = 1;
    }
    ::closelog();
    return result;
}
