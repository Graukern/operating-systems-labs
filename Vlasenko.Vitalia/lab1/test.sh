#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin

# Собираем само приложение: тесты должны запускаться и после удаления bin/.
./build.sh

# Автотесты сортировки файлов (без конфигурации).
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    src/FileSorter.cpp tests/test_filesorter.cpp \
    -o bin/test_filesorter
./bin/test_filesorter

# Автотесты чтения и проверки конфигурации + совместной работы с FileSorter.
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    src/Config.cpp src/FileSorter.cpp tests/test_config.cpp \
    -o bin/test_config
./bin/test_config

# Интеграционные тесты Linux: процессы, сеанс, файловые дескрипторы.
bash tests/test_daemon_demo.sh

# Интеграционные тесты ограниченного основного цикла.
bash tests/test_daemon_loop.sh

# Интеграционные тесты SIGHUP/SIGTERM для обычного и фонового режимов.
bash tests/test_daemon_signals.sh

# Интеграционные тесты нового постоянного режима: PID-файл и замена экземпляра.
bash tests/test_daemon_pid.sh
