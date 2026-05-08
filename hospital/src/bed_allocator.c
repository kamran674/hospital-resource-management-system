/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : bed_allocator.c
 * Group   : Group XX
 * Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
 * Date    : 2026-04-01
 * Purpose : Memory management — Best-Fit / First-Fit / Worst-Fit
 *           allocation, coalescing, fragmentation reporting,
 *           and paging simulation.
 * Compile : (linked into admissions via Makefile)
 * ============================================================
 */

#include "hospital.h"

/* ────────────────────────────────────────────────
   Internal helper: determine care units needed
   based on patient priority / infectious flag
   ──────────────────────────────────────────────── */
static int care_units_for(PatientRecord *p) {
    if (p->priority <= 2)      return ICU_CARE_UNITS;
    if (p->infectious)         return ISOLATION_CARE_UNITS;
    return GENERAL_CARE_UNITS;
}

static const char *bed_type_for(PatientRecord *p) {
    if (p->priority <= 2)  return "ICU";
    if (p->infectious)     return "ISOLATION";
    return "GENERAL";
}

/* ────────────────────────────────────────────────
   print_ward_map — visual of ward memory block
   ──────────────────────────────────────────────── */
void print_ward_map(SharedWard *ward) {
    printf("\n╔══════════════════ WARD MAP ══════════════════╗\n");
    printf("  Unit: ");
    for (int i = 0; i < TOTAL_CARE_UNITS; i++) printf("%3d", i);
    printf("\n  Occ:  ");
    for (int i = 0; i < TOTAL_CARE_UNITS; i++) {
        if (ward->ward[i] == -1) printf("  .");
        else                     printf("%3d", ward->ward[i]);
    }
    printf("\n╚══════════════════════════════════════════════╝\n\n");
}

/* ────────────────────────────────────────────────
   allocate_bed — Best / First / Worst Fit
   Returns partition_id on success, -1 on failure
   ──────────────────────────────────────────────── */
int allocate_bed(SharedWard *ward, PatientRecord *p, AllocStrategy strat) {
    int needed    = care_units_for(p);
    p->care_units = needed;
    const char *btype = bed_type_for(p);

    int best_idx  = -1;
    int best_size = -1;

    for (int i = 0; i < ward->num_partitions; i++) {
        BedPartition *bp = &ward->partitions[i];
        if (!bp->is_free) continue;
        if (strcmp(bp->bed_type, btype) != 0) continue;
        if (bp->size < needed) continue;

        if (strat == FIRST_FIT) {
            best_idx = i;
            break;
        } else if (strat == BEST_FIT) {
            if (best_idx == -1 || bp->size < best_size) {
                best_idx  = i;
                best_size = bp->size;
            }
        } else { /* WORST_FIT */
            if (best_idx == -1 || bp->size > best_size) {
                best_idx  = i;
                best_size = bp->size;
            }
        }
    }

    if (best_idx == -1) return -1; /* no suitable bed */

    BedPartition *chosen = &ward->partitions[best_idx];
    chosen->is_free    = 0;
    chosen->patient_id = p->patient_id;

    /* Mark ward array cells as occupied */
    for (int u = chosen->start_unit; u < chosen->start_unit + needed; u++)
        ward->ward[u] = p->patient_id;

    printf("[ALLOCATOR] Patient %d (%s) → partition %d [%s] units %d-%d\n",
           p->patient_id, p->name, chosen->partition_id, btype,
           chosen->start_unit, chosen->start_unit + needed - 1);

    paging_report(p, chosen->size);
    return best_idx;
}

/* ────────────────────────────────────────────────
   free_bed — mark partition free, clear ward array
   ──────────────────────────────────────────────── */
void free_bed(SharedWard *ward, int patient_id) {
    for (int i = 0; i < ward->num_partitions; i++) {
        BedPartition *bp = &ward->partitions[i];
        if (bp->patient_id == patient_id) {
            printf("[ALLOCATOR] Freeing partition %d (patient %d)\n",
                   bp->partition_id, patient_id);
            /* Clear ward array */
            for (int u = bp->start_unit; u < bp->start_unit + bp->size; u++)
                ward->ward[u] = -1;
            bp->is_free    = 1;
            bp->patient_id = -1;
            return;
        }
    }
    fprintf(stderr, "[ALLOCATOR] WARNING: patient %d not found in any partition\n", patient_id);
}

/* ────────────────────────────────────────────────
   coalesce_free — merge adjacent free partitions
   of the same bed type
   ──────────────────────────────────────────────── */
void coalesce_free(SharedWard *ward) {
    printf("\n[COALESCE] Before:\n");
    print_ward_map(ward);

    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < ward->num_partitions - 1; i++) {
            BedPartition *a = &ward->partitions[i];
            BedPartition *b = &ward->partitions[i + 1];
            if (!a->is_free || !b->is_free) continue;
            if (strcmp(a->bed_type, b->bed_type) != 0) continue;
            if (a->start_unit + a->size != b->start_unit) continue;

            /* Merge b into a */
            a->size += b->size;
            /* Shift remaining partitions left */
            for (int j = i + 1; j < ward->num_partitions - 1; j++)
                ward->partitions[j] = ward->partitions[j + 1];
            ward->num_partitions--;
            merged = 1;
            printf("[COALESCE] Merged partition %d + %d → new size %d\n",
                   a->partition_id, b->partition_id, a->size);
            break;
        }
    }

    printf("[COALESCE] After:\n");
    print_ward_map(ward);
}

/* ────────────────────────────────────────────────
   report_fragmentation — external fragmentation %
   ──────────────────────────────────────────────── */
void report_fragmentation(SharedWard *ward, FILE *log) {
    int total_free = 0, largest_free = 0, current_run = 0;

    for (int i = 0; i < TOTAL_CARE_UNITS; i++) {
        if (ward->ward[i] == -1) {
            total_free++;
            current_run++;
            if (current_run > largest_free) largest_free = current_run;
        } else {
            current_run = 0;
        }
    }

    double frag_pct = (total_free == 0) ? 0.0
                    : (1.0 - (double)largest_free / total_free) * 100.0;

    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("[FRAG] Free=%d  LargestBlock=%d  ExternalFrag=%.1f%%\n",
           total_free, largest_free, frag_pct);

    if (log) {
        fprintf(log, "[%s] Free=%d  LargestBlock=%d  ExternalFrag=%.1f%%\n",
                ts, total_free, largest_free, frag_pct);
        fflush(log);
    }
}

/* ────────────────────────────────────────────────
   paging_report — internal fragmentation per patient
   ──────────────────────────────────────────────── */
void paging_report(PatientRecord *p, int assigned_units) {
    (void)assigned_units; /* suppress unused warning */
    int pages          = (p->care_units + PAGE_SIZE - 1) / PAGE_SIZE;
    int total_paged    = pages * PAGE_SIZE;
    int internal_frag  = total_paged - p->care_units;

    printf("[PAGING] Patient %d: needs=%d  pages=%d  total_paged=%d  "
           "internal_frag=%d unit(s)\n",
           p->patient_id, p->care_units, pages, total_paged, internal_frag);
}
