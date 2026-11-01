#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ "$(uname -s)" != Linux ]; then
    echo 'Linux is required.' >&2
    exit 1
fi
temporary=$(mktemp ./disk-monitor.build.XXXXXX)
trap 'rm -f "$temporary"' EXIT HUP INT TERM
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/config.cpp src/monitor.cpp -o "$temporary"
chmod 755 "$temporary"
mv -f "$temporary" disk-monitor
echo 'Built: disk-monitor'
