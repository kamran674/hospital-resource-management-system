#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : triage.sh
# Group   : Group XX
# Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
# Date    : 2026-04-01
# Purpose : Validate input, compute triage priority (1-5),
#           build a PatientRecord and pipe it to admissions.
# Usage   : ./triage.sh <name> <age> <severity 1-10> [--infectious]
# ============================================================

set -euo pipefail

usage() {
    echo "Usage: $0 <name> <age> <severity 1-10> [--infectious]"
    echo "  name      : patient full name (no spaces — use underscore)"
    echo "  age       : integer 0-120"
    echo "  severity  : integer 1-10 (1=mild, 10=critical)"
    echo "  --infectious : flag for isolation requirement"
    exit 1
}

# ── Argument count check ──
if [ $# -lt 3 ]; then usage; fi

NAME="$1"
AGE="$2"
SEVERITY="$3"
INFECTIOUS=0
if [ "${4:-}" = "--infectious" ]; then INFECTIOUS=1; fi

# ── Validate name (non-empty, no special chars) ──
if [[ -z "$NAME" || ! "$NAME" =~ ^[A-Za-z_]+$ ]]; then
    echo "[ERROR] Invalid name. Use letters and underscores only." >&2
    exit 1
fi

# ── Validate age (numeric, 0-120) ──
if ! [[ "$AGE" =~ ^[0-9]+$ ]] || [ "$AGE" -lt 0 ] || [ "$AGE" -gt 120 ]; then
    echo "[ERROR] Age must be a number between 0 and 120." >&2
    exit 1
fi

# ── Validate severity (numeric, 1-10) ──
if ! [[ "$SEVERITY" =~ ^[0-9]+$ ]] || [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    echo "[ERROR] Severity must be a number between 1 and 10." >&2
    exit 1
fi

# ── Compute triage priority (1-5) ──
# Severity  1-2  → Priority 5 (non-urgent)
# Severity  3-4  → Priority 4 (less urgent)
# Severity  5-6  → Priority 3 (urgent)
# Severity  7-8  → Priority 2 (very urgent)
# Severity  9-10 → Priority 1 (critical)
if   [ "$SEVERITY" -ge 9 ]; then PRIORITY=1
elif [ "$SEVERITY" -ge 7 ]; then PRIORITY=2
elif [ "$SEVERITY" -ge 5 ]; then PRIORITY=3
elif [ "$SEVERITY" -ge 3 ]; then PRIORITY=4
else                              PRIORITY=5
fi

# ── Compute care units based on priority ──
if   [ "$PRIORITY" -le 2 ]; then CARE_UNITS=3   # ICU
elif [ "$INFECTIOUS" -eq 1 ]; then CARE_UNITS=2  # Isolation
else CARE_UNITS=1                                 # General
fi

ARRIVAL=$(date +%s)

# ── Print formatted patient record to STDERR ──
echo "============================================" >&2
echo "  TRIAGE ASSESSMENT" >&2
echo "  Patient  : $NAME" >&2
echo "  Age      : $AGE" >&2
echo "  Severity : $SEVERITY / 10" >&2
echo "  Priority : $PRIORITY (1=critical, 5=non-urgent)" >&2
echo "  Care Units Required: $CARE_UNITS" >&2
echo "  Infectious Flag: $INFECTIOUS" >&2
echo "  Arrival  : $(date)" >&2
echo "============================================" >&2

# ── Write binary PatientRecord struct to admissions via pipe ──
# Binary data goes to STDOUT (pipe to admissions)
python3 - <<PYEOF
import struct, sys, time

fmt = 'i 64s i i i i q i'
name_bytes = b'${NAME}'[:63] + b'\x00'
name_padded = name_bytes.ljust(64, b'\x00')

packed = struct.pack(fmt,
    0,
    name_padded,
    ${AGE},
    ${SEVERITY},
    ${PRIORITY},
    ${CARE_UNITS},
    ${ARRIVAL},
    ${INFECTIOUS}
)
sys.stdout.buffer.write(packed)
sys.stdout.buffer.flush()
PYEOF

echo "[TRIAGE] Patient record sent to admissions." >&2

