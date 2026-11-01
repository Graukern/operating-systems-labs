#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

cleanup() {
    rm -f "$SCRIPT_DIR"/*.o
    rm -f "$SCRIPT_DIR/reminderd.tmp"
}
trap cleanup EXIT

# Обратите внимание на добавленные пути src/
g++ -std=c++17 -Wall -Wextra -Werror -O2 -o reminderd.tmp \
    src/Daemon.cpp src/Event.cpp src/main.cpp
mv reminderd.tmp reminderd

echo "Build successful: $SCRIPT_DIR/reminderd"