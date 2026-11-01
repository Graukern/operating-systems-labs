#pragma once

#include <filesystem>

// Управляет жизненным циклом фонового процесса.
// Поддерживает SIGHUP/SIGTERM, syslog и PID-файл для постоянного режима.
class Daemon final {
public:
    // Единственный объект Daemon внутри одного процесса (Singleton).
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    // Учебная проверка fork(), setsid(), chdir() и стандартных дескрипторов.
    // reportPath должен быть абсолютным; durationSeconds — 1..30.
    void runDemo(const std::filesystem::path& reportPath,
                 unsigned int durationSeconds);

    // Выполняет несколько циклов сортировки; SIGHUP перечитывает конфигурацию,
    // SIGTERM корректно прерывает выполнение.
    // По умолчанию работает на переднем плане (удобно для отладки).
    // background=true: fork() + setsid() + chdir("/") + /dev/null.
    // Возвращает 0 при успехе и 2, если какое-либо правило завершилось ошибкой.
    // В фоновом режиме родитель немедленно возвращает 0 после успешного fork(),
    // а результат обработки относится уже к дочернему процессу.
    int runCycles(const std::filesystem::path& configPath,
                  unsigned int cycles, bool background);

    // Постоянная служба: в фоне (по умолчанию) или на переднем плане для отладки.
    // Обнаруживает прежний экземпляр по /proc и PID-файлу; заменяет его по SIGTERM.
    int runManaged(const std::filesystem::path& configPath, bool background);

private:
    Daemon() = default;
};
