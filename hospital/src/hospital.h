#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

/* ── Capacity Constants ── */
#define ICU_BEDS        4
#define ISOLATION_BEDS  4
#define GENERAL_BEDS    12
#define TOTAL_BEDS      (ICU_BEDS + ISOLATION_BEDS + GENERAL_BEDS)

/* Care units each bed type provides */
#define ICU_CARE_UNITS       3
#define ISOLATION_CARE_UNITS 2
#define GENERAL_CARE_UNITS   1

/* Total care units in the ward (simulated memory block) */
#define TOTAL_CARE_UNITS  (ICU_BEDS*ICU_CARE_UNITS + ISOLATION_BEDS*ISOLATION_CARE_UNITS + GENERAL_BEDS*GENERAL_CARE_UNITS)

/* Page size for paging simulation (in care units) */
#define PAGE_SIZE 2

/* Maximum waiting patients in priority queue */
#define MAX_QUEUE 64

/* Shared memory key */
#define SHM_KEY  0xBEDF00D

/* Named IPC resources */
#define DISCHARGE_FIFO   "/tmp/discharge_fifo"
#define SEM_ICU          "/sem_icu_limit"
#define SEM_ISOLATION    "/sem_isolation_limit"
#define SEM_QUEUE        "/sem_queue_slots"

/* ── Data Structures ── */

/* Patient record passed via IPC */
typedef struct {
    int    patient_id;
    char   name[64];
    int    age;
    int    severity;      /* 1-10 raw severity from triage */
    int    priority;      /* 1-5 computed triage priority  */
    int    care_units;    /* memory units required          */
    time_t arrival_time;
    int    infectious;    /* 1 = needs isolation            */
} PatientRecord;

/* Single bed partition in the ward memory model */
typedef struct {
    int  partition_id;
    int  start_unit;      /* index in ward array  */
    int  size;            /* number of care units */
    int  is_free;         /* 1 = FREE, 0 = OCCUPIED */
    int  patient_id;      /* -1 if free            */
    char bed_type[16];    /* "ICU","GENERAL","ISOLATION" */
} BedPartition;

/* Shared memory layout */
typedef struct {
    BedPartition partitions[TOTAL_BEDS];
    int          num_partitions;
    int          ward[TOTAL_CARE_UNITS]; /* -1 = free, else patient_id */
    pthread_mutex_t bed_mutex;
    pthread_cond_t  bed_freed;
    int total_patients_served;
} SharedWard;

/* Node for priority queue */
typedef struct {
    PatientRecord record;
    int           burst_time;   /* estimated treatment seconds */
    time_t        enqueue_time;
} QueueNode;

/* Priority queue (min-heap by priority field) */
typedef struct {
    QueueNode nodes[MAX_QUEUE];
    int       size;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} PriorityQueue;

/* Scheduling log entry */
typedef struct {
    int    patient_id;
    char   name[64];
    int    priority;
    double waiting_time;
    double turnaround_time;
    time_t start_time;
    time_t finish_time;
} SchedEntry;

/* ── Allocation strategy ── */
typedef enum { BEST_FIT, FIRST_FIT, WORST_FIT } AllocStrategy;

/* ── Function prototypes (implemented across .c files) ── */

/* bed_allocator.c */
int  allocate_bed(SharedWard *ward, PatientRecord *p, AllocStrategy strat);
void free_bed(SharedWard *ward, int patient_id);
void coalesce_free(SharedWard *ward);
void print_ward_map(SharedWard *ward);
void report_fragmentation(SharedWard *ward, FILE *log);
void paging_report(PatientRecord *p, int assigned_units);

/* scheduler.c */
void pq_init(PriorityQueue *pq);
void pq_push(PriorityQueue *pq, QueueNode node);
QueueNode pq_pop(PriorityQueue *pq);
int  pq_empty(PriorityQueue *pq);
void simulate_fcfs(SchedEntry *entries, int n, FILE *out);
void simulate_sjf(SchedEntry *entries, int n, FILE *out);
void simulate_priority(SchedEntry *entries, int n, FILE *out);
void simulate_rr(SchedEntry *entries, int n, int quantum, FILE *out);

#endif /* HOSPITAL_H */