#!/bin/bash
set -euo pipefail

usage() {
    cat >&2 <<EOF
Usage: $0 <config> <date> <time> [flag] <text>
  date : YYYY-MM-DD
  time : HH:MM:SS
  flag : -h (hourly), -d (daily), -w (weekly), omit for one-time
Env:
  REMINDER_CONFIG      override config path
  REMINDERD_PIDFILE    override pid file
EOF
    exit 1
}

[ $# -ge 4 ] || usage

CONFIG="${REMINDER_CONFIG:-$1}"
DATE="$2"
TIME="$3"
shift 3

FLAG=""
if [[ "$1" == -* ]]; then
    FLAG="$1"
    shift
    if [[ "$FLAG" != "-h" && "$FLAG" != "-d" && "$FLAG" != "-w" ]]; then
        echo "Error: invalid flag '$FLAG' (allowed: -h, -d, -w)" >&2
        exit 1
    fi
fi

TEXT="$*"
TRIMMED="${TEXT#"${TEXT%%[![:space:]]*}"}"
TRIMMED="${TRIMMED%"${TRIMMED##*[![:space:]]}"}"
[ -n "$TRIMMED" ] || { echo "Error: empty reminder text" >&2; exit 1; }

if ! date -d "$DATE $TIME" >/dev/null 2>&1; then
    echo "Error: invalid date/time '$DATE $TIME'" >&2
    exit 1
fi
NORMALIZED=$(date -d "$DATE $TIME" +"%Y-%m-%d %H:%M:%S")
if [ "$NORMALIZED" != "$DATE $TIME" ]; then
    echo "Error: date/time '$DATE $TIME' is not a real calendar time (normalized to '$NORMALIZED')" >&2
    exit 1
fi

[ -f "$CONFIG" ] || { echo "Error: config file not found: $CONFIG" >&2; exit 1; }

LINE="$DATE $TIME"
[ -n "$FLAG" ] && LINE="$LINE $FLAG"
LINE="$LINE $TEXT"

ORIG_MODE=$(stat -c '%a' "$CONFIG")
TMP=$(mktemp "${CONFIG}.XXXXXX")
trap 'rm -f "$TMP"' EXIT
if grep -q '^\[events\]' "$CONFIG"; then
    awk -v line="$LINE" '/^\[events\]/ { print; print line; next } { print }' \
        "$CONFIG" > "$TMP"
else
    cp "$CONFIG" "$TMP"
    printf '\n[events]\n%s\n' "$LINE" >> "$TMP"
fi
chmod "$ORIG_MODE" "$TMP"
mv "$TMP" "$CONFIG"
trap - EXIT

PIDFILE="${REMINDERD_PIDFILE:-/tmp/reminderd_$(id -u).pid}"
if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE" 2>/dev/null || true)
    if [ -n "$PID" ] && [ -d "/proc/$PID" ]; then
        COMM=$(cat "/proc/$PID/comm" 2>/dev/null || true)
        if [ "$COMM" = "reminderd" ]; then
            kill -HUP "$PID" 2>/dev/null || true
        else
            echo "Warning: PID $PID is not reminderd (comm='$COMM') — SIGHUP not sent" >&2
        fi
    fi
fi

echo "Event added to $CONFIG"