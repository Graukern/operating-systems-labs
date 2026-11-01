#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== 1/2: Unit tests (Event) ==="
g++ -std=c++17 -Wall -Werror -o /tmp/reminderd_test_event \
    tests/test_event.cpp src/Event.cpp
/tmp/reminderd_test_event
rm -f /tmp/reminderd_test_event

echo ""
echo "=== 2/2: Integration tests (Daemon) ==="
./build.sh
cd tests && ./test_daemon.sh && cd ..

echo ""
echo "=== All test suites passed ==="