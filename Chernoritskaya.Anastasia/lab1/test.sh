#!/bin/bash
# Сборка и запуск юнит-тестов 
set -e
cd "$(dirname "$0")"
OBJ=$(mktemp -d)
trap 'rm -rf "$OBJ"' EXIT
FLAGS="-std=c++17 -Wall -Werror -Isrc -Itests"
for f in src/*.cpp tests/*.cpp; do
    case "$f" in src/main.cpp) continue;; esac
    g++ $FLAGS -c "$f" -o "$OBJ/$(basename "${f%.cpp}").o"
done
g++ "$OBJ"/*.o -o "$OBJ/tests"
"$OBJ/tests"
