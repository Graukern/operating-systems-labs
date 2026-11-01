#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
temporary=$(mktemp -d .build.XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror \
    src/main.cpp src/config.cpp src/monitor.cpp src/daemon.cpp \
    -o "$temporary/diskmon"
mv -f -- "$temporary/diskmon" diskmon
echo 'Built diskmon (-std=c++17 -Wall -Wextra -Werror)'
