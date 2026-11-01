#!/bin/sh
set -eu

cd "$(dirname "$0")"

build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT INT TERM

case "${1:-}" in
"")
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$build_dir" -j
    cp "$build_dir/reminderd" .
    echo "built ./reminderd"
    ;;
test)
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
    cmake --build "$build_dir" -j
    cd "$build_dir"
    ctest --output-on-failure
    ;;
*)
    echo "usage: $0 [test]" >&2
    exit 1
    ;;
esac
