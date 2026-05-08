/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : patient_simulator.c
 * Group   : Group XX
 * Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
 * Date    : 2026-04-01
 * Purpose : Patient process — receives patient data via pipe,
 *           simulates treatment (sleep), then notifies
 *           admissions via named FIFO on discharge.
 * Compile : gcc -Wall -o patient_simulator patient_simulator.c
 * ============================================================
 */

#include "hospital.h"

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: patient_simulator <patient_id> <triage_level> <bed_id> <bed_type>\n");
        return EXIT_FAILURE;
    }

    int patient_id   = atoi(argv[1]);
    int triage_level = atoi(argv[2]);
    int bed_id       = atoi(argv[3]);
    char *bed_type   = argv[4];

    srand(time(NULL) ^ getpid()); /* unique seed per process */

    /* ── Determine sleep range by bed type ── */
    int sleep_min, sleep_max;
    if (strcmp(bed_type, "ICU") == 0)           { sleep_min = 5;  sleep_max = 15; }
    else if (strcmp(bed_type, "ISOLATION") == 0) { sleep_min = 3;  sleep_max = 10; }
    else                                          { sleep_min = 2;  sleep_max = 8;  }

    int treatment_time = sleep_min + rand() % (sleep_max - sleep_min + 1);

    /* ── Lifecycle messages ── */
    printf("[PATIENT %d] Arrived at hospital | Triage Level: %d | Bed: %d (%s)\n",
           patient_id, triage_level, bed_id, bed_type);
    fflush(stdout);

    printf("[PATIENT %d] Treatment started   | Duration: %d seconds\n",
           patient_id, treatment_time);
    fflush(stdout);

    /* Simulate treatment */
    sleep(treatment_time);

    printf("[PATIENT %d] Treatment complete  | Discharging from bed %d\n",
           patient_id, bed_id);
    fflush(stdout);

    /* ── Notify admissions via named FIFO ── */
    int fifo_fd = open(DISCHARGE_FIFO, O_WRONLY);
    if (fifo_fd < 0) {
        perror("[PATIENT] open FIFO");
        return EXIT_FAILURE;
    }

    /* Write patient_id as discharge signal */
    if (write(fifo_fd, &patient_id, sizeof(int)) != sizeof(int))
        perror("[PATIENT] write FIFO");

    close(fifo_fd);

    printf("[PATIENT %d] Discharge signal sent.\n", patient_id);
    fflush(stdout);

    return EXIT_SUCCESS;
}
