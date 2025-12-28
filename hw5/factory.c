// factory.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

/* ---- Given enums and helpers from the assignment ---- */
enum place
{
    NUZKY,
    VRTACKA,
    OHYBACKA,
    SVARECKA,
    LAKOVNA,
    SROUBOVAK,
    FREZA,
    _PLACE_COUNT
};

const char *place_str[_PLACE_COUNT] = {
    [NUZKY] = "nuzky",
    [VRTACKA] = "vrtacka",
    [OHYBACKA] = "ohybacka",
    [SVARECKA] = "svarecka",
    [LAKOVNA] = "lakovna",
    [SROUBOVAK] = "sroubovak",
    [FREZA] = "freza",
};

enum product
{
    A,
    B,
    C,
    _PRODUCT_COUNT
};

const char *product_str[_PRODUCT_COUNT] = {
    [A] = "A",
    [B] = "B",
    [C] = "C",
};

int find_string_in_array(const char **array, int length, char *what)
{
    for (int i = 0; i < length; i++)
        if (strcmp(array[i], what) == 0)
            return i;
    return -1;
}

/* ---- Our constants and production plan ---- */
#define PHASE_COUNT 6

/* Operation durations in ms per place (order matches enum place) */
static const int op_ms[_PLACE_COUNT] = {
    [NUZKY] = 100,
    [VRTACKA] = 200,
    [OHYBACKA] = 150,
    [SVARECKA] = 300,
    [LAKOVNA] = 400,
    [SROUBOVAK] = 250,
    [FREZA] = 500,
};

/* Production plan: which place is used at each phase for each product */
static const enum place plan[_PRODUCT_COUNT][PHASE_COUNT] = {
    [A] = {NUZKY, VRTACKA, OHYBACKA, SVARECKA, VRTACKA, LAKOVNA},
    [B] = {VRTACKA, NUZKY, FREZA, VRTACKA, LAKOVNA, SROUBOVAK},
    [C] = {FREZA, VRTACKA, SROUBOVAK, VRTACKA, FREZA, LAKOVNA},
};

/* ---- Forward declarations ---- */
struct Factory;

struct Worker
{
    char *name;
    enum place prof;
    pthread_t tid;

    bool leaving;  // set by 'end' or closing
    bool working;  // true while executing an operation
    int cur_prod;  // -1 if idle
    int cur_phase; // 0 if idle, else 1..6

    struct Factory *F;
    struct Worker *next;
};

struct Factory
{
    pthread_mutex_t mtx;
    pthread_cond_t cv;

    int parts[_PRODUCT_COUNT][PHASE_COUNT]; // waiting items per exact phase (0..5)
    int places_total[_PLACE_COUNT];
    int places_idle[_PLACE_COUNT];
    int remove_pending[_PLACE_COUNT];

    bool closing;

    struct Worker *workers; // singly-linked list
};

/* ---- Helpers ---- */

// Duplicate string or exit on failure
static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (!d)
    {
        perror("malloc");
        exit(1);
    }
    memcpy(d, s, n);
    return d;
}

// Initialize / destroy Factory
static void factory_init(struct Factory *F)
{
    memset(F, 0, sizeof(*F));
    if (pthread_mutex_init(&F->mtx, NULL) != 0)
    {
        perror("pthread_mutex_init");
        exit(1);
    }
    if (pthread_cond_init(&F->cv, NULL) != 0)
    {
        perror("pthread_cond_init");
        exit(1);
    }
    F->closing = false;
    F->workers = NULL;
}

static void factory_destroy(struct Factory *F)
{
    pthread_cond_destroy(&F->cv);
    pthread_mutex_destroy(&F->mtx);
}

/* Apply queued removals using currently idle places */
static void apply_pending_removals_locked(struct Factory *F)
{
    for (int p = 0; p < _PLACE_COUNT; ++p)
    {
        while (F->remove_pending[p] > 0 && F->places_idle[p] > 0)
        {
            F->remove_pending[p]--;
            F->places_idle[p]--;
            F->places_total[p]--;
        }
    }
}

/* Is any worker of this profession currently executing a step? (under lock) */
static bool any_worker_of_profession_working_locked(struct Factory *F, enum place p)
{
    for (struct Worker *w = F->workers; w; w = w->next)
        if (w->prof == p && w->working)
            return true;
    return false;
}

/* Is there at least one worker who can accept new work for this profession? (under lock)
   We define "can accept new work" as leaving == false. */
static bool any_worker_available_locked(struct Factory *F, enum place p)
{
    for (struct Worker *w = F->workers; w; w = w->next)
        if (w->prof == p && !w->leaving)
            return true;
    return false;
}

/* Compute if profession 'p' will still be needed given current WIP and machines.
   Idea:
   - Build boolean tokens parts_can_reach[prod][phase] representing items waiting now
     OR that will appear after currently running operations finish.
   - Propagate tokens forward only through steps that have at least one machine
     AND at least one worker able to accept new work for that step's profession.
   - If any token reaches a phase whose place == p, then p is still needed. */
static bool profession_needed_locked(struct Factory *F, enum place p)
{
    if (F->places_total[p] == 0)
        return false; // no machines => never needed

    bool token[_PRODUCT_COUNT][PHASE_COUNT] = {0};

    /* Seeds: currently waiting items */
    for (int prod = 0; prod < _PRODUCT_COUNT; ++prod)
        for (int ph = 0; ph < PHASE_COUNT; ++ph)
            if (F->parts[prod][ph] > 0)
                token[prod][ph] = true;

    /* Seeds: items that will appear after ongoing work completes (next phase) */
    for (struct Worker *w = F->workers; w; w = w->next)
    {
        if (!w->working)
            continue;
        int cur_idx = w->cur_phase - 1; // 0..5
        if (cur_idx + 1 < PHASE_COUNT)
            token[w->cur_prod][cur_idx + 1] = true;
    }

    /* Forward closure: advance tokens only through executable steps */
    bool changed;
    do
    {
        changed = false;
        for (int prod = 0; prod < _PRODUCT_COUNT; ++prod)
        {
            for (int ph = 0; ph < PHASE_COUNT; ++ph)
            {
                if (!token[prod][ph])
                    continue;

                enum place plc = plan[prod][ph];
                if (F->places_total[plc] <= 0)
                    continue;
                if (!any_worker_available_locked(F, plc))
                    continue;

                if (ph + 1 < PHASE_COUNT && !token[prod][ph + 1])
                {
                    token[prod][ph + 1] = true;
                    changed = true;
                }
            }
        }
    } while (changed);

    /* If any reachable phase requires profession p, then p is needed */
    for (int prod = 0; prod < _PRODUCT_COUNT; ++prod)
        for (int ph = 0; ph < PHASE_COUNT; ++ph)
            if (token[prod][ph] && plan[prod][ph] == p)
                return true;

    return false;
}

/* Linear find; called only with F->mtx locked */
static struct Worker *find_worker_locked(struct Factory *F, const char *name)
{
    for (struct Worker *w = F->workers; w; w = w->next)
        if (strcmp(w->name, name) == 0)
            return w;
    return NULL;
}

/* Choose a job for the worker (if any) */
static bool choose_job_locked(struct Factory *F, struct Worker *W, int *out_prod, int *out_phase)
{
    if (W->leaving)
        return false;                 // do not start new work if leaving
    apply_pending_removals_locked(F); // consume idle places if there is pending removal

    enum place p = W->prof;

    // Highest phase first (5..0), tie-break by product A<B<C (0.._PRODUCT_COUNT-1)
    for (int phase = PHASE_COUNT - 1; phase >= 0; --phase)
    {
        for (int prod = 0; prod < _PRODUCT_COUNT; ++prod)
        {
            if (plan[prod][phase] != p)
                continue;
            if (F->parts[prod][phase] <= 0)
                continue;
            if (F->places_idle[p] <= 0)
                continue;

            // Reserve the part and the workplace under the lock
            F->parts[prod][phase]--;
            F->places_idle[p]--;

            W->working = true;
            W->cur_prod = prod;
            W->cur_phase = phase + 1; // store 1..6 for easy printing

            if (out_prod)
                *out_prod = prod;
            if (out_phase)
                *out_phase = phase;
            return true;
        }
    }
    return false;
}

/* Sleep for given milliseconds */
static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ---- Worker thread ---- */
static void *worker_thread(void *arg)
{
    struct Worker *W = (struct Worker *)arg;
    struct Factory *F = W->F;

    pthread_mutex_lock(&F->mtx);
    for (;;)
    {
        /* If EOF mode: decide whether this profession can leave now.
           Rule: leave if nobody of this profession is currently working
                 AND no WIP can ever reach this profession. */
        if (F->closing && !W->working)
        {
            bool someone_working_same_prof = any_worker_of_profession_working_locked(F, W->prof);
            bool still_needed = profession_needed_locked(F, W->prof);
            if (!someone_working_same_prof && !still_needed)
            {
                W->leaving = true;
            }
        }

        /* Wait until: a) we should leave, or b) a job is assigned */
        while (!W->leaving && !choose_job_locked(F, W, NULL, NULL))
        {
            /* Re-check closing condition before sleeping to avoid missed wake-ups */
            if (F->closing && !W->working)
            {
                bool someone_working_same_prof = any_worker_of_profession_working_locked(F, W->prof);
                bool still_needed = profession_needed_locked(F, W->prof);
                if (!someone_working_same_prof && !still_needed)
                {
                    W->leaving = true;
                    break;
                }
            }
            pthread_cond_wait(&F->cv, &F->mtx);
        }

        /* Leave if requested and idle */
        if (W->leaving && !W->working)
        {
            printf("%s goes home\n", W->name);
            fflush(stdout);
            pthread_mutex_unlock(&F->mtx);
            return NULL;
        }

        /* If we got here, a job has been reserved under the lock */
        int phase_idx = W->cur_phase - 1; // 0..5
        enum product prod = (enum product)W->cur_prod;
        enum place plc = W->prof;

        /* Print activity under the lock to preserve start-order */
        printf("%s %s %d %s\n", W->name, place_str[plc], W->cur_phase, product_str[prod]);
        fflush(stdout);

        /* Execute without holding the lock */
        pthread_mutex_unlock(&F->mtx);
        sleep_ms(op_ms[plc]);
        pthread_mutex_lock(&F->mtx);

        /* Complete: move to next phase or finish */
        if (phase_idx == PHASE_COUNT - 1)
        {
            printf("done %s\n", product_str[prod]);
            fflush(stdout);
        }
        else
        {
            F->parts[prod][phase_idx + 1]++;
        }

        /* Return or remove workplace per pending remove */
        if (F->remove_pending[plc] > 0)
        {
            F->remove_pending[plc]--;
            F->places_total[plc]--;
        }
        else
        {
            F->places_idle[plc]++;
        }

        /* Clear worker state and wake others */
        W->working = false;
        W->cur_prod = -1;
        W->cur_phase = 0;
        pthread_cond_broadcast(&F->cv);
        /* loop */
    }
}

/* ---- Commands: start / end / add / remove / make ---- */
static int start_worker_cmd(struct Factory *F, const char *name, enum place prof)
{
    pthread_mutex_lock(&F->mtx);
    if (find_worker_locked(F, name))
    {
        pthread_mutex_unlock(&F->mtx);
        fprintf(stderr, "Invalid command: worker '%s' already exists\n", name);
        return -1;
    }

    struct Worker *W = (struct Worker *)calloc(1, sizeof(*W));
    if (!W)
    {
        pthread_mutex_unlock(&F->mtx);
        perror("calloc");
        return -1;
    }

    W->name = xstrdup(name);
    W->prof = prof;
    W->F = F;
    W->cur_prod = -1;
    W->cur_phase = 0;
    W->leaving = false;
    W->working = false;

    W->next = F->workers;
    F->workers = W;

    if (pthread_create(&W->tid, NULL, worker_thread, W) != 0)
    {
        F->workers = W->next;
        free(W->name);
        free(W);
        pthread_mutex_unlock(&F->mtx);
        fprintf(stderr, "Failed to create thread for worker %s\n", name);
        return -1;
    }

    pthread_cond_broadcast(&F->cv);
    pthread_mutex_unlock(&F->mtx);
    return 0;
}

static void end_worker_cmd(struct Factory *F, const char *name)
{
    pthread_mutex_lock(&F->mtx);
    struct Worker *W = find_worker_locked(F, name);
    if (!W)
    {
        pthread_mutex_unlock(&F->mtx);
        fprintf(stderr, "Invalid command: end %s (no such worker)\n", name);
        return;
    }
    W->leaving = true;
    pthread_cond_broadcast(&F->cv);
    pthread_mutex_unlock(&F->mtx);
}

static void add_place_cmd(struct Factory *F, enum place p)
{
    pthread_mutex_lock(&F->mtx);
    F->places_total[p]++;
    F->places_idle[p]++;
    apply_pending_removals_locked(F); // consume any queued remove on this type if possible
    pthread_cond_broadcast(&F->cv);
    pthread_mutex_unlock(&F->mtx);
}

static void remove_place_cmd(struct Factory *F, enum place p)
{
    pthread_mutex_lock(&F->mtx);
    if (F->places_idle[p] > 0)
    {
        F->places_idle[p]--;
        F->places_total[p]--;
    }
    else
    {
        F->remove_pending[p]++;
    }
    pthread_mutex_unlock(&F->mtx);
}

static void make_cmd(struct Factory *F, enum product prod)
{
    pthread_mutex_lock(&F->mtx);
    F->parts[prod][0]++; // new item waiting for phase 1
    pthread_cond_broadcast(&F->cv);
    pthread_mutex_unlock(&F->mtx);
}

/* ---- main: command loop ---- */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct Factory F;
    factory_init(&F);

    char *line = NULL;
    size_t sz = 0;
    while (1)
    {
        char *cmd, *arg1, *arg2, *arg3, *saveptr;

        if (getline(&line, &sz, stdin) == -1)
            break; /* Error or EOF */

        cmd = strtok_r(line, " \r\n", &saveptr);
        arg1 = strtok_r(NULL, " \r\n", &saveptr);
        arg2 = strtok_r(NULL, " \r\n", &saveptr);
        arg3 = strtok_r(NULL, " \r\n", &saveptr);

        if (!cmd)
        {
            continue; /* Empty line */
        }
        else if (strcmp(cmd, "start") == 0 && arg1 && arg2 && !arg3)
        {
            int p = find_string_in_array(place_str, _PLACE_COUNT, arg2);
            if (p >= 0)
            {
                start_worker_cmd(&F, arg1, (enum place)p);
            }
            else
            {
                fprintf(stderr, "Invalid command: start %s %s (bad place)\n", arg1, arg2);
            }
        }
        else if (strcmp(cmd, "make") == 0 && arg1 && !arg2)
        {
            int product = find_string_in_array(product_str, _PRODUCT_COUNT, arg1);
            if (product >= 0)
            {
                make_cmd(&F, (enum product)product);
            }
            else
            {
                fprintf(stderr, "Invalid command: make %s (bad product)\n", arg1);
            }
        }
        else if (strcmp(cmd, "end") == 0 && arg1 && !arg2)
        {
            end_worker_cmd(&F, arg1);
        }
        else if (strcmp(cmd, "add") == 0 && arg1 && !arg2)
        {
            int p = find_string_in_array(place_str, _PLACE_COUNT, arg1);
            if (p >= 0)
            {
                add_place_cmd(&F, (enum place)p);
            }
            else
            {
                fprintf(stderr, "Invalid command: add %s (bad place)\n", arg1);
            }
        }
        else if (strcmp(cmd, "remove") == 0 && arg1 && !arg2)
        {
            int p = find_string_in_array(place_str, _PLACE_COUNT, arg1);
            if (p >= 0)
            {
                remove_place_cmd(&F, (enum place)p);
            }
            else
            {
                fprintf(stderr, "Invalid command: remove %s (bad place)\n", arg1);
            }
        }
        else
        {
            fprintf(stderr, "Invalid command: %s\n", cmd);
        }
    }
    free(line);

    /* Closing: enter EOF mode. Workers will leave when their profession is not needed. */
    pthread_mutex_lock(&F.mtx);
    F.closing = true;
    pthread_cond_broadcast(&F.cv);   // wake workers to re-evaluate closing predicate
    struct Worker *head = F.workers; // snapshot head while holding the lock
    pthread_mutex_unlock(&F.mtx);

    /* Phase 1: join all workers, DO NOT free nodes yet */
    for (struct Worker *w = head; w; w = w->next)
    {
        pthread_join(w->tid, NULL);
    }

    /* Phase 2: free list under the lock and clear it */
    pthread_mutex_lock(&F.mtx);
    struct Worker *w = F.workers;
    while (w)
    {
        struct Worker *next = w->next;
        free(w->name);
        free(w);
        w = next;
    }
    F.workers = NULL;
    pthread_mutex_unlock(&F.mtx);

    factory_destroy(&F);
    return 0;
}
