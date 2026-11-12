#!/bin/bash
cd "$(dirname "$0")/.."

fails=0
out=$(mktemp)

ok() { echo "OK: $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }

check() {
    local name=$1
    shift
    if "$@"; then ok "$name"; else bad "$name"; fi
}

has() { grep -q -E "$1" "$out"; }
leftovers() { [ "$(ls /tmp /dev/mqueue 2>/dev/null | grep -c -E '^lab2_(mq|fifo|sock)_')" -eq 0 ]; }

check "build.sh собирает проект" ./build.sh > /dev/null
check "build.sh убирает папку build" test ! -e build

for src in src/conn_*.cpp; do
    name=$(basename "$src" .cpp)
    type=${name#conn_}
    check "$type: есть исполняемый файл host_$type" test -x "host_$type"
done

for src in src/conn_*.cpp; do
    name=$(basename "$src" .cpp)
    type=${name#conn_}
    echo "--- $type"

    (echo "общее"; sleep 0.5; echo "/p тайное"; sleep 0.5; echo "/q") \
        | LAB2_NOGUI=1 timeout 20 "./host_$type" > "$out" 2>&1
    code=${PIPESTATUS[1]}
    check "$type: хост завершился с кодом 0" test "$code" -eq 0
    check "$type: хост увидел клиента" has "host: клиент подключился"
    check "$type: общее сообщение дошло до хоста" has "host: .*client: общее"
    check "$type: личное сообщение помечено" has "client \(лично\): тайное"
    check "$type: клиент получил приветствие хоста" has "^\[.*\] host: привет"
    check "$type: выход клиента виден хосту" has "собеседник вышел из чата"
    check "$type: файлы соединения удалены" leftovers

    (sleep 6) | LAB2_NOGUI=1 LAB2_IDLE_MS=1500 timeout 20 "./host_$type" > "$out" 2>&1
    check "$type: молчащий клиент отключается" has "клиент долго молчит"
    check "$type: после отключения файлы удалены" leftovers

    LAB2_NOGUI=1 "./host_$type" < <(sleep 6) > "$out" 2>&1 &
    host_pid=$!
    sleep 1.5
    pkill -9 -P "$host_pid"
    wait "$host_pid"
    check "$type: убитый клиент замечен" has "клиент завершился|связь потеряна"
    check "$type: после убитого клиента файлы удалены" leftovers
done

rm -f "$out"
if [ "$fails" -eq 0 ]; then
    echo "все проверки прошли"
else
    echo "не прошло проверок: $fails"
    exit 1
fi
