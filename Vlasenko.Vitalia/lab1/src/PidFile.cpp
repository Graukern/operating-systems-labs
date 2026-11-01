#include "PidFile.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
struct ProcInfo {
    std::string startTime;
    char state;
};

// В /proc/<pid>/stat поле comm (имя) заключено в скобки и может содержать пробелы.
// Поэтому нельзя просто разделить всю строку по пробелам: сначала находим ") ".
std::optional<ProcInfo> readProc(pid_t pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(in, line)) {
        return std::nullopt;
    }
    const auto rightParen = line.rfind(") ");
    if (rightParen == std::string::npos) {
        throw std::runtime_error("Bad /proc stat format");
    }
    // После ") " первым полем идёт state (#3); starttime — поле #22,
    // значит, это токен с индексом 19 (при отсчёте от state).
    std::istringstream input(line.substr(rightParen + 2));
    std::string token;
    char state = '?';
    std::string startTime;
    for (int i = 0; i <= 19; ++i) {
        if (!(input >> token)) {
            throw std::runtime_error("Incomplete /proc stat");
        }
        if (i == 0) {
            state = token[0];
        }
        if (i == 19) {
            startTime = token;
        }
    }
    return ProcInfo{startTime, state};
}

struct Entry {
    pid_t pid;
    std::string startTime;
};

std::optional<Entry> readEntry(const fs::path& file) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        if (ec) {
            throw fs::filesystem_error("Cannot check PID file", file, ec);
        }
        return std::nullopt;
    }
    if (fs::is_symlink(file)) {
        throw std::runtime_error("Refusing symlink PID file: " + file.string());
    }
    std::ifstream in(file);
    long long id = 0;
    std::string stamp;
    std::string extra;
    if (!(in >> id >> stamp) || (in >> extra) || id <= 1 ||
        id > static_cast<long long>(std::numeric_limits<pid_t>::max()) ||
        stamp.empty() || stamp.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("Invalid PID file format: " + file.string());
    }
    return Entry{static_cast<pid_t>(id), stamp};
}

bool sameExecutable(pid_t pid) {
    std::error_code ec;
    const auto oldExe = fs::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec);
    if (ec) {
        return false;
    }
    auto oldPath = oldExe.string();
    // Linux дописывает " (deleted)" к /proc/<pid>/exe, когда бинарник
    // заменён новой сборкой, а старый процесс ещё исполняется.
    constexpr const char* deletedSuffix = " (deleted)";
    const std::string suffix(deletedSuffix);
    if (oldPath.size() > suffix.size() &&
        oldPath.compare(oldPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        oldPath.resize(oldPath.size() - suffix.size());
    }
    const auto ownExe = fs::read_symlink("/proc/self/exe", ec);
    return !ec && oldPath == ownExe.string();
}

// Проверяем, что найденный процесс действительно запущен как --daemon.
// Иначе при ручной подмене PID-файла можно было бы остановить наш же
// sorter-test, но запущенный в учебном режиме --loop.
bool isManagedMode(pid_t pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    if (!in) {
        return false;
    }
    std::string executableArg;
    std::string modeArg;
    return static_cast<bool>(std::getline(in, executableArg, '\0')) &&
           static_cast<bool>(std::getline(in, modeArg, '\0')) &&
           modeArg == "--daemon";
}

// По требованиям лабораторной проверяем процесс через /proc.
// PID + starttime + путь к исполняемому файлу + режим --daemon защищают от
// обычного повторного использования PID чужим процессом.
// От злонамеренной подмены файлов в доступном на запись каталоге это не защищает.
bool matchesRunningDaemon(const Entry& old) {
    const auto info = readProc(old.pid);
    return info && info->state != 'Z' && info->state != 'X' &&
           info->startTime == old.startTime && sameExecutable(old.pid) &&
           isManagedMode(old.pid);
}

void writeFully(int fd, const std::string& value) {
    std::size_t done = 0;
    while (done != value.size()) {
        const ssize_t n = write(fd, value.data() + done, value.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            throw std::system_error(errno ? errno : EIO,
                                    std::generic_category(), "write PID file");
        }
        done += static_cast<std::size_t>(n);
    }
}
} // namespace

PidFile::PidFile(const fs::path& configPath) {
    // Абсолютный путь конфига уже сохранён в Config. PID создаём рядом с ним,
    // в каталоге run/; после chdir("/") путь остаётся правильным.
    const fs::path projectDirectory = fs::canonical(configPath.parent_path());
    path_ = projectDirectory / "run" / "sorter.pid";
    lockPath_ = projectDirectory / "run" / "sorter.pid.lock";
}

PidFile::~PidFile() {
    // Открытый descriptor наследуется после fork; закрываем только свою копию.
    if (lockFd_ >= 0) {
        close(lockFd_);
    }
}

void PidFile::acquireAndStopPrevious() {
    fs::create_directories(path_.parent_path());
    lockFd_ = open(lockPath_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lockFd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "open PID lock");
    }
    if (flock(lockFd_, LOCK_EX) < 0) {
        throw std::system_error(errno, std::generic_category(), "flock PID lock");
    }
    const auto previous = readEntry(path_);
    if (!previous) {
        return; // Первый запуск
    }
    const auto proc = readProc(previous->pid);
    if (!proc || proc->state == 'Z' || proc->state == 'X' ||
        proc->startTime != previous->startTime) {
        return; // PID-файл устарел; старый PID исчез или переиспользован.
    }
    if (!sameExecutable(previous->pid)) {
        throw std::runtime_error("PID file refers to another program; refusing SIGTERM");
    }
    if (!isManagedMode(previous->pid)) {
        throw std::runtime_error("PID file refers to a different launch mode; refusing SIGTERM");
    }
    if (previous->pid == getpid()) {
        throw std::runtime_error("PID file unexpectedly refers to the starter");
    }
    if (kill(previous->pid, SIGTERM) < 0) {
        if (errno == ESRCH) {
            return;
        }
        throw std::system_error(errno, std::generic_category(), "SIGTERM old daemon");
    }

    // SIGTERM — просьба завершиться, а не мгновенное уничтожение процесса.
    // Ждём, пока старый экземпляр закончит работу и освободит ресурсы.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!matchesRunningDaemon(*previous)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("Previous daemon did not stop after SIGTERM (10 s)");
}

void PidFile::writeCurrent() {
    const auto proc = readProc(getpid());
    if (!proc) {
        throw std::runtime_error("Cannot read own /proc stat");
    }
    const fs::path temporary = path_.string() + ".tmp." + std::to_string(getpid());
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open temporary PID file");
    }
    try {
        writeFully(fd, std::to_string(getpid()) + "\n" + proc->startTime + "\n");
        if (close(fd) < 0) {
            throw std::system_error(errno, std::generic_category(), "close temporary PID file");
        }
        fs::rename(temporary, path_);
    } catch (...) {
        // Закрытие при ошибке harmless, даже если fd уже закрыт после failed close.
        close(fd);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
    ownerPid_ = getpid();
    ownerStart_ = proc->startTime;
}

void PidFile::unlock() {
    if (lockFd_ >= 0) {
        // flock привязан к open file description: после fork копия lockFd_ у
        // родителя может ещё держать блокировку, пока родитель не вернётся.
        close(lockFd_);
        lockFd_ = -1;
    }
}

void PidFile::removeIfOwned() noexcept {
    if (ownerPid_ <= 0 || ownerPid_ != getpid()) {
        return;
    }
    try {
        const auto stored = readEntry(path_);
        if (stored && stored->pid == ownerPid_ && stored->startTime == ownerStart_) {
            std::error_code ec;
            fs::remove(path_, ec);
        }
    } catch (...) {
        // Деструкторная/финальная очистка не должна аварийно завершать процесс.
    }
    ownerPid_ = -1;
}
