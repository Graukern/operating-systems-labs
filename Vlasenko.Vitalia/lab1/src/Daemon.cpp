#include "Daemon.hpp"

#include "Config.hpp"
#include "FileSorter.hpp"
#include "PidFile.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>      // open(), O_RDWR
#include <sys/types.h>  // pid_t
#include <syslog.h>     // openlog(), syslog(), closelog()
#include <unistd.h>     // fork(), setsid(), chdir(), dup2(), close(), sleep(), _exit()

namespace fs = std::filesystem;

namespace {
// Обработчик сигнала НЕ должен читать файлы, выводить в cout или писать в syslog.
// Эти флаги имеют тип, который разрешено изменять из обработчика сигнала.
volatile std::sig_atomic_t stopRequested = 0;
volatile std::sig_atomic_t reloadRequested = 0;

void handleDaemonSignal(int signalNumber) {
    if (signalNumber == SIGTERM) {
        stopRequested = 1;
    } else if (signalNumber == SIGHUP) {
        reloadRequested = 1;
    }
}

// Настраиваем обработчики в том процессе, который запускает основной цикл.
void installSignalHandlers() {
    stopRequested = 0;
    reloadRequested = 0;

    struct sigaction action {};
    action.sa_handler = handleDaemonSignal;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGTERM, &action, nullptr) == -1 ||
        sigaction(SIGHUP, &action, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(),
                                "Cannot install signal handlers");
    }
}

void logInfo(const std::string& message, bool verbose) {
    syslog(LOG_INFO, "%s", message.c_str());
    if (verbose) {
        std::cout << "[INFO] " << message << '\n' << std::flush;
    }
}

void logError(const std::string& message, bool verbose) {
    syslog(LOG_ERR, "%s", message.c_str());
    if (verbose) {
        std::cerr << "[ERROR] " << message << '\n' << std::flush;
    }
}

// Открытие/закрытие syslog привязываем к времени работы основного цикла.
class SyslogSession {
public:
    SyslogSession() { openlog("sorter-daemon", LOG_PID, LOG_DAEMON); }
    ~SyslogSession() { closelog(); }

    SyslogSession(const SyslogSession&) = delete;
    SyslogSession& operator=(const SyslogSession&) = delete;
};

bool redirectStandardStreams() {
    const int nullFd = open("/dev/null", O_RDWR);
    if (nullFd == -1) {
        return false;
    }

    const bool ok =
        dup2(nullFd, STDIN_FILENO) != -1 &&
        dup2(nullFd, STDOUT_FILENO) != -1 &&
        dup2(nullFd, STDERR_FILENO) != -1;

    if (nullFd > STDERR_FILENO) {
        close(nullFd);
    }
    return ok;
}

// Родитель сообщает PID и возвращается в main(); потомок отделяется от TTY.
bool startDetachedChild() {
    const pid_t pid = fork();
    if (pid == -1) {
        throw std::system_error(errno, std::generic_category(), "fork failed");
    }

    if (pid > 0) {
        std::cout << "Started background child, PID=" << pid << '\n';
        std::cout.flush();
        return false;
    }

    if (setsid() == -1) {
        _exit(2);
    }
    if (chdir("/") == -1) {
        _exit(3);
    }
    if (!redirectStandardStreams()) {
        _exit(4);
    }
    return true;
}

// Один цикл: все правила конфигурации. После SIGTERM не начинаем новое правило.
bool processAllRules(const Config& config, bool verbose) {
    bool allSucceeded = true;
    for (const SortRule& rule : config.rules()) {
        if (stopRequested != 0) {
            break;
        }
        try {
            FileSorter::processRule(rule);
        } catch (const std::exception& error) {
            allSucceeded = false;
            logError("Rule " + rule.source.string() + " -> " +
                     rule.destination.string() + ": " + error.what(), verbose);
        }
    }
    return allSucceeded;
}

// Вместо непрерываемого ожидания на десятки секунд проверяем флаги
// не реже чем примерно раз в 100 мс. Между проверками процесс спит.
void waitUntilOrSignal(std::chrono::steady_clock::time_point deadline) {
    using Clock = std::chrono::steady_clock;
    constexpr auto maxPause = std::chrono::milliseconds(100);
    while (stopRequested == 0 && reloadRequested == 0) {
        const auto now = Clock::now();
        if (now >= deadline) {
            return;
        }
        const auto remaining = deadline - now;
        std::this_thread::sleep_for(std::min(remaining, Clock::duration(maxPause)));
    }
}

// Проверяем, что операция очистки назначения не удалит PID-файл или lock.
// Проверка выполняется и при первоначальной загрузке, и после SIGHUP.
void protectPidPaths(const Config& config, const PidFile& pidFile) {
    for (const SortRule& rule : config.rules()) {
        const fs::path relative = pidFile.path().lexically_relative(rule.destination);
        if (!relative.empty() && !relative.is_absolute() &&
            (*relative.begin() != "..")) {
            throw std::runtime_error("Destination may erase daemon PID file: " +
                                     rule.destination.string());
        }
    }
}

// cycles == 0 означает постоянную работу до получения SIGTERM.
// pidFile нужен только в режиме --daemon (проверка новых правил при SIGHUP).
int executeCycles(Config& config, unsigned int cycles, bool verbose,
                  const PidFile* pidFile = nullptr,
                  bool handlersInstalled = false) {
    using Clock = std::chrono::steady_clock;
    if (!handlersInstalled) {
        installSignalHandlers();
    }
    SyslogSession logging;
    logInfo("Daemon loop started, PID=" + std::to_string(getpid()), verbose);

    bool allSucceeded = true;
    unsigned int completed = 0;
    auto nextRunAt = Clock::now();

    while (cycles == 0 || completed < cycles) {
        if (stopRequested != 0) {
            logInfo("SIGTERM received: daemon is shutting down", verbose);
            return 0;
        }

        if (reloadRequested != 0) {
            reloadRequested = 0;
            try {
                if (pidFile != nullptr) {
                    Config fresh(config.path());
                    fresh.load();
                    protectPidPaths(fresh, *pidFile);
                    config = std::move(fresh);
                } else {
                    config.load();
                }
                logInfo("SIGHUP received: configuration reloaded", verbose);
                // Настройки применяем сразу, не ждём старого интервала.
                nextRunAt = Clock::now();
            } catch (const std::exception& error) {
                logError(std::string("SIGHUP reload failed; keeping previous ") +
                         "configuration: " + error.what(), verbose);
            }
        }

        if (stopRequested != 0) {
            continue;
        }
        waitUntilOrSignal(nextRunAt);
        if (stopRequested != 0 || reloadRequested != 0) {
            continue;
        }

        const auto startedAt = Clock::now();
        const bool succeeded = processAllRules(config, verbose);
        allSucceeded = allSucceeded && succeeded;
        ++completed;

        if (verbose) {
            std::cout << "[CYCLE] " << completed << '/'
                      << (cycles == 0 ? "unlimited" : std::to_string(cycles))
                      << (succeeded ? " completed" : " completed with errors")
                      << '\n' << std::flush;
        }
        nextRunAt = startedAt + std::chrono::seconds(config.intervalSeconds());
    }

    if (stopRequested != 0) {
        logInfo("SIGTERM received: daemon is shutting down", verbose);
        return 0;
    }
    logInfo("Configured number of cycles completed", verbose);
    return allSucceeded ? 0 : 2;
}
}  // namespace

Daemon& Daemon::instance() {
    static Daemon singleton;
    return singleton;
}

void Daemon::runDemo(const fs::path& reportPath,
                     unsigned int durationSeconds) {
    if (!reportPath.is_absolute()) {
        throw std::invalid_argument("Report path must be absolute");
    }
    if (durationSeconds < 1 || durationSeconds > 30) {
        throw std::invalid_argument("Demo duration must be between 1 and 30 seconds");
    }

    if (!startDetachedChild()) {
        return;
    }

    // Демонстрационный отчёт из прошлых этапов оставляем без изменений.
    {
        std::ofstream report(reportPath, std::ios::trunc);
        if (!report) {
            _exit(5);
        }
        report << "PID=" << getpid() << '\n'
               << "PPID=" << getppid() << '\n'
               << "PGID=" << getpgrp() << '\n'
               << "SID=" << getsid(0) << '\n'
               << "CWD=" << fs::current_path().string() << '\n'
               << "STDIN=" << fs::read_symlink("/proc/self/fd/0").string() << '\n'
               << "STDOUT=" << fs::read_symlink("/proc/self/fd/1").string() << '\n'
               << "STDERR=" << fs::read_symlink("/proc/self/fd/2").string() << '\n';
        report.flush();
        if (!report) {
            _exit(6);
        }
    }

    sleep(durationSeconds);
    _exit(0);
}

int Daemon::runCycles(const fs::path& configPath,
                      unsigned int cycles, bool background) {
    if (cycles < 1 || cycles > 20) {
        throw std::invalid_argument("Cycle count must be between 1 and 20");
    }

    // Конфигурацию проверяем до fork(); пути становятся абсолютными.
    Config config(configPath);
    config.load();

    if (!background) {
        return executeCycles(config, cycles, true);
    }

    if (!startDetachedChild()) {
        return 0;  // Родитель не ждёт потомка.
    }

    try {
        const int result = executeCycles(config, cycles, false);
        _exit(result);
    } catch (const std::exception& error) {
        // В фоне stderr указывает на /dev/null, но ошибку можно записать в журнал.
        openlog("sorter-daemon", LOG_PID, LOG_DAEMON);
        syslog(LOG_ERR, "Daemon fatal error: %s", error.what());
        closelog();
        _exit(7);
    } catch (...) {
        _exit(7);
    }
}

// Реальный режим --daemon: PID-файл, замена предыдущего экземпляра, постоянный цикл.
// Учебные режимы --loop и --daemon-demo не затрагиваем.
int Daemon::runManaged(const fs::path& configPath, bool background) {
    Config config(configPath);
    config.load();
    PidFile pidFile(config.path());
    protectPidPaths(config, pidFile);

    // Эксклюзивная блокировка на время замены экземпляра (не на всё время работы).
    pidFile.acquireAndStopPrevious();

    int readyPipe[2] = {-1, -1};
    if (background) {
        if (pipe2(readyPipe, O_CLOEXEC) == -1) {
            throw std::system_error(errno, std::generic_category(), "pipe2 failed");
        }
        const pid_t child = fork();
        if (child == -1) {
            const int error = errno;
            close(readyPipe[0]);
            close(readyPipe[1]);
            throw std::system_error(error, std::generic_category(), "fork failed");
        }
        if (child > 0) {
            close(readyPipe[1]);
            // Родителю блокировка больше не нужна. Потомок всё ещё её держит.
            pidFile.unlock();
            char answer[256] = {};
            const ssize_t n = read(readyPipe[0], answer, sizeof(answer) - 1);
            close(readyPipe[0]);
            if (n <= 0 || std::string(answer, static_cast<std::size_t>(n)) != "READY") {
                throw std::runtime_error("Daemon startup failed: " +
                     (n > 0 ? std::string(answer, static_cast<std::size_t>(n))
                            : "no response from child"));
            }
            std::cout << "Started daemon, PID=" << child
                      << ", PID file=" << pidFile.path() << '\n';
            return 0;
        }
        close(readyPipe[0]);
        if (setsid() == -1 || chdir("/") == -1 || !redirectStandardStreams()) {
            const std::string message = "setsid/chdir/redirect failed";
            (void)write(readyPipe[1], message.data(), message.size());
            close(readyPipe[1]);
            _exit(7);
        }
    }

    // В дочернем (или foreground) процессе уже можно подготовить обработчики
    // до публикации PID: сразу после появления PID-файла SIGTERM будет безопасен.
    try {
        installSignalHandlers();
        pidFile.writeCurrent();
        pidFile.unlock();
        if (background) {
            (void)write(readyPipe[1], "READY", 5);
            close(readyPipe[1]);
            readyPipe[1] = -1;
        }
        const int result = executeCycles(config, 0, !background, &pidFile, true);
        pidFile.removeIfOwned();
        if (background) {
            _exit(result);
        }
        return result;
    } catch (const std::exception& error) {
        pidFile.removeIfOwned();
        if (background) {
            // Сообщаем причину родителю через канал, пока он ещё открыт.
            if (readyPipe[1] >= 0) {
                (void)write(readyPipe[1], error.what(),
                            std::min(std::char_traits<char>::length(error.what()),
                                     static_cast<std::size_t>(200)));
                close(readyPipe[1]);
            }
            openlog("sorter-daemon", LOG_PID, LOG_DAEMON);
            syslog(LOG_ERR, "Daemon fatal error: %s", error.what());
            closelog();
            _exit(7);
        }
        throw;
    }
}
