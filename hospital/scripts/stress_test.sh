#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stress_test.sh
# Group   : Group XX
# Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
# Date    : 2026-04-01
# Purpose : Automated stress test — spawns 20 patient arrivals
#           in rapid succession to test concurrency & stability.
# Usage   : ./stress_test.sh
# ============================================================

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║         STRESS TEST — 20 PATIENTS       ║"
echo "╚══════════════════════════════════════════╝"
echo ""

NAMES=("Alice_Smith" "Bob_Jones" "Carol_White" "David_Brown"
       "Eva_Green"   "Frank_Lee"  "Grace_Kim"   "Hank_Turner"
       "Iris_Hall"   "Jack_Moore" "Karen_Clark" "Leo_Walker"
       "Mia_Hill"    "Ned_Young"  "Olivia_King" "Paul_Scott"
       "Quinn_Adams" "Rita_Baker" "Sam_Carter"  "Tina_Davis")

AGES=(45 32 67 23 51 38 72 29 55 41 63 34 48 56 27 70 39 44 61 33)
SEVERITIES=(9 3 7 5 10 2 8 4 6 1 9 7 5 8 3 6 10 4 7 2)
INFECTIOUS=(0 0 0 0 0 1 0 0 1 0 0 0 0 1 0 0 0 0 0 1)

PASS=0
FAIL=0

for i in $(seq 0 19); do
    NAME="${NAMES[$i]}"
    AGE="${AGES[$i]}"
    SEV="${SEVERITIES[$i]}"
    INF="${INFECTIOUS[$i]}"

    echo -n "  Patient $((i+1))/20 — $NAME (age=$AGE, severity=$SEV) ... "

    FLAG=""
    [ "$INF" -eq 1 ] && FLAG="--infectious"

    if bash scripts/triage.sh "$NAME" "$AGE" "$SEV" $FLAG > /dev/null 2>&1; then
        echo "OK"
        PASS=$((PASS+1))
    else
        echo "FAIL"
        FAIL=$((FAIL+1))
    fi

    # Stagger arrivals slightly (0.3s apart) to simulate rapid burst
    sleep 0.3
done

echo ""
echo "─────────────────────────────────────────"
echo "  Stress Test Results:"
echo "  Passed : $PASS / 20"
echo "  Failed : $FAIL / 20"
echo "─────────────────────────────────────────"
echo ""


