#!/bin/bash


PID_FILE="/tmp/hospital_admissions.pid"
FIFO="/tmp/discharge_fifo"

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║    HOSPITAL PATIENT TRIAGE SYSTEM        ║"
echo "║         SHUTTING DOWN...                 ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# ── Send SIGTERM to admissions manager ──
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
        echo "[STOP] Sending SIGTERM to admissions (PID $PID)..."
        kill -SIGTERM "$PID"
        sleep 2
        # Force kill if still running
        if kill -0 "$PID" 2>/dev/null; then
            echo "[STOP] Force killing admissions..."
            kill -9 "$PID" 2>/dev/null || true
        fi
        echo "[STOP] Admissions manager stopped."
    else
        echo "[STOP] Admissions manager not running."
    fi
    rm -f "$PID_FILE"
else
    echo "[STOP] No PID file found. Is the hospital running?"
fi

# ── Remove named FIFO ──
if [ -p "$FIFO" ]; then
    rm -f "$FIFO"
    echo "[STOP] Removed FIFO: $FIFO"
fi

# ── Remove shared memory segment ──
SHM_KEY_HEX="0xbedf00d"
SHM_ID=$(ipcs -m 2>/dev/null | awk -v key="$SHM_KEY_HEX" 'tolower($1)==tolower(key){print $2}')
if [ -n "$SHM_ID" ]; then
    ipcrm -m "$SHM_ID" 2>/dev/null && echo "[STOP] Shared memory removed (id=$SHM_ID)"
else
    echo "[STOP] No shared memory segment found."
fi

# ── Remove named semaphores ──
for sem in /sem_icu_limit /sem_isolation_limit /sem_queue_slots; do
    python3 -c "
import posix_ipc
try:
    posix_ipc.unlink_semaphore('$sem')
    print('[STOP] Semaphore $sem removed.')
except Exception as e:
    print(f'[STOP] Semaphore $sem not found (ok).')
" 2>/dev/null || true
done

# ── Final summary from logs ──
echo ""
echo "─────────────── FINAL SUMMARY ───────────────"
if [ -f "logs/memory_log.txt" ]; then
    echo "Memory events logged: $(wc -l < logs/memory_log.txt) lines"
fi
if [ -f "logs/schedule_log.txt" ]; then
    echo "Schedule log: logs/schedule_log.txt"
    echo "Last scheduling entry:"
    tail -5 logs/schedule_log.txt 2>/dev/null || true
fi
echo "─────────────────────────────────────────────"
echo "[STOP] Hospital is CLOSED. Goodbye."
echo ""

