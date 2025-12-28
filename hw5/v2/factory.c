// factory.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

/* --- Enums and helpers --- */
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
const char *place_str[_PLACE_COUNT] = {"nuzky", "vrtacka", "ohybacka", "svarecka", "lakovna", "sroubovak", "freza"};

enum product
{
    A,
    B,
    C,
    _PRODUCT_COUNT
};
const char *product_str[_PRODUCT_COUNT] = {"A", "B", "C"};

int find_string_in_array(const char **arr, int len, char *what)
{
    for (int i = 0; i < len; i++)
        if (strcmp(arr[i], what) == 0)
            return i;
    return -1;
}

/* --- Constants/plan --- */
#define PHASE_COUNT 6
static const int op_ms[_PLACE_COUNT] = {[NUZKY] = 100, [VRTACKA] = 200, [OHYBACKA] = 150, [SVARECKA] = 300, [LAKOVNA] = 400, [SROUBOVAK] = 250, [FREZA] = 500};
static const enum place plan[_PRODUCT_COUNT][PHASE_COUNT] = {
    [A] = {NUZKY, VRTACKA, OHYBACKA, SVARECKA, VRTACKA, LAKOVNA},
    [B] = {VRTACKA, NUZKY, FREZA, VRTACKA, LAKOVNA, SROUBOVAK},
    [C] = {FREZA, VRTACKA, SROUBOVAK, VRTACKA, FREZA, LAKOVNA},
};

/* --- Data structures --- */
struct Factory;

struct Worker
{
    char *name;
    enum place prof;       // profession
    pthread_t tid;         // thread ID
    bool leaving, working; // flags
    int cur_prod;          // -1 if idle
    int cur_phase;         // 0 if idle, else 1..6
    struct Factory *F;     // back reference
    struct Worker *next;   // next in linked list
};

struct Factory
{
    pthread_mutex_t mtx;                    // mutex for synchronization
    pthread_cond_t cv;                      // condition variable
    int parts[_PRODUCT_COUNT][PHASE_COUNT]; // waiting items per phase (0..5)
    int places_total[_PLACE_COUNT], places_idle[_PLACE_COUNT], remove_pending[_PLACE_COUNT];
    bool closing;
    struct Worker *workers; // singly-linked list
};

/* --- Init / destroy --- */
static void factory_init(struct Factory *F)
{
    memset(F, 0, sizeof(*F));
    pthread_mutex_init(&F->mtx, NULL);
    pthread_cond_init(&F->cv, NULL);
}

static void factory_destroy(struct Factory *F)
{
    pthread_cond_destroy(&F->cv);
    pthread_mutex_destroy(&F->mtx);
}

/* --- Helpers under lock - called with F->mtx locked --- */

// Apply queued removals using idle places
static void apply_pending_removals_locked(struct Factory *F)
{
    for (int p = 0; p < _PLACE_COUNT; p++)
        while (F->remove_pending[p] > 0 && F->places_idle[p] > 0)
        {
            F->remove_pending[p]--;
            F->places_idle[p]--;
            F->places_total[p]--;
        }
}

// Find worker by name
static struct Worker *find_worker_locked(struct Factory *F, const char *name)
{
    for (struct Worker *w = F->workers; w; w = w->next)
        if (strcmp(w->name, name) == 0)
            return w;
    return NULL;
}

// Is any worker of profession p currently working?
static bool any_worker_of_profession_working_locked(struct Factory *F, enum place p)
{
    for (struct Worker *w = F->workers; w; w = w->next)
        if (w->prof == p && w->working)
            return true;
    return false;
}

// Is any worker of profession p available (not leaving)?
static bool any_worker_available_locked(struct Factory *F, enum place p)
{
    for (struct Worker *w = F->workers; w; w = w->next)
        if (w->prof == p && !w->leaving)
            return true;
    return false;
}

/* Is profession p still needed if we never accept new input? (EOF mode) */
static bool profession_needed_locked(struct Factory *F, enum place p)
{
    if (F->places_total[p] == 0)
        return false;

    bool ok[_PLACE_COUNT] = {0}; // ok[plc] = place plc has idle place and available worker

    for (int plc = 0; plc < _PLACE_COUNT; ++plc)
        ok[plc] = (F->places_total[plc] > 0) && any_worker_available_locked(F, (enum place)plc);

    bool token[_PRODUCT_COUNT][PHASE_COUNT] = {{0}}; // token[pr][ph] = product pr can reach phase ph

    // Initial tokens: all existing parts + in-progress work
    for (int pr = 0; pr < _PRODUCT_COUNT; ++pr)
        for (int ph = 0; ph < PHASE_COUNT; ++ph)
            if (F->parts[pr][ph] > 0)
                token[pr][ph] = true;

    // Add in-progress work
    for (struct Worker *w = F->workers; w; w = w->next)
        if (w->working)
        {
            int i = w->cur_phase - 1;
            if (i + 1 < PHASE_COUNT)
                token[w->cur_prod][i + 1] = true;
        }

    // Propagate tokens forward
    bool changed;
    do
    {
        changed = false;
        for (int pr = 0; pr < _PRODUCT_COUNT; ++pr)
            for (int ph = 0; ph < PHASE_COUNT; ++ph)
                if (token[pr][ph])
                {
                    enum place plc = plan[pr][ph];
                    if (!ok[plc])
                        continue;
                    if (ph + 1 < PHASE_COUNT && !token[pr][ph + 1])
                    {
                        token[pr][ph + 1] = true;
                        changed = true;
                    }
                }
    } while (changed);

    // Check if p is needed - any reachable phase uses p
    for (int pr = 0; pr < _PRODUCT_COUNT; ++pr)
        for (int ph = 0; ph < PHASE_COUNT; ++ph)
            if (token[pr][ph] && plan[pr][ph] == p)
                return true;
    return false;
}

/* Choose highest-step job for W (6->1; A<B<C). Reserve part+place under lock. */
static bool choose_job_locked(struct Factory *F, struct Worker *W)
{
    if (W->leaving)
        return false;
    apply_pending_removals_locked(F);
    enum place p = W->prof;

    for (int ph = PHASE_COUNT - 1; ph >= 0; --ph)
        for (int pr = 0; pr < _PRODUCT_COUNT; ++pr)
        {
            if (plan[pr][ph] != p)
                continue;
            if (F->parts[pr][ph] <= 0 || F->places_idle[p] <= 0)
                continue;
            F->parts[pr][ph]--;
            F->places_idle[p]--;
            W->working = true;
            W->cur_prod = pr;
            W->cur_phase = ph + 1;
            return true;
        }
    return false;
}

/* Sleep for given milliseconds */
static void sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* --- Worker thread --- */
static void *worker_thread(void *arg)
{
    struct Worker *W = (struct Worker *)arg;
    struct Factory *F = W->F;
    pthread_mutex_lock(&F->mtx); // lock the factory mutex
    for (;;)
    {
        if (F->closing && !W->working) // EOF mode: check if can leave
        {
            if (!any_worker_of_profession_working_locked(F, W->prof) &&
                !profession_needed_locked(F, W->prof))
                W->leaving = true;
        }
        while (!W->leaving && !choose_job_locked(F, W)) // wait for job or leave signal
        {
            if (F->closing && !W->working)
            {
                if (!any_worker_of_profession_working_locked(F, W->prof) &&
                    !profession_needed_locked(F, W->prof))
                {
                    W->leaving = true;
                    break;
                }
            }
            pthread_cond_wait(&F->cv, &F->mtx);
        }
        if (W->leaving && !W->working) // leave if requested and idle
        {
            printf("%s goes home\n", W->name);
            pthread_mutex_unlock(&F->mtx);
            return NULL;
        }

        // Execute reserved job
        int i = W->cur_phase - 1;
        enum product pr = (enum product)W->cur_prod;
        enum place plc = W->prof;
        printf("%s %s %d %s\n", W->name, place_str[plc], W->cur_phase, product_str[pr]);

        pthread_mutex_unlock(&F->mtx);
        sleep_ms(op_ms[plc]); // simulate work by sleeping
        pthread_mutex_lock(&F->mtx);

        // Job done: update factory state
        if (i == PHASE_COUNT - 1)
            printf("done %s\n", product_str[pr]);
        else
            F->parts[pr][i + 1]++;

        if (F->remove_pending[plc] > 0)
        {
            F->remove_pending[plc]--;
            F->places_total[plc]--;
        }
        else
            F->places_idle[plc]++;

        W->working = false;
        W->cur_prod = -1;
        W->cur_phase = 0;
        pthread_cond_broadcast(&F->cv);
    }
}

/* --- Commands --- */
static int start_worker_cmd(struct Factory *F, const char *name, enum place prof)
{
    pthread_mutex_lock(&F->mtx);
    if (find_worker_locked(F, name))
    {
        pthread_mutex_unlock(&F->mtx);
        fprintf(stderr, "Invalid command: worker '%s' exists\n", name);
        return -1;
    }
    struct Worker *W = calloc(1, sizeof(*W));
    if (!W)
    {
        pthread_mutex_unlock(&F->mtx);
        perror("calloc");
        return -1;
    }
    W->name = strdup(name);
    W->prof = prof;
    W->F = F;
    W->cur_prod = -1;
    W->next = F->workers;
    F->workers = W;
    if (pthread_create(&W->tid, NULL, worker_thread, W) != 0)
    {
        F->workers = W->next;
        free(W->name);
        free(W);
        pthread_mutex_unlock(&F->mtx);
        fprintf(stderr, "Failed to create thread\n");
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
    apply_pending_removals_locked(F);
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
        F->remove_pending[p]++;
    pthread_mutex_unlock(&F->mtx);
}

static void make_cmd(struct Factory *F, enum product pr)
{
    pthread_mutex_lock(&F->mtx);
    F->parts[pr][0]++;
    pthread_cond_broadcast(&F->cv);
    pthread_mutex_unlock(&F->mtx);
}

/* --- main --- */
int main(void)
{
    // Initialize factory
    struct Factory F;
    factory_init(&F);

    char *line = NULL;  // for getline
    size_t sz = 0;
    for (;;)
    {
        char *cmd, *arg1, *arg2, *arg3, *saveptr; // command and arguments
        if (getline(&line, &sz, stdin) == -1) // EOF
            break;
        // Parse command line
        cmd = strtok_r(line, " \r\n", &saveptr);
        arg1 = strtok_r(NULL, " \r\n", &saveptr);
        arg2 = strtok_r(NULL, " \r\n", &saveptr);
        arg3 = strtok_r(NULL, " \r\n", &saveptr);

        if (!cmd)
            continue;
        else if (strcmp(cmd, "start") == 0 && arg1 && arg2 && !arg3) // start worker
        {
            int p = find_string_in_array(place_str, _PLACE_COUNT, arg2);
            if (p >= 0)
                start_worker_cmd(&F, arg1, (enum place)p);
            else
                fprintf(stderr, "Invalid command: start %s %s (bad place)\n", arg1, arg2);
        }
        else if (strcmp(cmd, "make") == 0 && arg1 && !arg2) // make product
        {
            int pr = find_string_in_array(product_str, _PRODUCT_COUNT, arg1);
            if (pr >= 0)
                make_cmd(&F, (enum product)pr);
            else
                fprintf(stderr, "Invalid command: make %s (bad product)\n", arg1);
        }
        else if (strcmp(cmd, "end") == 0 && arg1 && !arg2) // end worker
        {
            end_worker_cmd(&F, arg1);
        }
        else if (strcmp(cmd, "add") == 0 && arg1 && !arg2) // add place
        {
            int p = find_string_in_array(place_str, _PLACE_COUNT, arg1);
            if (p >= 0)
                add_place_cmd(&F, (enum place)p);
            else
                fprintf(stderr, "Invalid command: add %s (bad place)\n", arg1);
        }
        else if (strcmp(cmd, "remove") == 0 && arg1 && !arg2) // remove place
        {
            int p = find_string_in_array(place_str, _PLACE_COUNT, arg1);
            if (p >= 0)
                remove_place_cmd(&F, (enum place)p);
            else
                fprintf(stderr, "Invalid command: remove %s (bad place)\n", arg1);
        }
        else
        {
            fprintf(stderr, "Invalid command: %s\n", cmd);
        }
    }
    free(line);

    /* Enter EOF mode; workers will leave when their profession is not needed - cleanup */
    pthread_mutex_lock(&F.mtx);
    F.closing = true;
    pthread_cond_broadcast(&F.cv);
    pthread_mutex_unlock(&F.mtx);

    // Synchronize with all workers
    for (struct Worker *w = F.workers; w; w = w->next)
        pthread_join(w->tid, NULL);

    struct Worker *w = F.workers;
    while (w)
    {
        struct Worker *n = w->next;
        free(w->name);
        free(w);
        w = n;
    }
    F.workers = NULL;

    factory_destroy(&F);
    return 0;
}
