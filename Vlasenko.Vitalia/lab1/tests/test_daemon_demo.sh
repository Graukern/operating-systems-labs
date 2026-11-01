#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Тест 1: форк и создание нового сеанса.
# Условия: запускаем короткоживущего потомка и получаем его сведения из отчёта.
# Ожидаем: PID == PGID == SID, процесс существует и не имеет управляющего TTY.
#
# Тест 2: смена рабочего каталога.
# Ожидаем: после chdir("/") каталог процесса равен '/'.
#
# Тест 3: перенаправление стандартных потоков.
# Ожидаем: файловые дескрипторы 0, 1 и 2 у потомка ведут в /dev/null.
# При этом вывод родителя в терминал не должен ломаться.
test_dir="$(mktemp -d)"
child_pid=""
cleanup() {
    if [[ -n "$child_pid" && "$child_pid" =~ ^[0-9]+$ ]]; then
        kill -TERM "$child_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

./bin/sorter-test --daemon-demo "$test_dir/report" 10 >/dev/null

# Потомок пишет отчёт асинхронно: ждём появления всех нужных строк.
for ((attempt=0; attempt<100; ++attempt)); do
    if [[ -f "$test_dir/report" ]] &&
       grep -q '^STDERR=' "$test_dir/report"; then
        break
    fi
    sleep 0.05
done
[[ -f "$test_dir/report" ]] && grep -q '^STDERR=' "$test_dir/report" || {
    echo '[FAIL] daemon did not write complete report'; exit 1;
}

get_value() { sed -n "s/^$1=//p" "$test_dir/report"; }
child_pid="$(get_value PID)"
process_group="$(get_value PGID)"
session_id="$(get_value SID)"

[[ "$child_pid" =~ ^[0-9]+$ ]] || { echo '[FAIL] invalid PID'; exit 1; }
[[ "$child_pid" == "$process_group" && "$child_pid" == "$session_id" ]] || {
    echo '[FAIL] setsid did not create session/group'; exit 1;
}
kill -0 "$child_pid" || { echo '[FAIL] child no longer exists'; exit 1; }
tty="$(ps -p "$child_pid" -o tty= | tr -d '[:space:]')"
[[ "$tty" == "?" ]] || { echo "[FAIL] unexpected controlling TTY: $tty"; exit 1; }
echo '[PASS] fork(), setsid(), PID/PGID/SID, no controlling TTY'

[[ "$(get_value CWD)" == "/" ]] || {
    echo "[FAIL] daemon working directory is not /"; exit 1;
}
echo '[PASS] chdir("/") changes daemon working directory'

for descriptor in STDIN STDOUT STDERR; do
    [[ "$(get_value "$descriptor")" == "/dev/null" ]] || {
        echo "[FAIL] $descriptor is not redirected to /dev/null"; exit 1;
    }
done
echo '[PASS] stdin, stdout, stderr are redirected to /dev/null'
