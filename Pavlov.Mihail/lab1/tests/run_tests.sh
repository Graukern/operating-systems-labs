#!/bin/bash
# Тесты демона. Запуск: tests/run_tests.sh

cd "$(dirname "$0")/.."

BIN=build/lab1
PIDFILE=/tmp/lab1_daemon.pid
TMP=$(mktemp -d)
FAILED=0

ok()   { echo "OK   $1"; }
fail() { echo "FAIL $1"; FAILED=1; }

check() {
    local name="$1"
    shift
    if "$@" >/dev/null 2>&1; then ok "$name"; else fail "$name"; fi
}

wait_for() {
    for i in $(seq 1 50); do
        if "$@" >/dev/null 2>&1; then return 0; fi
        sleep 0.1
    done
    return 1
}

pid_changed() { [ -s "$PIDFILE" ] && [ "$(cat "$PIDFILE")" != "$1" ]; }

alive() { kill -0 "$1" 2>/dev/null; }
dead()  { ! kill -0 "$1" 2>/dev/null; }

cleanup() {
    pkill -x lab1 2>/dev/null
    rm -rf "$TMP"
    rm -f "$PIDFILE"
}
trap cleanup EXIT

pkill -x lab1 2>/dev/null
rm -f "$PIDFILE"

# рабочий каталог и конфиг
mkdir -p "$TMP/in/sub1/deep" "$TMP/in/sub2"
touch "$TMP/in/keep.txt" "$TMP/in/keep2.log" "$TMP/in/sub1/a.tmp"
CONF="$TMP/test.conf"
printf 'dir1=%s/in\ninterval=1\n' "$TMP" > "$CONF"

# 1. сборка
check "сборка build.sh" ./build.sh
check "после сборки нет лишних файлов" test "$(ls build)" = "lab1"

# 2. ошибки запуска
$BIN >/dev/null 2>&1
[ $? -eq 1 ] && ok "без аргументов код 1" || fail "без аргументов код 1"

$BIN "$TMP/net_takogo.conf" >/dev/null 2>&1
[ $? -eq 1 ] && ok "нет файла конфига код 1" || fail "нет файла конфига код 1"

printf 'dir1=/tmp\ninterval=abc\n' > "$TMP/bad.conf"
$BIN "$TMP/bad.conf" >/dev/null 2>&1
[ $? -eq 1 ] && ok "битый конфиг код 1" || fail "битый конфиг код 1"
check "при битом конфиге нет pid-файла" test ! -e "$PIDFILE"

printf 'dir2=/tmp\n' > "$TMP/old_key.conf"
$BIN "$TMP/old_key.conf" >/dev/null 2>&1
[ $? -eq 1 ] && ok "неизвестный ключ dir2 отвергается" || fail "неизвестный ключ dir2 отвергается"

# 3. запуск
$BIN "$CONF"
wait_for test -s "$PIDFILE"
PID=$(cat "$PIDFILE")
check "демон запущен" alive "$PID"
check "демон без терминала" test "$(ps -o tty= -p "$PID" | tr -d ' ')" = "?"
check "демон лидер сессии" test "$(ps -o sid= -p "$PID" | tr -d ' ')" = "$PID"
check "рабочая папка демона /" test "$(readlink /proc/"$PID"/cwd)" = "/"

# 4. действие варианта 7
check "подкаталог sub1 удалён" wait_for test ! -e "$TMP/in/sub1"
check "подкаталог sub2 удалён" wait_for test ! -e "$TMP/in/sub2"
check "файл keep.txt не тронут" test -f "$TMP/in/keep.txt"
check "файл keep2.log не тронут" test -f "$TMP/in/keep2.log"

mkdir -p "$TMP/in/newdir/x"
touch "$TMP/in/newdir/x/data"
check "новый подкаталог удалён" wait_for test ! -e "$TMP/in/newdir"
check "файлы в корне dir1 живы" test -f "$TMP/in/keep.txt"

# 5. повторный запуск
$BIN "$CONF"
check "старый экземпляр остановлен" wait_for dead "$PID"
wait_for pid_changed "$PID"
NEW=$(cat "$PIDFILE")
check "новый экземпляр работает" alive "$NEW"
check "работает ровно один демон" test "$(pgrep -xc lab1)" = "1"

# 6. SIGHUP: меняем dir1
mkdir -p "$TMP/other/d1" "$TMP/other/d2"
touch "$TMP/other/file"
printf 'dir1=%s/other\ninterval=1\n' "$TMP" > "$CONF"
kill -HUP "$NEW"
check "SIGHUP: демон удаляет подпапки в новом dir1" wait_for test ! -e "$TMP/other/d1"
check "SIGHUP: файл в новом dir1 жив" test -f "$TMP/other/file"
check "SIGHUP: демон жив" alive "$NEW"

# 7. SIGHUP с битым конфигом: демон должен остаться жить
echo "мусор" >> "$CONF"
kill -HUP "$NEW"
sleep 2
check "битый конфиг при SIGHUP: демон жив" alive "$NEW"

# 8. SIGTERM
kill -TERM "$NEW"
check "SIGTERM: демон завершился" wait_for dead "$NEW"
check "SIGTERM: pid-файл удалён" test ! -e "$PIDFILE"

# 9. остался старый pid-файл от убитого демона
mkdir -p "$TMP/in/late"
printf 'dir1=%s/in\ninterval=1\n' "$TMP" > "$CONF"
$BIN "$CONF"
wait_for test -s "$PIDFILE"
OLD=$(cat "$PIDFILE")
kill -9 "$OLD"
$BIN "$CONF"
wait_for pid_changed "$OLD"
check "запуск после kill -9 работает" alive "$(cat $PIDFILE)"
check "подкаталог late удалён" wait_for test ! -e "$TMP/in/late"
pkill -x lab1

echo
if [ $FAILED -eq 0 ]; then
    echo "Все тесты прошли"
else
    echo "Есть упавшие тесты"
fi
exit $FAILED