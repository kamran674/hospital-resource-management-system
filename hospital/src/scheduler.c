/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : scheduler.c
 * Group   : Group XX
 * Members : Member1 (24F-XXXX), Member2 (24F-YYYY), Member3 (24F-ZZZZ)
 * Date    : 2026-04-01
 * Purpose : Priority queue (min-heap) + scheduling algorithm
 *           simulations: FCFS, SJF, Priority, Round Robin.
 * ============================================================
 */

#include "hospital.h"

/* ────────────────────────────────────────────────
   Priority Queue — min-heap ordered by priority
   (lower priority number = higher urgency)
   ──────────────────────────────────────────────── */

void pq_init(PriorityQueue *pq) {
    pq->size = 0;
    pthread_mutex_init(&pq->lock, NULL);
    pthread_cond_init(&pq->not_empty, NULL);
    pthread_cond_init(&pq->not_full, NULL);
}

static void swap_nodes(QueueNode *a, QueueNode *b) {
    QueueNode tmp = *a; *a = *b; *b = tmp;
}

/* Bubble up after insert */
static void heapify_up(PriorityQueue *pq, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (pq->nodes[parent].record.priority > pq->nodes[idx].record.priority) {
            swap_nodes(&pq->nodes[parent], &pq->nodes[idx]);
            idx = parent;
        } else break;
    }
}

/* Bubble down after pop */
static void heapify_down(PriorityQueue *pq, int idx) {
    int n = pq->size;
    while (1) {
        int left  = 2*idx + 1;
        int right = 2*idx + 2;
        int smallest = idx;
        if (left  < n && pq->nodes[left].record.priority  < pq->nodes[smallest].record.priority) smallest = left;
        if (right < n && pq->nodes[right].record.priority < pq->nodes[smallest].record.priority) smallest = right;
        if (smallest == idx) break;
        swap_nodes(&pq->nodes[idx], &pq->nodes[smallest]);
        idx = smallest;
    }
}

/* Thread-safe push (blocks if full) */
void pq_push(PriorityQueue *pq, QueueNode node) {
    pthread_mutex_lock(&pq->lock);
    while (pq->size >= MAX_QUEUE)
        pthread_cond_wait(&pq->not_full, &pq->lock);
    node.enqueue_time = time(NULL);
    pq->nodes[pq->size++] = node;
    heapify_up(pq, pq->size - 1);
    pthread_cond_signal(&pq->not_empty);
    pthread_mutex_unlock(&pq->lock);
}

/* Thread-safe pop (blocks if empty) */
QueueNode pq_pop(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->lock);
    while (pq->size == 0)
        pthread_cond_wait(&pq->not_empty, &pq->lock);
    QueueNode top = pq->nodes[0];
    pq->nodes[0] = pq->nodes[--pq->size];
    heapify_down(pq, 0);
    pthread_cond_signal(&pq->not_full);
    pthread_mutex_unlock(&pq->lock);
    return top;
}

int pq_empty(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->lock);
    int empty = (pq->size == 0);
    pthread_mutex_unlock(&pq->lock);
    return empty;
}

/* ────────────────────────────────────────────────
   Scheduling Simulation Helpers
   ──────────────────────────────────────────────── */

static void print_header(FILE *out, const char *algo) {
    fprintf(out, "\n========================================\n");
    fprintf(out, "  Algorithm: %s\n", algo);
    fprintf(out, "  %-5s %-15s %-8s %-10s %-12s %-12s\n",
            "PID", "Name", "Priority", "Burst(s)", "Wait(s)", "TAT(s)");
    fprintf(out, "----------------------------------------\n");
}

/* ── ASCII Gantt Chart ── */
typedef struct { int pid; double start; double end; } GanttSlot;

static void print_gantt(FILE *out, GanttSlot *slots, int ns, double total_time) {
    double scale = (total_time > 60.0) ? total_time / 60.0 : 1.0;
    fprintf(out, "\n  Gantt Chart (1 char = %.1f s):\n", scale);

    /* Top border */
    fprintf(out, "  +");
    for (int i = 0; i < ns; i++) {
        int w = (int)((slots[i].end - slots[i].start) / scale);
        if (w < 1) w = 1;
        for (int c = 0; c < w; c++) fprintf(out, "-");
        fprintf(out, "+");
    }
    fprintf(out, "\n  |");

    /* Labels row */
    for (int i = 0; i < ns; i++) {
        int w = (int)((slots[i].end - slots[i].start) / scale);
        if (w < 1) w = 1;
        char lbl[8]; snprintf(lbl, sizeof(lbl), "P%d", slots[i].pid);
        int pl = (w - (int)strlen(lbl)) / 2;
        int pr = w - (int)strlen(lbl) - pl;
        for (int c = 0; c < pl; c++) fprintf(out, " ");
        fprintf(out, "%s", lbl);
        for (int c = 0; c < pr; c++) fprintf(out, " ");
        fprintf(out, "|");
    }
    fprintf(out, "\n  +");

    /* Bottom border */
    for (int i = 0; i < ns; i++) {
        int w = (int)((slots[i].end - slots[i].start) / scale);
        if (w < 1) w = 1;
        for (int c = 0; c < w; c++) fprintf(out, "-");
        fprintf(out, "+");
    }
    fprintf(out, "\n  0");

    /* Time labels */
    for (int i = 0; i < ns; i++) {
        int w = (int)((slots[i].end - slots[i].start) / scale);
        if (w < 1) w = 1;
        char tl[12]; snprintf(tl, sizeof(tl), "%.0f", slots[i].end);
        for (int c = 0; c < w - (int)strlen(tl); c++) fprintf(out, " ");
        fprintf(out, "%s", tl);
    }
    fprintf(out, "\n\n");
}

static void print_metrics(FILE *out, SchedEntry *entries, int n) {
    double total_wait = 0, total_tat = 0;
    for (int i = 0; i < n; i++) {
        fprintf(out, "  %-5d %-15s %-8d %-10.1f %-12.1f %-12.1f\n",
                entries[i].patient_id,
                entries[i].name,
                entries[i].priority,
                (double)(entries[i].finish_time - entries[i].start_time),
                entries[i].waiting_time,
                entries[i].turnaround_time);
        total_wait += entries[i].waiting_time;
        total_tat  += entries[i].turnaround_time;
    }
    fprintf(out, "----------------------------------------\n");
    fprintf(out, "  Avg Waiting Time   : %.2f s\n", total_wait / n);
    fprintf(out, "  Avg Turnaround Time: %.2f s\n", total_tat  / n);
    fprintf(out, "========================================\n\n");
    fflush(out);
}

/* FCFS — sort by arrival time */
void simulate_fcfs(SchedEntry *entries, int n, FILE *out) {
    /* Sort by start_time (arrival) */
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (entries[j].start_time < entries[i].start_time) {
                SchedEntry tmp = entries[i]; entries[i] = entries[j]; entries[j] = tmp;
            }

    double clock = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        double arrive = difftime(entries[i].start_time, entries[0].start_time);
        if (clock < arrive) clock = arrive;
        entries[i].waiting_time     = clock - arrive;
        entries[i].turnaround_time  = entries[i].waiting_time + burst;
        clock += burst;
    }

    /* Build Gantt slots */
    GanttSlot slots[MAX_QUEUE]; double t = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        slots[i].pid   = entries[i].patient_id;
        slots[i].start = t + entries[i].waiting_time;
        slots[i].end   = slots[i].start + burst;
        t = slots[i].end;
    }
    print_header(out, "FCFS (First-Come First-Served)");
    print_gantt(out, slots, n, t);
    print_metrics(out, entries, n);
}

/* SJF — sort by burst time */
void simulate_sjf(SchedEntry *entries, int n, FILE *out) {
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++) {
            double bi = difftime(entries[i].finish_time, entries[i].start_time);
            double bj = difftime(entries[j].finish_time, entries[j].start_time);
            if (bj < bi) { SchedEntry tmp = entries[i]; entries[i] = entries[j]; entries[j] = tmp; }
        }

    double clock = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        entries[i].waiting_time    = clock;
        entries[i].turnaround_time = clock + burst;
        clock += burst;
    }

    /* Build Gantt slots */
    GanttSlot slots_sjf[MAX_QUEUE]; double t2 = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        slots_sjf[i].pid   = entries[i].patient_id;
        slots_sjf[i].start = t2;
        slots_sjf[i].end   = t2 + burst;
        t2 += burst;
    }
    print_header(out, "SJF (Shortest Job First)");
    print_gantt(out, slots_sjf, n, t2);
    print_metrics(out, entries, n);
}

/* Priority Scheduling — sort by priority ascending */
void simulate_priority(SchedEntry *entries, int n, FILE *out) {
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (entries[j].priority < entries[i].priority) {
                SchedEntry tmp = entries[i]; entries[i] = entries[j]; entries[j] = tmp;
            }

    double clock = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        entries[i].waiting_time    = clock;
        entries[i].turnaround_time = clock + burst;
        clock += burst;
    }

    /* Build Gantt slots */
    GanttSlot slots_pri[MAX_QUEUE]; double t3 = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        slots_pri[i].pid   = entries[i].patient_id;
        slots_pri[i].start = t3;
        slots_pri[i].end   = t3 + burst;
        t3 += burst;
    }
    print_header(out, "Priority Scheduling (triage level)");
    print_gantt(out, slots_pri, n, t3);
    print_metrics(out, entries, n);
}

/* Round Robin — configurable quantum */
void simulate_rr(SchedEntry *entries, int n, int quantum, FILE *out) {
    double *remaining = calloc(n, sizeof(double));
    double *arrival   = calloc(n, sizeof(double));
    double  clock     = 0;
    double  total_wait[MAX_QUEUE] = {0};

    for (int i = 0; i < n; i++) {
        remaining[i] = difftime(entries[i].finish_time, entries[i].start_time);
        arrival[i]   = difftime(entries[i].start_time, entries[0].start_time);
    }

    int done = 0;
    while (done < n) {
        int any = 0;
        for (int i = 0; i < n; i++) {
            if (remaining[i] <= 0) continue;
            any = 1;
            double run = (remaining[i] < quantum) ? remaining[i] : quantum;
            total_wait[i] += clock - arrival[i] - (difftime(entries[i].finish_time, entries[i].start_time) - remaining[i]);
            if (total_wait[i] < 0) total_wait[i] = 0;
            clock += run;
            remaining[i] -= run;
            if (remaining[i] <= 0) {
                entries[i].turnaround_time = clock - arrival[i];
                entries[i].waiting_time    = entries[i].turnaround_time - difftime(entries[i].finish_time, entries[i].start_time);
                if (entries[i].waiting_time < 0) entries[i].waiting_time = 0;
                done++;
            }
        }
        if (!any) break;
    }

    /* Build simple Gantt (one slot per patient showing total service) */
    GanttSlot slots_rr[MAX_QUEUE]; double t4 = 0;
    for (int i = 0; i < n; i++) {
        double burst = difftime(entries[i].finish_time, entries[i].start_time);
        slots_rr[i].pid   = entries[i].patient_id;
        slots_rr[i].start = t4;
        slots_rr[i].end   = t4 + burst;
        t4 += burst;
    }

    free(remaining); free(arrival);

    fprintf(out, "\n========================================\n");
    fprintf(out, "  Algorithm: Round Robin (quantum=%ds)\n", quantum);
    fprintf(out, "----------------------------------------\n");
    print_gantt(out, slots_rr, n, t4);
    print_metrics(out, entries, n);
}
