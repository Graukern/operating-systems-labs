#!/bin/bash
# Сборка демона: ./build.sh
# Результат лежит в build/lab1, промежуточные файлы удаляются.

set -e

cd "$(dirname "$0")"
mkdir -p build

for f in src/*.cpp; do
    name=$(basename "$f" .cpp)
    g++ -std=c++17 -Wall -Werror -c "$f" -o "build/$name.o"
done

g++ build/*.o -o build/lab1
rm -f build/*.o

echo "Сборка готова: build/lab1"