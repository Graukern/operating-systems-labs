#!/bin/bash
set -e

TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT
cp reminderd add_event.sh "$TEST_DIR/"
cd "$TEST_DIR"

cat > fake_term.sh <<'STUB'
#!/bin/bash
echo "TRIGGER" >> "$FAKE_TERM_LOG"
STUB
chmod +x fake_term.sh
export FAKE_TERM_LOG="$TEST_DIR/triggers.log"
: > "$FAKE_TERM_LOG"

write_config() {
    local extra="$1"
    cat > config.ini <<EOF
[settings]
interval = 2
pid_file = $TEST_DIR/reminderd.pid
terminal = $TEST_DIR/fake_term.sh

[events]
$extra
EOF
}

write_config ""
./reminderd config.ini
sleep 1
[ -f reminderd.pid ] || { echo "FAIL: pid file not created"; exit 1; }
PID1=$(cat reminderd.pid)
kill -0 "$PID1" 2>/dev/null || { echo "FAIL: daemon not running"; exit 1; }

PERM=$(stat -c '%a' reminderd.pid)
[ "$PERM" = "600" ] || { echo "FAIL: pid file permissions $PERM, expected 600"; exit 1; }

./reminderd config.ini
sleep 1
PID2=$(cat reminderd.pid)
[ "$PID1" != "$PID2" ] || { echo "FAIL: old daemon not killed"; exit 1; }
kill -0 "$PID1" 2>/dev/null && { echo "FAIL: old daemon alive"; exit 1; }

kill -HUP "$PID2"
sleep 1
kill -0 "$PID2" 2>/dev/null || { echo "FAIL: died after SIGHUP"; exit 1; }

./add_event.sh config.ini "$(date +%Y-%m-%d)" "$(date +%H:%M:%S)" -h "Test event"
grep -q "Test event" config.ini || { echo "FAIL: event not added"; exit 1; }

if ./add_event.sh config.ini "$(date +%Y-%m-%d)" "$(date +%H:%M:%S)" -h "" 2>/dev/null; then
    echo "FAIL: empty text accepted"; exit 1
fi
if ./add_event.sh config.ini "$(date +%Y-%m-%d)" "$(date +%H:%M:%S)" -h "   " 2>/dev/null; then
    echo "FAIL: whitespace text accepted"; exit 1
fi
if ./add_event.sh config.ini "2025-02-30" "12:00:00" "bad" 2>/dev/null; then
    echo "FAIL: 2025-02-30 accepted"; exit 1
fi
if ./add_event.sh config.ini "2025-01-01" "25:00:00" "bad" 2>/dev/null; then
    echo "FAIL: 25:00:00 accepted"; exit 1
fi

echo $$ > /tmp/fake_reminderd.pid
OUTPUT=$(REMINDERD_PIDFILE=/tmp/fake_reminderd.pid ./add_event.sh config.ini \
         "$(date +%Y-%m-%d)" "$(date +%H:%M:%S)" "foreign" 2>&1)
rm -f /tmp/fake_reminderd.pid
echo "$OUTPUT" | grep -q "not reminderd" || { echo "FAIL: no warning for foreign PID"; exit 1; }

kill -TERM "$PID2"
sleep 1
kill -0 "$PID2" 2>/dev/null && { echo "FAIL: daemon didn't terminate"; exit 1; }
[ -f reminderd.pid ] && { echo "FAIL: pid file remains"; exit 1; }

: > "$FAKE_TERM_LOG"
PAST_DATE=$(date -d '3 days ago' +%Y-%m-%d)
PAST_TIME=$(date -d '3 days ago' +%H:%M:%S)
write_config "$PAST_DATE $PAST_TIME -d Missed daily"
./reminderd config.ini
sleep 6
PID3=$(cat reminderd.pid)
kill -TERM "$PID3" 2>/dev/null || true
sleep 1
TRIGGERS=$(wc -l < "$FAKE_TERM_LOG" || echo 0)
[ "$TRIGGERS" -le 1 ] || { echo "FAIL: storm detected ($TRIGGERS triggers)"; exit 1; }

echo "All tests passed"