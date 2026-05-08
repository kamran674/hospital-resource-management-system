# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# File    : Makefile
# Group   : Group XX
# Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
# Date    : 2026-04-01
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
SRCDIR  = src
LOGDIR  = logs

ADMISSIONS_SRCS = $(SRCDIR)/admissions.c \
                  $(SRCDIR)/bed_allocator.c \
                  $(SRCDIR)/scheduler.c

PATIENT_SRCS    = $(SRCDIR)/patient_simulator.c

.PHONY: all clean run test valgrind

## ── all: build both binaries ──────────────────────────
all: admissions patient_simulator $(LOGDIR)
	@echo ""
	@echo "✓ Build complete — zero warnings required!"
	@echo "  Run:  make run"
	@echo "  Test: make test"

admissions: $(ADMISSIONS_SRCS) $(SRCDIR)/hospital.h
	$(CC) $(CFLAGS) -o $@ $(ADMISSIONS_SRCS) -lpthread
	@echo "[BUILD] admissions compiled OK"

patient_simulator: $(PATIENT_SRCS) $(SRCDIR)/hospital.h
	$(CC) $(CFLAGS) -o $@ $(PATIENT_SRCS) -lpthread
	@echo "[BUILD] patient_simulator compiled OK"

$(LOGDIR):
	mkdir -p $(LOGDIR)

## ── clean: remove binaries and logs ──────────────────
clean:
	rm -f admissions patient_simulator
	rm -f $(LOGDIR)/schedule_log.txt $(LOGDIR)/memory_log.txt
	rm -f /tmp/discharge_fifo /tmp/hospital_admissions.pid
	@echo "[CLEAN] Done."

## ── run: start the hospital simulation ───────────────
run: all
	@echo "[RUN] Starting hospital (Best-Fit strategy)..."
	bash scripts/start_hospital.sh --strategy best

## ── test: run automated stress test ──────────────────
test: all
	@echo "[TEST] Running stress test (20 patients)..."
	bash scripts/stress_test.sh

## ── valgrind: memory leak check on admissions ────────
valgrind: admissions patient_simulator
	@echo "[VALGRIND] Run the hospital first, then:"
	@echo "  valgrind --leak-check=full --track-origins=yes ./admissions"
	valgrind --leak-check=full --track-origins=yes --error-exitcode=1 \
	         ./admissions --strategy best &
	sleep 3
	bash scripts/triage.sh Test_Patient 30 5
	sleep 8
	kill %1 2>/dev/null || true

