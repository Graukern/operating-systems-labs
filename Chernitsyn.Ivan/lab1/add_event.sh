#!/bin/sh
set -eu

pid_file=/tmp/reminderd.pid
config="$(cd "$(dirname "$0")" && pwd)/reminder.conf"

usage() {
    echo "usage: $0 [-c config] YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] text" >&2
    exit 1
}

if [ "${1:-}" = "-c" ]; then
    [ $# -ge 2 ] || usage
    config=$2
    shift 2
fi

[ $# -ge 3 ] || usage

day=$1
clock=$2
shift 2

flag=
case "$1" in
-h|-d|-w)
    flag=$1
    shift
    ;;
esac

[ $# -ge 1 ] || usage
text=$(printf '%s' "$*" | tr '\n\t' '  ')

if ! printf '%s' "$day" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' ||
   ! printf '%s' "$clock" | grep -Eq '^[0-9]{2}:[0-9]{2}(:[0-9]{2})?$'; then
    echo "bad date or time: $day $clock" >&2
    exit 1
fi

if ! when=$(date -d "$day $clock" +%s 2>/dev/null); then
    echo "bad date or time: $day $clock" >&2
    exit 1
fi

if [ -z "$flag" ] && [ "$when" -le "$(date +%s)" ]; then
    echo "$day $clock is already in the past" >&2
    exit 1
fi

if [ -s "$config" ] && [ -n "$(tail -c 1 "$config")" ]; then
    echo >> "$config"
fi

if [ -n "$flag" ]; then
    printf '%s %s %s %s\n' "$day" "$clock" "$flag" "$text" >> "$config"
else
    printf '%s %s %s\n' "$day" "$clock" "$text" >> "$config"
fi

echo "added: $day $clock${flag:+ $flag} $text"

if [ -f "$pid_file" ]; then
    pid=$(cat "$pid_file")
    if [ -n "$pid" ] && [ -d "/proc/$pid" ]; then
        kill -HUP "$pid"
        echo "daemon $pid notified"
    fi
fi
