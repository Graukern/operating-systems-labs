#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
./build.sh
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -fPIC -shared \
    tests/capture.cpp -ldl -o "$temporary/capture.so"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -Isrc \
    tests/singleton.cpp src/config.cpp src/monitor.cpp src/daemon.cpp \
    -o "$temporary/singleton"
"$temporary/singleton"
echo 'PASS: singleton construction, identity, copy and move restrictions'
DISKMON_CAPTURE="$temporary/capture.so" python3 tests/test_diskmon.py
