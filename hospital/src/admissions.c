
#include "hospital.h"
#include <sys/mman.h>

/* ── Global state ── */
static SharedWard      *g_ward       = NULL;  /* shared memory  */
static int              g_shm_id     = -1;
static PriorityQueue    g_queue;               /* patient queue  */
static sem_t           *g_sem_icu    = NULL;
static sem_t           *g_sem_iso    = NULL;
static sem_t           *g_sem_queue  = NULL;
static AllocStrategy    g_strategy   = BEST_FIT;
static volatile int     g_running    = 1;      /* shutdown flag  */
static int              g_next_pid   = 1;      /* patient ID counter */
static pthread_mutex_t  g_pid_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* Scheduling log */
static SchedEntry  g_sched_log[256];
static int         g_sched_count = 0;
static pthread_mutex_t g_sched_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Memory log file */
static FILE *g_mem_log = NULL;

/* ── Bonus: mmap patient record log ── */
#define MMAP_RECORD_FILE  "logs/patient_records.dat"
#define MMAP_MAX_RECORDS  256
typedef struct {
    int        patient_id;
    char       name[64];
    int        priority;
    char       bed_type[16];
    time_t     admitted_at;
    time_t     discharged_at;   /* 0 = still admitted */
} MmapRecord;
static MmapRecord *g_mmap_records = NULL;
static int         g_mmap_fd      = -1;
static size_t      g_mmap_size    = 0;

/* ── Forward declarations ── */
static void  init_ward(void);
static void  cleanup(void);
static void  sigchld_handler(int sig);
static void  sigterm_handler(int sig);
static void *receptionist_thread(void *arg);
static void *scheduler_thread(void *arg);
static void *nurse_thread(void *arg);
static void  admit_patient(QueueNode *node);
static void  write_schedule_log(void);
static void  mmap_init(void);
static void  mmap_write_admission(PatientRecord *p, const char *bed_type);
static void  mmap_write_discharge(int patient_id);
static void  mmap_close(void);

/* ────────────────────────────────────────────────
   Bonus: mmap patient record log
   ──────────────────────────────────────────────── */
static void mmap_init(void) {
    g_mmap_size = MMAP_MAX_RECORDS * sizeof(MmapRecord);
    g_mmap_fd   = open(MMAP_RECORD_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (g_mmap_fd < 0) { perror("[MMAP] open"); return; }

    /* Extend file to required size */
    if (ftruncate(g_mmap_fd, (off_t)g_mmap_size) < 0) {
        perror("[MMAP] ftruncate"); close(g_mmap_fd); g_mmap_fd = -1; return;
    }

    g_mmap_records = (MmapRecord *)mmap(NULL, g_mmap_size,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, g_mmap_fd, 0);
    if (g_mmap_records == MAP_FAILED) {
        perror("[MMAP] mmap"); g_mmap_records = NULL; return;
    }
    memset(g_mmap_records, 0, g_mmap_size);
    printf("[MMAP] Patient record log initialised → %s\n", MMAP_RECORD_FILE);
}

static void mmap_write_admission(PatientRecord *p, const char *bed_type) {
    if (!g_mmap_records || p->patient_id < 1 || p->patient_id > MMAP_MAX_RECORDS) return;
    MmapRecord *r = &g_mmap_records[p->patient_id - 1];
    r->patient_id  = p->patient_id;
    strncpy(r->name,     p->name,    64);
    strncpy(r->bed_type, bed_type,   16);
    r->priority    = p->priority;
    r->admitted_at = time(NULL);
    r->discharged_at = 0;
    msync(r, sizeof(MmapRecord), MS_ASYNC);
    printf("[MMAP] Admission record written for patient %d\n", p->patient_id);
}

static void mmap_write_discharge(int patient_id) {
    if (!g_mmap_records || patient_id < 1 || patient_id > MMAP_MAX_RECORDS) return;
    MmapRecord *r = &g_mmap_records[patient_id - 1];
    r->discharged_at = time(NULL);
    msync(r, sizeof(MmapRecord), MS_ASYNC);
    printf("[MMAP] Discharge record written for patient %d\n", patient_id);
}

static void mmap_close(void) {
    if (g_mmap_records) {
        msync(g_mmap_records, g_mmap_size, MS_SYNC);
        munmap(g_mmap_records, g_mmap_size);
        g_mmap_records = NULL;
    }
    if (g_mmap_fd >= 0) { close(g_mmap_fd); g_mmap_fd = -1; }
    printf("[MMAP] Patient record log flushed and closed.\n");
}

/* ────────────────────────────────────────────────
   Ward Initialization — shared memory + partitions
   ──────────────────────────────────────────────── */
static void init_ward(void) {
    /* Create shared memory */
    g_shm_id = shmget(SHM_KEY, sizeof(SharedWard), IPC_CREAT | 0666);
    if (g_shm_id < 0) { perror("shmget"); exit(EXIT_FAILURE); }

    g_ward = (SharedWard *)shmat(g_shm_id, NULL, 0);
    if (g_ward == (void *)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    memset(g_ward, 0, sizeof(SharedWard));

    /* Initialize ward memory array to -1 (free) */
    for (int i = 0; i < TOTAL_CARE_UNITS; i++) g_ward->ward[i] = -1;

    /* Initialize mutex and condition variable (shared across processes) */
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_ward->bed_mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&g_ward->bed_freed, &cattr);
    pthread_condattr_destroy(&cattr);

    /* Build partition list — ICU first, then Isolation, then General */
    int pid_ctr = 0, unit = 0;

    /* ICU partitions */
    for (int i = 0; i < ICU_BEDS; i++) {
        BedPartition *bp  = &g_ward->partitions[pid_ctr];
        bp->partition_id  = pid_ctr++;
        bp->start_unit    = unit;
        bp->size          = ICU_CARE_UNITS;
        bp->is_free       = 1;
        bp->patient_id    = -1;
        strcpy(bp->bed_type, "ICU");
        unit += ICU_CARE_UNITS;
    }
    /* Isolation partitions */
    for (int i = 0; i < ISOLATION_BEDS; i++) {
        BedPartition *bp  = &g_ward->partitions[pid_ctr];
        bp->partition_id  = pid_ctr++;
        bp->start_unit    = unit;
        bp->size          = ISOLATION_CARE_UNITS;
        bp->is_free       = 1;
        bp->patient_id    = -1;
        strcpy(bp->bed_type, "ISOLATION");
        unit += ISOLATION_CARE_UNITS;
    }
    /* General partitions */
    for (int i = 0; i < GENERAL_BEDS; i++) {
        BedPartition *bp  = &g_ward->partitions[pid_ctr];
        bp->partition_id  = pid_ctr++;
        bp->start_unit    = unit;
        bp->size          = GENERAL_CARE_UNITS;
        bp->is_free       = 1;
        bp->patient_id    = -1;
        strcpy(bp->bed_type, "GENERAL");
        unit += GENERAL_CARE_UNITS;
    }
    g_ward->num_partitions = pid_ctr;
    g_ward->total_patients_served = 0;

    printf("[INIT] Ward initialized: %d ICU + %d Isolation + %d General = %d beds, %d care units\n",
           ICU_BEDS, ISOLATION_BEDS, GENERAL_BEDS, TOTAL_BEDS, TOTAL_CARE_UNITS);
    print_ward_map(g_ward);
}

/* ────────────────────────────────────────────────
   Signal Handlers
   ──────────────────────────────────────────────── */
static void sigchld_handler(int sig) {
    (void)sig;
    /* Reap any zombie child processes without blocking */
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void sigterm_handler(int sig) {
    (void)sig;
    printf("\n[ADMISSIONS] SIGTERM received — shutting down...\n");
    g_running = 0;
}

/* ────────────────────────────────────────────────
   Admit Patient — fork + execv patient_simulator
   ──────────────────────────────────────────────── */
static void admit_patient(QueueNode *node) {
    PatientRecord *p = &node->record;

    /* Allocate a bed (already holds bed_mutex from caller) */
    int part_idx = allocate_bed(g_ward, p, g_strategy);
    if (part_idx < 0) {
        printf("[ADMISSIONS] No bed available for patient %d — re-queuing\n", p->patient_id);
        pq_push(&g_queue, *node);
        return;
    }

    BedPartition *bp = &g_ward->partitions[part_idx];

    /* Log for scheduling simulation */
    pthread_mutex_lock(&g_sched_mutex);
    if (g_sched_count < 256) {
        SchedEntry *se = &g_sched_log[g_sched_count++];
        se->patient_id = p->patient_id;
        strncpy(se->name, p->name, 64);
        se->priority   = p->priority;
        se->start_time = time(NULL);
        /* Estimate finish time based on bed type */
        int est = (strcmp(bp->bed_type,"ICU")==0) ? 10
                : (strcmp(bp->bed_type,"ISOLATION")==0) ? 6 : 5;
        se->finish_time = se->start_time + est;
    }
    pthread_mutex_unlock(&g_sched_mutex);

    /* Acquire semaphore for bed type capacity */
    if (strcmp(bp->bed_type, "ICU") == 0)
        sem_wait(g_sem_icu);
    else if (strcmp(bp->bed_type, "ISOLATION") == 0)
        sem_wait(g_sem_iso);

    /* Fork child process */
    pid_t pid = fork();
    if (pid < 0) {
        perror("[ADMISSIONS] fork");
        free_bed(g_ward, p->patient_id);
        return;
    }

    if (pid == 0) {
        /* ── Child: become patient_simulator ── */
        char sid[16], slevel[16], sbid[16];
        snprintf(sid,    sizeof(sid),    "%d", p->patient_id);
        snprintf(slevel, sizeof(slevel), "%d", p->priority);
        snprintf(sbid,   sizeof(sbid),   "%d", bp->partition_id);

        execv("./patient_simulator",
              (char *[]){ "./patient_simulator", sid, slevel, sbid, bp->bed_type, NULL });
        perror("[PATIENT_SIM] execv failed");
        _exit(EXIT_FAILURE);
    }

    /* Parent continues */
    printf("[ADMISSIONS] Patient %d (%s) admitted — PID %d → bed %d [%s]\n",
           p->patient_id, p->name, pid, bp->partition_id, bp->bed_type);

    /* Bonus: write admission record to mmap log */
    mmap_write_admission(p, bp->bed_type);

    report_fragmentation(g_ward, g_mem_log);
}

/* ────────────────────────────────────────────────
   Thread: Receptionist — reads from FIFO, enqueues
   ──────────────────────────────────────────────── */
static void *receptionist_thread(void *arg) {
    (void)arg;
    printf("[RECEPTIONIST] Thread started — listening on %s\n", DISCHARGE_FIFO);

    /* Read patient records from triage pipe (stdin in this model) */
    PatientRecord rec;
    while (g_running) {
        /* Read a PatientRecord written by triage.sh via pipe */
        ssize_t n = read(STDIN_FILENO, &rec, sizeof(PatientRecord));
        if (n <= 0) {
            if (g_running) usleep(100000);
            continue;
        }

        /* Assign unique patient ID */
        pthread_mutex_lock(&g_pid_mutex);
        rec.patient_id = g_next_pid++;
        pthread_mutex_unlock(&g_pid_mutex);

        printf("[RECEPTIONIST] New patient: %s (age %d, severity %d → priority %d)\n",
               rec.name, rec.age, rec.severity, rec.priority);

        /* Wait on bounded semaphore (producer side) */
        sem_wait(g_sem_queue);

        QueueNode node;
        node.record     = rec;
        node.burst_time = (rec.priority <= 2) ? 10 : (rec.priority == 3) ? 6 : 4;
        pq_push(&g_queue, node);
    }
    return NULL;
}

/* ────────────────────────────────────────────────
   Thread: Scheduler — dequeues, finds bed, forks
   ──────────────────────────────────────────────── */
static void *scheduler_thread(void *arg) {
    (void)arg;
    printf("[SCHEDULER] Thread started\n");

    while (g_running) {
        if (pq_empty(&g_queue)) { usleep(200000); continue; }

        QueueNode node = pq_pop(&g_queue);
        sem_post(g_sem_queue); /* free one slot in bounded semaphore */

        pthread_mutex_lock(&g_ward->bed_mutex);

        /* Wait if no bed is immediately free */
        int part = -1;
        while (g_running) {
            part = -1;
            /* Quick check — call allocate_bed tentatively */
            for (int i = 0; i < g_ward->num_partitions; i++) {
                BedPartition *bp = &g_ward->partitions[i];
                if (!bp->is_free) continue;
                /* Basic bed type filter */
                if (node.record.priority <= 2 && strcmp(bp->bed_type,"ICU") != 0) continue;
                if (node.record.infectious && strcmp(bp->bed_type,"ISOLATION") != 0) continue;
                part = i; break;
            }
            if (part >= 0) break;
            printf("[SCHEDULER] No suitable bed for patient %d — waiting...\n",
                   node.record.patient_id);
            pthread_cond_wait(&g_ward->bed_freed, &g_ward->bed_mutex);
        }

        admit_patient(&node);
        pthread_mutex_unlock(&g_ward->bed_mutex);
    }
    return NULL;
}

/* ────────────────────────────────────────────────
   Thread: Nurse — monitors discharge FIFO,
   frees beds, triggers coalescing
   ──────────────────────────────────────────────── */
typedef struct { char bed_type[16]; } NurseArg;

static void *nurse_thread(void *arg) {
    NurseArg *na = (NurseArg *)arg;
    printf("[NURSE-%s] Thread started — monitoring %s beds\n",
           na->bed_type, na->bed_type);

    int fifo_fd = open(DISCHARGE_FIFO, O_RDONLY | O_NONBLOCK);
    if (fifo_fd < 0) { perror("[NURSE] open FIFO"); return NULL; }

    while (g_running) {
        int patient_id = -1;
        ssize_t n = read(fifo_fd, &patient_id, sizeof(int));
        if (n != sizeof(int)) { usleep(300000); continue; }

        /* Check if this discharge is for our bed type */
        int mine = 0;
        pthread_mutex_lock(&g_ward->bed_mutex);
        for (int i = 0; i < g_ward->num_partitions; i++) {
            if (g_ward->partitions[i].patient_id == patient_id &&
                strcmp(g_ward->partitions[i].bed_type, na->bed_type) == 0) {
                mine = 1; break;
            }
        }

        if (mine) {
            printf("[NURSE-%s] Discharge received for patient %d\n",
                   na->bed_type, patient_id);
            free_bed(g_ward, patient_id);
            coalesce_free(g_ward);
            report_fragmentation(g_ward, g_mem_log);
            g_ward->total_patients_served++;

            /* Bonus: write discharge record to mmap log */
            mmap_write_discharge(patient_id);

            /* Release bed-type semaphore */
            if (strcmp(na->bed_type, "ICU") == 0)        sem_post(g_sem_icu);
            else if (strcmp(na->bed_type,"ISOLATION")==0) sem_post(g_sem_iso);

            /* Signal scheduler that a bed is free */
            pthread_cond_broadcast(&g_ward->bed_freed);
        }
        pthread_mutex_unlock(&g_ward->bed_mutex);
    }

    close(fifo_fd);
    free(na);
    return NULL;
}

/* ────────────────────────────────────────────────
   Write scheduling simulation to schedule_log.txt
   ──────────────────────────────────────────────── */
static void write_schedule_log(void) {
    FILE *f = fopen("logs/schedule_log.txt", "w");
    if (!f) { perror("fopen schedule_log"); return; }

    fprintf(f, "Hospital Patient Triage & Bed Allocator\n");
    fprintf(f, "Scheduling Simulation Report\n");
    fprintf(f, "Generated: %s\n\n", __DATE__);

    if (g_sched_count == 0) {
        fprintf(f, "No patients were admitted.\n");
        fclose(f); return;
    }

    /* Copy entries for each simulation (simulations sort in-place, so copy) */
    SchedEntry copy[256];
    memcpy(copy, g_sched_log, g_sched_count * sizeof(SchedEntry));
    simulate_fcfs(copy, g_sched_count, f);

    memcpy(copy, g_sched_log, g_sched_count * sizeof(SchedEntry));
    simulate_sjf(copy, g_sched_count, f);

    memcpy(copy, g_sched_log, g_sched_count * sizeof(SchedEntry));
    simulate_priority(copy, g_sched_count, f);

    memcpy(copy, g_sched_log, g_sched_count * sizeof(SchedEntry));
    simulate_rr(copy, g_sched_count, 3, f);

    fclose(f);
    printf("[ADMISSIONS] Schedule log written to logs/schedule_log.txt\n");
}

/* ────────────────────────────────────────────────
   Cleanup — detach + remove shared memory, semaphores
   ──────────────────────────────────────────────── */
static void cleanup(void) {
    printf("\n[CLEANUP] Total patients served: %d\n",
           g_ward ? g_ward->total_patients_served : 0);

    write_schedule_log();

    if (g_ward && g_ward != (void *)-1) {
        pthread_mutex_destroy(&g_ward->bed_mutex);
        pthread_cond_destroy(&g_ward->bed_freed);
        shmdt(g_ward);
    }
    if (g_shm_id >= 0) shmctl(g_shm_id, IPC_RMID, NULL);

    if (g_sem_icu)   { sem_close(g_sem_icu);   sem_unlink(SEM_ICU);       }
    if (g_sem_iso)   { sem_close(g_sem_iso);   sem_unlink(SEM_ISOLATION); }
    if (g_sem_queue) { sem_close(g_sem_queue); sem_unlink(SEM_QUEUE);     }

    /* Remove FIFO */
    unlink(DISCHARGE_FIFO);

    if (g_mem_log) fclose(g_mem_log);

    /* Bonus: flush and close mmap record log */
    mmap_close();

    printf("[CLEANUP] Done.\n");
}

/* ────────────────────────────────────────────────
   main
   ──────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    /* Parse --strategy flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "first") == 0)      g_strategy = FIRST_FIT;
            else if (strcmp(argv[i+1], "worst") == 0) g_strategy = WORST_FIT;
            else                                       g_strategy = BEST_FIT;
            printf("[ADMISSIONS] Allocation strategy: %s\n", argv[i+1]);
            i++;
        }
    }

    /* Open memory log */
    g_mem_log = fopen("logs/memory_log.txt", "a");
    if (!g_mem_log) perror("[ADMISSIONS] open memory_log (non-fatal)");

    /* Bonus: initialise mmap patient record log */
    mmap_init();

    /* Signal handlers */
    struct sigaction sa_chld = { .sa_handler = sigchld_handler, .sa_flags = SA_RESTART };
    sigemptyset(&sa_chld.sa_mask);
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_term = { .sa_handler = sigterm_handler };
    sigemptyset(&sa_term.sa_mask);
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT,  &sa_term, NULL);

    /* Initialize shared memory ward */
    init_ward();

    /* Create named FIFO for discharge notifications */
    mkfifo(DISCHARGE_FIFO, 0666);

    /* Initialize semaphores */
    g_sem_icu   = sem_open(SEM_ICU,      O_CREAT, 0666, ICU_BEDS);
    g_sem_iso   = sem_open(SEM_ISOLATION,O_CREAT, 0666, ISOLATION_BEDS);
    g_sem_queue = sem_open(SEM_QUEUE,    O_CREAT, 0666, MAX_QUEUE);
    if (!g_sem_icu || !g_sem_iso || !g_sem_queue) {
        perror("sem_open"); cleanup(); return EXIT_FAILURE;
    }

    /* Initialize priority queue */
    pq_init(&g_queue);

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   HOSPITAL PATIENT TRIAGE SYSTEM       ║\n");
    printf("║         ADMISSIONS MANAGER             ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    /* Launch thread pool */
    pthread_t t_receptionist, t_scheduler;
    pthread_t t_nurse[3];
    const char *nurse_types[] = { "ICU", "ISOLATION", "GENERAL" };

    pthread_create(&t_receptionist, NULL, receptionist_thread, NULL);
    pthread_create(&t_scheduler,    NULL, scheduler_thread,    NULL);

    for (int i = 0; i < 3; i++) {
        NurseArg *na = malloc(sizeof(NurseArg));
        strncpy(na->bed_type, nurse_types[i], 16);
        pthread_create(&t_nurse[i], NULL, nurse_thread, na);
    }

    /* Main thread: wait for shutdown signal */
    while (g_running) sleep(1);

    /* Wait for threads to finish */
    pthread_cancel(t_receptionist);
    pthread_cancel(t_scheduler);
    pthread_join(t_receptionist, NULL);
    pthread_join(t_scheduler, NULL);
    for (int i = 0; i < 3; i++) {
        pthread_cancel(t_nurse[i]);
        pthread_join(t_nurse[i], NULL);
    }

    cleanup();
    return EXIT_SUCCESS;
}