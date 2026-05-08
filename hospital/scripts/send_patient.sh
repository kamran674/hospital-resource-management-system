#!/bin/bash
# Usage: ./scripts/send_patient.sh <name> <age> <severity> [--infectious]

ADM_PID=$(pgrep admissions)
if [ -z "$ADM_PID" ]; then
    echo "ERROR: admissions not running!"
    exit 1
fi

./scripts/triage.sh "$@" > /proc/$ADM_PID/fd/0
