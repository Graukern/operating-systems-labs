#!/usr/bin/env bash
# Usage: ./tests/run_tests.sh [name filter]
set -uo pipefail

LAB=$(cd "$(dirname "$0")/.." && pwd)
BIN=$LAB/diskmon
WORK=$(mktemp -d)
SHIM=$WORK/syslog_capture.so
trap 'pkill -TERM -f "^$BIN " 2>/dev/null; rm -rf "$WORK"' EXIT

fail() { echo "    FAIL: $*"; return 1; }

setup() {
    T=$WORK/$1
    mkdir -p "$T/a" "$T/b" "$T/outside"
    LOG=$T/syslog
    PIDF=$T/diskmon.pid
    CONF=$T/diskmon.conf
    : > "$LOG"
}

conf() { printf '%s\n' "$@" > "$CONF"; }

dm() { LD_PRELOAD=$SHIM DISKMON_SYSLOG_FILE=$LOG "$BIN" -p "$PIDF" "$@"; }

start() {
    dm "$CONF" > "$T/stdout" 2> "$T/stderr" || { cat "$T/stderr"; fail "diskmon did not start"; return 1; }
    PID=$(cat "$PIDF")
    wait_log "started pid=$PID"
}

stop() {
    [ -n "${PID:-}" ] && kill -TERM "$PID" 2>/dev/null
    wait_gone "$PID"
}

alive() { [ -d "/proc/$1" ] && [ "$(awk '{print $3}' "/proc/$1/stat" 2>/dev/null)" != Z ]; }

wait_gone() {
    for _ in $(seq 50); do alive "$1" || return 0; sleep 0.1; done
    fail "process $1 is still running"
}

wait_log() {
    for _ in $(seq 30); do grep -qF -- "$1" "$LOG" && return 0; sleep 0.1; done
    echo "    --- log:"; sed 's/^/    | /' "$LOG"
    fail "no '$1' in log"
}

no_log() {
    sleep "${2:-0.5}"
    if grep -qF -- "$1" "$LOG"; then
        echo "    --- log:"; sed 's/^/    | /' "$LOG"
        fail "unexpected '$1' in log"
    fi
}

test_build() {
    "$LAB/build.sh" > /dev/null || fail "build.sh failed" || return 1
    [ -x "$BIN" ] || fail "no binary"
    [ -z "$(find "$LAB" -name '*.o')" ] || fail "object files left behind"
}

test_daemonize() {
    setup daemonize; conf "watch a"
    start || return 1
    grep -q "diskmon started, pid $PID" "$T/stdout" || fail "no startup message" || return 1
    [ "$(ps -o tty= -p "$PID" | tr -d ' ')" = "?" ] || fail "has a controlling terminal" || return 1
    [ "$(ps -o sid= -p "$PID" | tr -d ' ')" != "$PID" ] || fail "is a session leader" || return 1
    [ "$(readlink "/proc/$PID/cwd")" = / ] || fail "cwd is not /" || return 1
    for fd in 0 1 2; do
        [ "$(readlink "/proc/$PID/fd/$fd")" = /dev/null ] || fail "fd $fd is not /dev/null" || return 1
    done
    stop
}

test_bad_config() {
    setup bad_config
    conf "watch missing"
    dm "$CONF" 2> "$T/stderr" && fail "started with a missing directory" && return 1
    grep -q "missing: No such file or directory" "$T/stderr" || fail "unclear error" || return 1
    conf "watch a" "watch a/../a"
    dm -c "$CONF" 2> "$T/stderr" && fail "accepted a duplicate directory" && return 1
    mkdir "$T/a/sub"; conf "watch a" "watch a/sub"
    dm -c "$CONF" 2> "$T/stderr" && fail "accepted overlapping rules" && return 1
    conf "watch a events=create,bogus"
    dm -c "$CONF" 2> "$T/stderr" && fail "accepted unknown event" && return 1
    grep -q ":1: unknown event 'bogus'" "$T/stderr" || fail "no line number in error" || return 1
    [ ! -e "$PIDF" ] || fail "pid file created"
}

test_file_events() {
    setup file_events; conf "watch a"
    start || return 1
    echo data > "$T/a/f"
    chmod 600 "$T/a/f"
    cat "$T/a/f" > /dev/null
    rm "$T/a/f"
    for e in CREATE MODIFY CLOSE_WRITE ATTRIB OPEN ACCESS CLOSE_NOWRITE DELETE; do
        wait_log "local0.info diskmon[$PID]: event=$e kind=file path=$T/a/f" || return 1
    done
    grep -q '^daemon.info .*started' "$LOG" || fail "daemon messages are not LOG_DAEMON" || return 1
    stop
}

test_recursive() {
    setup recursive; mkdir -p "$T/a/old/deep"; conf "watch a"
    start || return 1
    touch "$T/a/old/deep/f1"
    wait_log "event=CREATE kind=file path=$T/a/old/deep/f1" || return 1
    mkdir "$T/a/new"
    wait_log "event=CREATE kind=dir path=$T/a/new" || return 1
    touch "$T/a/new/f2"
    wait_log "event=CREATE kind=file path=$T/a/new/f2" || return 1
    stop
}

test_non_recursive_and_mask() {
    setup mask; mkdir "$T/b/sub"
    conf "watch b recursive=no events=create,delete"
    start || return 1
    touch "$T/b/sub/hidden"
    echo x > "$T/b/f"
    rm "$T/b/f"
    wait_log "event=DELETE kind=file path=$T/b/f" || return 1
    grep -qF "event=CREATE kind=file path=$T/b/f" "$LOG" || fail "no CREATE" || return 1
    no_log "hidden" 0 || return 1
    no_log "event=MODIFY" 0 || return 1
    stop
}

test_rename_and_move() {
    setup rename; mkdir "$T/a/d"; conf "watch a" "watch b"
    start || return 1
    touch "$T/a/f"
    mv "$T/a/f" "$T/a/g"
    wait_log "event=RENAME kind=file from=$T/a/f to=$T/a/g" || return 1
    mv "$T/a/d" "$T/a/d2"
    wait_log "event=RENAME kind=dir from=$T/a/d to=$T/a/d2" || return 1
    touch "$T/a/d2/inside"
    wait_log "event=CREATE kind=file path=$T/a/d2/inside" || return 1
    mv "$T/a/g" "$T/b/g"
    wait_log "event=RENAME kind=file from=$T/a/g to=$T/b/g" || return 1
    mv "$T/b/g" "$T/outside/g"
    wait_log "event=MOVE_OUT kind=file path=$T/b/g" || return 1
    mv "$T/outside/g" "$T/a/g"
    wait_log "event=MOVE_IN kind=file path=$T/a/g" || return 1
    stop
}

test_exclude() {
    setup exclude; conf 'watch a exclude=*.swp,.git'
    start || return 1
    touch "$T/a/x.swp"
    mkdir "$T/a/.git"; touch "$T/a/.git/index"
    touch "$T/a/visible"
    wait_log "path=$T/a/visible" || return 1
    no_log "path=$T/a/x.swp" 0 || return 1
    no_log "path=$T/a/.git" 0 || return 1
    stop
}

test_special_names() {
    setup names; conf "watch a"
    start || return 1
    touch "$T/a/with space" "$T/a/new"$'\n'"line"
    wait_log "path=\"$T/a/with space\"" || return 1
    wait_log "path=\"$T/a/new\\nline\"" || return 1
    [ "$(grep -c '^line' "$LOG")" -eq 0 ] || fail "newline in a name split the log line"
    stop
}

test_debounce() {
    setup debounce; conf "debounce_ms 300" "watch a"
    start || return 1
    exec 3> "$T/a/log"
    for i in $(seq 50); do echo "$i" >&3; sleep 0.002; done
    exec 3>&-
    wait_log "event=CLOSE_WRITE kind=file path=$T/a/log" || return 1
    [ "$(grep -c 'event=MODIFY' "$LOG")" -eq 1 ] || fail "MODIFY events were not merged" || return 1
    grep -qE "event=MODIFY kind=file path=$T/a/log count=[0-9]+" "$LOG" || fail "no count=" || return 1
    stop
}

test_sighup_reload() {
    setup sighup; conf "watch a"
    start || return 1
    conf "watch a" "watch b"
    kill -HUP "$PID"
    wait_log "config reloaded: rules=2" || return 1
    wait_log "watch $T/b recursive=yes" || return 1
    [ "$(grep -c "watch $T/a " "$LOG")" -eq 1 ] || fail "unchanged rule was re-added" || return 1
    touch "$T/b/f"
    wait_log "event=CREATE kind=file path=$T/b/f" || return 1

    conf "watch b"
    kill -HUP "$PID"
    wait_log "unwatch $T/a" || return 1
    touch "$T/a/ignored"
    no_log "path=$T/a/ignored" || return 1

    conf "watch nowhere"
    kill -HUP "$PID"
    wait_log "reload failed, keeping previous config" || return 1
    touch "$T/b/still"
    wait_log "event=CREATE kind=file path=$T/b/still" || return 1
    stop
}

test_sigusr1_stats() {
    setup stats; conf "watch a"
    start || return 1
    touch "$T/a/1" "$T/a/2"
    wait_log "path=$T/a/2" || return 1
    kill -USR1 "$PID"
    wait_log "stats: uptime=" || return 1
    grep -q "stats: .*rules=1 watches=1 .*CREATE=2" "$LOG" || fail "wrong stats"
    stop
}

test_sigterm() {
    setup sigterm; conf "watch a"
    start || return 1
    kill -TERM "$PID"
    wait_gone "$PID" || return 1
    wait_log "daemon.info diskmon[$PID]: exiting: SIGTERM from pid" || return 1
    [ ! -e "$PIDF" ] || fail "pid file not removed"
}

test_restart_replaces_old() {
    setup restart; conf "watch a"
    start || return 1
    local old=$PID
    start || return 1
    [ "$PID" != "$old" ] || fail "pid did not change" || return 1
    alive "$old" && fail "old instance still running" && return 1
    wait_log "stopping previous instance pid=$old" || return 1
    wait_log "diskmon[$old]: exiting: SIGTERM" || return 1
    [ "$(pgrep -fc "^$BIN -p $PIDF ")" -eq 1 ] || fail "more than one instance" || return 1
    stop
}

test_stale_pid_file() {
    setup stale; conf "watch a"
    sleep 30 & local other=$!
    echo "$other" > "$PIDF"
    start || { kill "$other"; return 1; }
    alive "$other" || fail "killed a foreign process with the same pid" || return 1
    wait_log "points to pid $other which is not diskmon" || return 1
    kill "$other"; stop
}

test_queue_overflow() {
    setup overflow; conf "watch a events=create"
    start || return 1
    local max; max=$(cat /proc/sys/fs/inotify/max_queued_events)
    kill -STOP "$PID"
    (cd "$T/a" && seq $((max + 100)) | xargs touch)
    kill -CONT "$PID"
    wait_log "inotify queue overflow" || return 1
    stop
}

g++ -shared -fPIC -O2 -Wall -Wextra -Werror "$LAB/tests/syslog_capture.cpp" -o "$SHIM" || exit 1

passed=0; failed=()
TESTS=$(grep -oE '^test_[a-z0-9_]+' "$0")
for t in $TESTS; do
    [[ -n "${1:-}" && "$t" != *"$1"* ]] && continue
    echo "[ RUN  ] ${t#test_}"
    PID=
    if "$t"; then
        echo "[  OK  ] ${t#test_}"; passed=$((passed + 1))
    else
        echo "[ FAIL ] ${t#test_}"; failed+=("${t#test_}")
        [ -n "${PID:-}" ] && kill -TERM "$PID" 2>/dev/null
    fi
done

echo
echo "passed: $passed, failed: ${#failed[@]} ${failed[*]:+(${failed[*]})}"
[ ${#failed[@]} -eq 0 ]
