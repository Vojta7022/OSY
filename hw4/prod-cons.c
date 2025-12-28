#define _GNU_SOURCE // Enable GNU extensions (like %ms in scanf)
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// Node structure for a singly-linked queue
// Holds one work item: a number (x) and a string (word)
typedef struct Node
{
    int x;             // Number of times to repeat the word
    char *word;        // Dynamically allocated string
    struct Node *next; // Pointer to next node in queue
} Node;

// Shared queue (FIFO) between producer and consumers
static Node *head = NULL, *tail = NULL;

// Mutex protecting the queue (head, tail, done flag)
static pthread_mutex_t qmtx = PTHREAD_MUTEX_INITIALIZER;

// Condition variable: signals when new work is added or producer is done
static pthread_cond_t qcv = PTHREAD_COND_INITIALIZER;

// Mutex protecting stdout to prevent interleaved output from consumers
static pthread_mutex_t outmtx = PTHREAD_MUTEX_INITIALIZER;

// Flag: set to 1 when producer finishes reading input (EOF or error)
static int done = 0;

// Flag: set to 1 if invalid input detected (negative number or scanf error)
static int invalid_input = 0;

// Number of consumer threads
static int N = 1;

// Enqueue: Add a node to the tail of the queue
// Caller must hold qmtx
static void enqueue(Node *n)
{
    // If queue is empty, new node becomes both head and tail
    if (tail)
        tail->next = n;
    else
        head = n;
    tail = n;
    n->next = NULL; // New tail has no successor
}

// Dequeue: Remove and return node from head of queue
// Caller must hold qmtx
// Returns NULL if queue is empty
static Node *dequeue(void)
{
    Node *n = head;
    if (n)
    {
        head = n->next; // Move head forward
        if (!head)
            tail = NULL; // If queue now empty, clear tail too
    }
    return n;
}

// Producer thread: reads input and enqueues work items
static void *producer(void *arg)
{
    (void)arg; // Unused parameter
    int x, ret;
    char *text;

    // Read pairs of (number, string) from stdin
    // %ms allocates memory for the string automatically
    while ((ret = scanf("%d %ms", &x, &text)) == 2)
    {
        if (x < 0)
        { // Reject negative numbers
            free(text);
            invalid_input = 1;
            break;
        }

        // Create a new work item
        Node *n = (Node *)malloc(sizeof(Node));
        n->x = x;
        n->word = text;
        n->next = NULL;

        // Add to queue and signal waiting consumers
        pthread_mutex_lock(&qmtx);
        enqueue(n);
        pthread_cond_signal(&qcv); // Wake one waiting consumer
        pthread_mutex_unlock(&qmtx);
    }

    // Input finished (EOF or error)
    pthread_mutex_lock(&qmtx);
    done = 1; // Tell consumers no more work is coming

    // If scanf failed for a reason other than EOF, mark invalid
    if (ret != EOF)
        invalid_input = 1;

    // Wake ALL consumers so they can exit
    for (int i = 0; i < N; ++i)
        pthread_cond_signal(&qcv);
    pthread_mutex_unlock(&qmtx);

    return NULL;
}

// Consumer thread: processes work items from the queue
static void *consumer(void *arg)
{
    int id = *(int *)arg; // Thread ID for output

    for (;;)
    {
        pthread_mutex_lock(&qmtx);

        // Wait for work to arrive or producer to finish
        // Standard condition variable pattern: loop to handle spurious wakeups
        while (!head && !done)
        {
            pthread_cond_wait(&qcv, &qmtx); // Atomically unlock and wait
        }

        Node *n = dequeue();            // Try to get work
        int should_exit = (!n && done); // Exit if queue empty AND producer done
        pthread_mutex_unlock(&qmtx);

        if (should_exit)
            break; // Clean exit
        if (!n)
            continue; // Spurious wakeup; retry

        // Process the work item outside the queue lock
        // Lock output mutex to prevent interleaved lines from different threads
        pthread_mutex_lock(&outmtx);
        printf("Thread %d:", id);
        for (int i = 0; i < n->x; ++i)
        {
            printf(" %s", n->word); // Print word x times
        }
        printf("\n");
        fflush(stdout); // Ensure output appears immediately
        pthread_mutex_unlock(&outmtx);

        // Free the work item's memory
        free(n->word);
        free(n);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    // Parse command-line argument: number of consumer threads
    long tmpN = 1;
    if (argc >= 2)
    {
        char *end = NULL;
        tmpN = strtol(argv[1], &end, 10);
        // Validate: must be positive integer with no trailing garbage
        if (!end || *end || tmpN < 1)
            return 1;
    }

    // Validate N is within allowed range (1 to CPU count)
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (tmpN < 1 || tmpN > cpus)
        return 1;
    N = (int)tmpN;

    // Allocate thread handles and IDs
    pthread_t prod;
    pthread_t *cons = (pthread_t *)malloc(sizeof(pthread_t) * N);
    int *ids = (int *)malloc(sizeof(int) * N);

    // Start N consumer threads (IDs 1..N)
    for (int i = 0; i < N; ++i)
    {
        ids[i] = i + 1;
        pthread_create(&cons[i], NULL, consumer, &ids[i]);
    }

    // Start producer thread
    pthread_create(&prod, NULL, producer, NULL);

    // Wait for producer to finish
    pthread_join(prod, NULL);

    // Wait for all consumers to finish
    for (int i = 0; i < N; ++i)
    {
        pthread_join(cons[i], NULL);
    }

    // Clean up dynamically allocated memory
    free(ids);
    free(cons);

    // Drain any remaining items in queue (shouldn't happen in normal operation)
    while (head)
    {
        Node *n = dequeue();
        free(n->word);
        free(n);
    }

    // Destroy synchronization primitives
    pthread_mutex_destroy(&qmtx);
    pthread_mutex_destroy(&outmtx);
    pthread_cond_destroy(&qcv);

    // Exit with error code if invalid input was detected
    return invalid_input ? 1 : 0;
}
