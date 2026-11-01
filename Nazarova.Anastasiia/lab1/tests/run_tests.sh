#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic src/config.cpp tests/config_test.cpp -o "$temporary/config-test"
"$temporary/config-test"
if [ "$(uname -s)" != Linux ]; then
    echo 'Integration tests skipped: Linux is required.'
    exit 0
fi
sh ./build.sh
python3 tests/integration.py
