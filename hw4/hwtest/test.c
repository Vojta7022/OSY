// prod-cons.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>     // sysconf
#include <pthread.h>

typedef struct item {
    int x;
    char *text;          // allocated by scanf("%ms")
    struct item *next;
} item_t;

typedef struct {
    item_t *head;
    item_t *tail;
    size_t size;
} queue_t;

// Global shared state
static queue_t queue = { .head = NULL, .tail = NULL, .size = 0 };
static pthread_mutex_t queue_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_cv  = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t io_mtx = PTHREAD_MUTEX_INITIALIZER; // for atomic stdout printing

static bool done = false;         // producer finished (EOF or invalid input encountered)
static bool input_error = false;  // invalid input was seen

// Queue ops (no locking inside; caller must hold queue_mtx)
static void enqueue_unlocked(queue_t *q, item_t *it) {
    if (!q->head) {
        q->head = it;
    } else {
        q->tail->next = it;
    }
    q->tail = it;
    q->size++;
}

static item_t* dequeue_unlocked(queue_t *q) {
    item_t *it = q->head;
    if (it) {
        q->head = it->next;
        if (!q->head) q->tail = NULL;
        q->size--;
    }
    return it;
}

// Thread start routines (we'll implement later)
static void* producer_thread(void *arg) {
    (void)arg;
    return NULL;
}

static void* consumer_thread(void *arg) {
    (void)arg;
    return NULL;
}

// Parse N from argv[1], check range [1..cpu_count]; on invalid -> exit(1)
static int parse_consumer_count(int argc, char **argv) {
    long n = 1;
    if (argc >= 2) {
        char *endptr;
        errno = 0;
        n = strtol(argv[1], &endptr, 10);
        if (errno != 0 || *endptr != '\0' || n < 1) {
            exit(1);
        }
    }
    long cpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu < 1) cpu = 1; // fallback

    if (n < 1 || n > cpu) {
        // per assignment: invalid N -> exit code 1
        exit(1);
    }
    return (int)n;
}

int main(int argc, char **argv) {
    int N = parse_consumer_count(argc, argv);

    // TODO: create 1 producer thread + N consumer threads (IDs 1..N)
    // For now: just print N and return 0 so we can compile/test the skeleton.

    printf("OK, N=%d (skeleton)\n", N);
    return 0;
}
