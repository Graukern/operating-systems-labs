#pragma once

#include <filesystem>
#include <string>

#include <sys/types.h>

// Отдельный компонент PID-файла. Lock-файл сериализует запуски и замену демона.
// PID-файл содержит PID (1-я строка) и /proc/<pid>/stat starttime (2-я строка).
class PidFile final {
public:
    explicit PidFile(const std::filesystem::path& configPath);
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    // Вызывается до fork(): блокируем параллельные старты, проверяем предыдущий PID,
    // посылаем SIGTERM только подтверждённому предыдущему экземпляру.
    void acquireAndStopPrevious();

    // Вызывается в новом рабочем процессе: записывает собственный PID атомарно.
    void writeCurrent();

    // После записи PID отпускаем блокировку, чтобы другие старты не зависали.
    void unlock();

    // При штатном завершении удаляем PID-файл, только если он всё ещё наш.
    void removeIfOwned() noexcept;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::filesystem::path lockPath_;
    int lockFd_ = -1;
    pid_t ownerPid_ = -1;
    std::string ownerStart_;
};
