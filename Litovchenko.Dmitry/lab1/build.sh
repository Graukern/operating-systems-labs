#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-g++}
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror -Iinclude)

OBJDIR=$(mktemp -d)
trap 'rm -rf "$OBJDIR"' EXIT

objs=()
for src in src/*.cpp; do
    obj="$OBJDIR/$(basename "${src%.cpp}").o"
    echo "  CXX  $src"
    "$CXX" "${CXXFLAGS[@]}" -c "$src" -o "$obj"
    objs+=("$obj")
done

echo "  LD   diskmon"
"$CXX" "${objs[@]}" -o diskmon
