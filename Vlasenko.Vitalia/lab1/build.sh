#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin

# Компилируем все компоненты приложения без промежуточных объектных файлов.
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    src/main.cpp src/FileSorter.cpp src/Config.cpp src/Daemon.cpp src/PidFile.cpp \
    -o bin/sorter-test
printf 'Built bin/sorter-test\n'
