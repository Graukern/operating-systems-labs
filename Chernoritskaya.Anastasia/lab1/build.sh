#!/bin/bash
# Сборка демона в один исполняемый файл ./diskmon, промежуточные файлы удаляются
set -e
cd "$(dirname "$0")"
OBJ=$(mktemp -d)
trap 'rm -rf "$OBJ"' EXIT
FLAGS="-std=c++17 -Wall -Werror -Isrc"
for f in src/*.cpp; do
    g++ $FLAGS -c "$f" -o "$OBJ/$(basename "${f%.cpp}").o"
done
g++ "$OBJ"/*.o -o diskmon
echo "Built: ./diskmon"