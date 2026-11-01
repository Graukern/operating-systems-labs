#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

test_dir="$(mktemp -d)"
child_pids=()
other_pid=""
loop_pid=""
cleanup() {
    for pid in "${child_pids[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    if [[ -n "$other_pid" ]]; then
        kill -TERM "$other_pid" 2>/dev/null || true
        wait "$other_pid" 2>/dev/null || true
    fi
    if [[ -n "$loop_pid" ]]; then
        kill -TERM "$loop_pid" 2>/dev/null || true
        wait "$loop_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

# Помощник: считаем Z (зомби) уже закончившим выполнение процессом.
is_finished() {
    local pid="$1" state
    if ! kill -0 "$pid" 2>/dev/null; then return 0; fi
    state="$(ps -p "$pid" -o stat= 2>/dev/null || true)"
    [[ -z "$state" || "$state" == Z* ]]
}
wait_finished() {
    local pid="$1"
    for ((n=0; n<100; n++)); do
        if is_finished "$pid"; then return 0; fi
        sleep 0.05
    done
    return 1
}
wait_pid_file() {
    local path="$1"
    for ((n=0; n<100; n++)); do
        if [[ -s "$path" ]]; then return 0; fi
        sleep 0.05
    done
    return 1
}

mkdir -p "$test_dir/source" "$test_dir/destination"
printf 'image\n' > "$test_dir/source/photo.png"
printf 'text\n' > "$test_dir/source/note.txt"
cat > "$test_dir/config.txt" <<'CONFIG'
interval=30
source destination .png IMAGES
CONFIG
pidfile="$test_dir/run/sorter.pid"

# Тест 1: первый старт в режиме постоянного демона.
# Условия: PID-файл отсутствует, корректный конфиг в тестовом каталоге.
# Ожидаем: появился PID-файл; /proc подтверждает живой процесс;
# файл рассортирован; PID в stdout совпадает с содержимым PID-файла.
output="$(./bin/sorter-test --daemon "$test_dir/config.txt")"
[[ "$output" =~ PID=([0-9]+) ]] || { echo "[FAIL] No PID: $output"; exit 1; }
first="${BASH_REMATCH[1]}"
child_pids+=("$first")
wait_pid_file "$pidfile" || { echo '[FAIL] PID file missing'; exit 1; }
[[ "$(head -n1 "$pidfile")" == "$first" ]] || { echo '[FAIL] Wrong PID'; exit 1; }
[[ "$(sed -n '2p' "$pidfile")" =~ ^[0-9]+$ ]] || { echo '[FAIL] Missing starttime'; exit 1; }
kill -0 "$first"
for ((n=0; n<100; n++)); do
    if [[ -f "$test_dir/destination/IMAGES/photo.png" ]]; then break; fi
    sleep 0.05
done
[[ -f "$test_dir/destination/IMAGES/photo.png" ]] || { echo '[FAIL] Files not copied'; exit 1; }
echo '[PASS] first daemon start creates PID file and processes files'

# Тест 2: повторный старт с тем же конфигом.
# Условия: предыдущий daemon ещё активен.
# Ожидаем: новый процесс отправляет SIGTERM старому, ждёт его завершения,
# получает новый PID и заменяет запись в PID-файле.
output="$(./bin/sorter-test --daemon "$test_dir/config.txt")"
[[ "$output" =~ PID=([0-9]+) ]] || { echo '[FAIL] No second PID'; exit 1; }
second="${BASH_REMATCH[1]}"
child_pids+=("$second")
[[ "$second" != "$first" ]] || { echo '[FAIL] PID did not change'; exit 1; }
[[ "$(head -n1 "$pidfile")" == "$second" ]] || { echo '[FAIL] PID file not replaced'; exit 1; }
wait_finished "$first" || { echo '[FAIL] Old daemon still running'; exit 1; }
kill -0 "$second"
echo '[PASS] new daemon sends SIGTERM to previous instance and replaces PID'

# Тест 3: PID-файл не меняется после SIGHUP, а правила обновляются.
# Условия: демон уже работает, в источнике есть .png и .txt.
# Изменяем конфигурацию на .txt -> TEXT и отправляем SIGHUP.
# Ожидаем: сортировка выполняется по новому правилу без перезапуска;
# PID остаётся тем же, а старая категория IMAGES больше не содержит photo.png.
cat > "$test_dir/config.txt" <<'CONFIG'
interval=30
source destination .txt TEXT
CONFIG
kill -HUP "$second"
for ((n=0; n<100; n++)); do
    if [[ -f "$test_dir/destination/TEXT/note.txt" &&
          ! -e "$test_dir/destination/IMAGES/photo.png" ]]; then
        break
    fi
    sleep 0.05
done
[[ -f "$test_dir/destination/TEXT/note.txt" ]] || {
    echo '[FAIL] SIGHUP did not apply new sorting rule'; exit 1;
}
[[ ! -e "$test_dir/destination/IMAGES/photo.png" ]] || {
    echo '[FAIL] Old sorting result remained after SIGHUP'; exit 1;
}
[[ "$(head -n1 "$pidfile")" == "$second" ]] || {
    echo '[FAIL] PID changed after SIGHUP'; exit 1;
}
echo '[PASS] SIGHUP reloads rules without changing PID file'

# Тест 4: SIGTERM останавливает зарегистрированный демон.
# Ожидаем: процесс завершается и удаляет только свой PID-файл.
kill -TERM "$second"
wait_finished "$second" || { echo '[FAIL] SIGTERM did not stop daemon'; exit 1; }
for ((n=0; n<100; n++)); do
    if [[ ! -e "$pidfile" ]]; then break; fi
    sleep 0.05
done
[[ ! -e "$pidfile" ]] || { echo '[FAIL] PID file was not removed'; exit 1; }
echo '[PASS] SIGTERM causes graceful PID file cleanup'

# Тест 5: файл содержит PID ранее существовавшего процесса.
# Условия: фиктивные PID и starttime, /proc не содержит этот процесс.
# Ожидаем: программа считает PID-файл устаревшим и регистрирует себя.
mkdir -p "$(dirname "$pidfile")"
printf '99999999\n123456\n' > "$pidfile"
output="$(./bin/sorter-test --daemon "$test_dir/config.txt")"
[[ "$output" =~ PID=([0-9]+) ]] || { echo '[FAIL] Stale replacement failed'; exit 1; }
third="${BASH_REMATCH[1]}"
child_pids+=("$third")
[[ "$(head -n1 "$pidfile")" == "$third" ]] || { echo '[FAIL] Stale entry not replaced'; exit 1; }
kill -TERM "$third"
wait_finished "$third" || { echo '[FAIL] Could not stop after stale PID test'; exit 1; }
echo '[PASS] stale PID file does not block new daemon'

# Тест 6: опасная запись о постороннем процессе.
# Условия: в PID-файле указан реальный sleep с корректным starttime.
# Ожидаем: демон НЕ посылает SIGTERM чужой программе и отказывается
# запускаться до исправления PID-файла.
sleep 40 &
other_pid=$!
start_time="$(awk '{print $22}' "/proc/$other_pid/stat")"
printf '%s\n%s\n' "$other_pid" "$start_time" > "$pidfile"
if ./bin/sorter-test --daemon "$test_dir/config.txt" > "$test_dir/failure.out" 2>&1; then
    echo '[FAIL] Foreign PID was accepted'; exit 1
fi
kill -0 "$other_pid" || { echo '[FAIL] Foreign process was killed'; exit 1; }
[[ "$(head -n1 "$pidfile")" == "$other_pid" ]] || { echo '[FAIL] Foreign PID file was overwritten'; exit 1; }
echo '[PASS] foreign process in PID file is not killed'

# Тест 7: повреждённый PID-файл.
# Условия: вместо PID и starttime в файле мусор.
# Ожидаем: ошибка запуска; никакой сигнал другому процессу не посылается.
printf 'garbage\n' > "$pidfile"
if ./bin/sorter-test --daemon "$test_dir/config.txt" > "$test_dir/failure.out" 2>&1; then
    echo '[FAIL] Corrupted PID file accepted'; exit 1
fi
echo '[PASS] invalid PID file is rejected'

# Тест 8: PID настоящего sorter-test, но НЕ демона (--loop вместо --daemon).
# Условия: запускаем sorter-test в обычном циклическом режиме и записываем в
# PID-файл его реальные PID и starttime из /proc.
# Ожидаем: новый --daemon откажется посылать SIGTERM процессу в режиме --loop;
# PID-файл останется без изменений, а --loop продолжит работу.
./bin/sorter-test --loop "$test_dir/config.txt" 20 > "$test_dir/loop.out" 2>&1 &
loop_pid=$!
loop_start="$(awk '{print $22}' "/proc/$loop_pid/stat")"
printf '%s\n%s\n' "$loop_pid" "$loop_start" > "$pidfile"
if ./bin/sorter-test --daemon "$test_dir/config.txt" > "$test_dir/failure.out" 2>&1; then
    echo '[FAIL] A --loop process was mistaken for the managed daemon'; exit 1
fi
kill -0 "$loop_pid" || { echo '[FAIL] The unrelated --loop process was killed'; exit 1; }
[[ "$(head -n1 "$pidfile")" == "$loop_pid" ]] || {
    echo '[FAIL] PID file was overwritten after detecting --loop'; exit 1;
}
kill -TERM "$loop_pid"
wait "$loop_pid" || true
loop_pid=""
echo '[PASS] same executable in --loop mode is not killed'
