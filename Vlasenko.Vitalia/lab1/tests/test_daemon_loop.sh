#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Важно: тестовые source/destination создаются ТОЛЬКО в mktemp-каталоге.
# Ни реальные файлы пользователя, ни содержимое test_data не удаляются.
test_dir="$(mktemp -d)"
loop_pid=""
cleanup() {
    if [[ -n "$loop_pid" ]]; then
        kill -TERM "$loop_pid" 2>/dev/null || true
        wait "$loop_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

mkdir -p "$test_dir/source/nested" "$test_dir/destination"
printf 'first version\n' > "$test_dir/source/nested/old.png"
cat > "$test_dir/config.txt" <<'CONFIG'
# Интервал 2 секунды используется только в тесте, не в лабораторном режиме.
interval=2
source destination .png IMAGES
CONFIG

# Тест 1: основной цикл действительно повторяет обработку, а не работает 1 раз.
# Условия: запускаем ровно 2 цикла, после первого заменяем исходный файл.
# Ожидаем: после второго цикла старый результат удалён, новый скопирован,
# вложенная структура сохранена и в выводе есть сообщения об ОБОИХ циклах.
started_at_ns="$(date +%s%N)"
timeout 15s ./bin/sorter-test --loop "$test_dir/config.txt" 2 \
    > "$test_dir/loop-output.txt" 2>&1 &
loop_pid=$!

# Дожидаемся завершения первого цикла (строка выводится с flush).
ready="false"
for ((attempt=0; attempt<80; ++attempt)); do
    if grep -qF '[CYCLE] 1/2 completed' "$test_dir/loop-output.txt"; then
        ready="true"
        break
    fi
    sleep 0.025
done
if [[ "$ready" != "true" ]]; then
    echo '[FAIL] First cycle did not complete in time'
    cat "$test_dir/loop-output.txt" || true
    exit 1
fi
[[ -f "$test_dir/destination/IMAGES/nested/old.png" ]] || {
    echo '[FAIL] First cycle did not copy the nested PNG'; exit 1;
}

rm "$test_dir/source/nested/old.png"
mkdir -p "$test_dir/source/new_dir"
printf 'second version\n' > "$test_dir/source/new_dir/new.txt"

if ! wait "$loop_pid"; then
    loop_pid=""
    echo '[FAIL] Loop process returned nonzero exit code'
    cat "$test_dir/loop-output.txt" || true
    exit 1
fi
loop_pid=""
finished_at_ns="$(date +%s%N)"
elapsed_ms=$(( (finished_at_ns - started_at_ns) / 1000000 ))
[[ "$elapsed_ms" -ge 1500 ]] || {
    echo "[FAIL] Iterations ran too soon: ${elapsed_ms} ms"; exit 1;
}

[[ ! -e "$test_dir/destination/IMAGES/nested/old.png" ]] || {
    echo '[FAIL] Old output survived second cycle'; exit 1;
}
[[ "$(cat "$test_dir/destination/OTHERS/new_dir/new.txt")" == 'second version' ]] || {
    echo '[FAIL] New nested TXT is missing or has wrong contents'; exit 1;
}
grep -qF '[CYCLE] 2/2 completed' "$test_dir/loop-output.txt" || {
    echo '[FAIL] Second cycle was not reported'; exit 1;
}
echo '[PASS] Periodic loop: two cycles, refresh, recursive paths'

# Тест 2: в конфиге ошибка — программа должна завершиться ДО очистки цели.
# Условия: неверный интервал и уже существующий файл в destination.
# Ожидаем: ненулевой код возврата, файл назначения остался нетронутым.
printf 'keep me\n' > "$test_dir/destination/sentinel.txt"
cat > "$test_dir/invalid.txt" <<'CONFIG'
interval=not-a-number
source destination .png IMAGES
CONFIG
if ./bin/sorter-test --loop "$test_dir/invalid.txt" 2 > /dev/null 2>&1; then
    echo '[FAIL] Invalid configuration was accepted'; exit 1;
fi
[[ "$(cat "$test_dir/destination/sentinel.txt")" == 'keep me' ]] || {
    echo '[FAIL] Invalid configuration unexpectedly modified destination'; exit 1;
}
echo '[PASS] Invalid config is rejected before copying/deleting'

# Тест 3: ограниченный фоновый цикл продолжает работу после ухода родителя.
# Условия: запускаем ОДИН цикл с --background (ожидания 60 сек нет).
# Ожидаем: потомок скопирует файл из конфига, несмотря на возврат команды.
background_dir="$test_dir/background"
mkdir -p "$background_dir/source" "$background_dir/dest"
printf 'background works\n' > "$background_dir/source/a.png"
cat > "$background_dir/config.txt" <<'CONFIG'
interval=1
source dest .png IMAGES
CONFIG
output="$(./bin/sorter-test --loop "$background_dir/config.txt" 1 --background)"
[[ "$output" == 'Started background child, PID='* ]] || {
    echo '[FAIL] Background mode did not report child PID'; exit 1;
}
background_ready="false"
for ((attempt=0; attempt<80; ++attempt)); do
    if [[ -f "$background_dir/dest/IMAGES/a.png" ]]; then
        background_ready="true"
        break
    fi
    sleep 0.025
done
[[ "$background_ready" == "true" ]] || {
    echo '[FAIL] Background child did not copy file'; exit 1;
}
[[ "$(cat "$background_dir/dest/IMAGES/a.png")" == 'background works' ]] || {
    echo '[FAIL] Background copy content mismatch'; exit 1;
}
echo '[PASS] Background loop: child processes config after parent exits'
