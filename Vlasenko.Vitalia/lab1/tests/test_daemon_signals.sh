#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

test_dir="$(mktemp -d)"
foreground_pid=""
background_pid=""
cleanup() {
    if [[ -n "$foreground_pid" ]]; then
        kill -TERM "$foreground_pid" 2>/dev/null || true
        wait "$foreground_pid" 2>/dev/null || true
    fi
    if [[ -n "$background_pid" ]]; then
        kill -TERM "$background_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

# Ждём нужное условие не дольше ~4 секунд. Не используем фиксированные
# sleep(15): тест не должен зависеть от того, насколько быстро работает ПК.
wait_for_file() {
    local filename="$1"
    for ((i=0; i<80; ++i)); do
        if [[ -f "$filename" ]]; then
            return 0
        fi
        sleep 0.05
    done
    return 1
}

wait_for_text() {
    local filename="$1"
    local pattern="$2"
    for ((i=0; i<80; ++i)); do
        if grep -qF -- "$pattern" "$filename"; then
            return 0
        fi
        sleep 0.05
    done
    return 1
}

# Проверяем завершение процесса: в некоторых окружениях потомок после выхода
# временно существует как зомби (STAT=Z), пока его родитель не вызовет wait().
is_finished() {
    local pid="$1"
    if ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi
    local state
    state="$(ps -p "$pid" -o stat= 2>/dev/null || true)"
    [[ -z "$state" || "$state" == Z* ]]
}

wait_until_finished() {
    local pid="$1"
    for ((i=0; i<80; ++i)); do
        if is_finished "$pid"; then
            return 0
        fi
        sleep 0.05
    done
    return 1
}

# Тест 1: сигнал SIGHUP приводит к перечитыванию конфигурации.
# Условия: цикл уже обработал файлы по правилу .png -> IMAGES, интервал 15 с.
# После этого меняем правило на .txt -> TEXT и отправляем SIGHUP.
# Ожидаем: программа не завершается и сразу выполняет новый цикл по новому
# правилу (не дожидаясь старых 15 секунд); копии распределяются заново.
mkdir -p "$test_dir/source" "$test_dir/destination"
printf 'png contents\n' > "$test_dir/source/a.png"
printf 'txt contents\n' > "$test_dir/source/b.txt"
cat > "$test_dir/config.txt" <<'CONFIG'
interval=15
source destination .png IMAGES
CONFIG

./bin/sorter-test --loop "$test_dir/config.txt" 20 > "$test_dir/output.log" 2>&1 &
foreground_pid=$!
wait_for_text "$test_dir/output.log" '[CYCLE] 1/20 completed' || {
    echo '[FAIL] Initial cycle did not complete'; cat "$test_dir/output.log"; exit 1;
}
[[ -f "$test_dir/destination/IMAGES/a.png" ]] || {
    echo '[FAIL] Initial sorting did not put PNG into IMAGES'; exit 1;
}
cat > "$test_dir/config.new" <<'CONFIG'
interval=1
source destination .txt TEXT
CONFIG
mv -- "$test_dir/config.new" "$test_dir/config.txt"
kill -HUP "$foreground_pid"
wait_for_text "$test_dir/output.log" '[INFO] SIGHUP received: configuration reloaded' || {
    echo '[FAIL] SIGHUP did not reload the configuration'; cat "$test_dir/output.log"; exit 1;
}
wait_for_file "$test_dir/destination/TEXT/b.txt" || {
    echo '[FAIL] SIGHUP did not apply the new .txt rule promptly'; exit 1;
}
[[ -f "$test_dir/destination/OTHERS/a.png" ]] || {
    echo '[FAIL] Old PNG should now be in OTHERS'; exit 1;
}
[[ ! -d "$test_dir/destination/IMAGES" ]] || {
    echo '[FAIL] Old destination category was not cleared'; exit 1;
}
echo '[PASS] SIGHUP reloads rules and schedules an immediate new cycle'

# Тест 2: ошибочная конфигурация при SIGHUP не уничтожает старые настройки.
# Условия: подменяем конфиг вариантом с некорректным interval, отправляем HUP.
# Ожидаем: в выводе есть ошибка перезагрузки; программа остаётся живой,
# и после следующего цикла продолжает раскладывать .txt в папку TEXT.
cat > "$test_dir/config.new" <<'CONFIG'
interval=wrong
source destination .png BROKEN
CONFIG
mv -- "$test_dir/config.new" "$test_dir/config.txt"
kill -HUP "$foreground_pid"
wait_for_text "$test_dir/output.log" '[ERROR] SIGHUP reload failed; keeping previous configuration' || {
    echo '[FAIL] Invalid reload error was not reported'; cat "$test_dir/output.log"; exit 1;
}
printf 'new txt\n' > "$test_dir/source/new.txt"
wait_for_file "$test_dir/destination/TEXT/new.txt" || {
    echo '[FAIL] Previous valid rule was not preserved after bad reload'; exit 1;
}
kill -0 "$foreground_pid" 2>/dev/null || {
    echo '[FAIL] Invalid configuration terminated the process'; exit 1;
}
echo '[PASS] Invalid SIGHUP retains previously loaded configuration'

# Тест 3: SIGTERM корректно завершает основной цикл во время ожидания.
# Условия: после успешной перезагрузки цикл ждёт до следующего запуска.
# Ожидаем: завершение за разумное время, статус 0, сообщение о SIGTERM.
kill -TERM "$foreground_pid"
wait_until_finished "$foreground_pid" || {
    echo '[FAIL] SIGTERM did not stop the foreground process in time'; exit 1;
}
if ! wait "$foreground_pid"; then
    echo '[FAIL] SIGTERM did not end with successful exit status'; exit 1
fi
foreground_pid=""
wait_for_text "$test_dir/output.log" '[INFO] SIGTERM received: daemon is shutting down' || {
    echo '[FAIL] Graceful SIGTERM message missing'; exit 1;
}
echo '[PASS] SIGTERM stops foreground loop cleanly during sleep'

# Тест 4: обработчики сигналов работают и ПОСЛЕ fork()/setsid().
# Условия: запускаем цикл в фоне с интервалом 15 секунд; после первого
# выполнения меняем правило и отправляем потомку SIG HUP, затем SIG TERM.
# Ожидаем: файл будет обработан по новому правилу, потомок корректно завершится.
bg_dir="$test_dir/background"
mkdir -p "$bg_dir/source" "$bg_dir/dest"
printf 'image\n' > "$bg_dir/source/photo.png"
printf 'text\n' > "$bg_dir/source/report.txt"
cat > "$bg_dir/config.txt" <<'CONFIG'
interval=15
source dest .png IMAGES
CONFIG

bg_output="$(./bin/sorter-test --loop "$bg_dir/config.txt" 20 --background)"
[[ "$bg_output" =~ ^Started\ background\ child,\ PID=([0-9]+)$ ]] || {
    echo "[FAIL] Background child PID is missing: $bg_output"; exit 1;
}
background_pid="${BASH_REMATCH[1]}"
wait_for_file "$bg_dir/dest/IMAGES/photo.png" || {
    echo '[FAIL] Background first cycle did not copy PNG'; exit 1;
}
cat > "$bg_dir/config.new" <<'CONFIG'
interval=15
source dest .txt TEXT
CONFIG
mv -- "$bg_dir/config.new" "$bg_dir/config.txt"
kill -HUP "$background_pid"
wait_for_file "$bg_dir/dest/TEXT/report.txt" || {
    echo '[FAIL] SIGHUP was not handled by detached child'; exit 1;
}
kill -TERM "$background_pid"
wait_until_finished "$background_pid" || {
    echo '[FAIL] Background child did not stop after SIGTERM'; exit 1;
}
background_pid=""
echo '[PASS] SIGHUP and SIGTERM work in detached background child'
