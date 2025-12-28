#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef SIGTERM
#define SIGTERM 15
#endif
typedef void (*sighandler_t)(int);
extern sighandler_t signal(int, sighandler_t);
extern int kill(pid_t, int);

#define TRY(x) do { if ((x) == -1) _exit(2); } while (0)

static void gen_term(int s) {
    (void)s;
    write(STDERR_FILENO, "GEN TERMINATED\n", 15);
    _exit(0);
}

static void run_gen(int r, int w) {
    TRY(close(r));
    TRY(dup2(w, STDOUT_FILENO));
    TRY(close(w));

    if (signal(SIGTERM, gen_term) == (sighandler_t)-1) _exit(2);

    srand((unsigned)getpid());
    for (;;) {
        int a = rand() % 4096, b = rand() % 4096;
        if (dprintf(STDOUT_FILENO, "%d %d\n", a, b) < 0) _exit(2);
        (void)sleep(1);
    }
}

static void run_nsd(int r, int w) {
    TRY(close(w));
    TRY(dup2(r, STDIN_FILENO));
    TRY(close(r));
    execl("./nsd", "nsd", (char*)NULL);
    _exit(2);
}

int main(void) {
    int p[2]; TRY(pipe(p));

    pid_t gen = fork(); if (gen < 0) _exit(2);
    if (gen == 0) run_gen(p[0], p[1]);

    pid_t nsd = fork(); if (nsd < 0) _exit(2);
    if (nsd == 0) run_nsd(p[0], p[1]);

    TRY(close(p[0])); TRY(close(p[1]));

    if (sleep(5) != 0) _exit(2);
    TRY(kill(gen, SIGTERM));

    int st, ok = 1;
    for (int i = 0; i < 2; ++i) {
        if (wait(&st) == -1) _exit(2);
        if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) ok = 0;
    }
    puts(ok ? "OK" : "ERROR");
    return ok ? 0 : 1;
}
