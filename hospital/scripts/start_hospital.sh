#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : start_hospital.sh
# Group   : Group XX
# Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
# Date    : 2026-04-01
# Purpose : Initialize IPC resources and launch the admissions
#           manager process in the background.
# Usage   : ./start_hospital.sh [--strategy best|first|worst]
# ============================================================

set -euo pipefail

STRATEGY="${2:-best}"
PID_FILE="/tmp/hospital_admissions.pid"
FIFO="/tmp/discharge_fifo"
LOG_DIR="logs"

# ── Banner ──
echo ""
echo "╔══════════════════════════════════════════╗"
echo "║    HOSPITAL PATIENT TRIAGE SYSTEM       ║"
echo "║         STARTING UP...                  ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "  ICU Beds       : 4  (care units = 3 each)"
echo "  Isolation Beds : 4  (care units = 2 each)"
echo "  General Beds   : 12 (care units = 1 each)"
echo "  Total Beds     : 20"
echo "  Total Units    : $(( 4*3 + 4*2 + 12*1 ))"
echo "  Strategy       : $STRATEGY"
echo ""

# ── Check if already running ──
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "[ERROR] Admissions manager already running (PID $OLD_PID)."
        echo "        Run ./stop_hospital.sh first."
        exit 1
    fi
fi

# ── Create log directory ──
mkdir -p "$LOG_DIR"

# ── Build if not already built ──
if [ ! -f "./admissions" ] || [ ! -f "./patient_simulator" ]; then
    echo "[START] Building project..."
    make all
fi

# ── Create named FIFO ──
if [ ! -p "$FIFO" ]; then
    mkfifo "$FIFO"
    echo "[START] Created FIFO: $FIFO"
fi

# ── Remove stale semaphores (if any) ──
for sem in /sem_icu_limit /sem_isolation_limit /sem_queue_slots; do
    python3 -c "
import posix_ipc, sys
try:
    posix_ipc.unlink_semaphore('$sem')
except: pass
" 2>/dev/null || true
done

# ── Remove stale shared memory ──
SHM_KEY_HEX="0xBEDF00D"
SHM_ID=$(ipcs -m | awk -v key="$SHM_KEY_HEX" 'tolower($1)==tolower(key){print $2}' 2>/dev/null || echo "")
if [ -n "$SHM_ID" ]; then
    ipcrm -m "$SHM_ID" 2>/dev/null || true
    echo "[START] Removed stale shared memory segment $SHM_ID"
fi

# ── Launch admissions manager ──
echo "[START] Launching admissions manager..."
./admissions --strategy "$STRATEGY" &
ADMISSIONS_PID=$!
echo "$ADMISSIONS_PID" > "$PID_FILE"
echo "[START] Admissions manager started — PID: $ADMISSIONS_PID"
echo "[START] Hospital is OPEN. Use ./scripts/triage.sh to admit patients."
echo ""

g
