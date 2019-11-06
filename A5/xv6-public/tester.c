#include "types.h"
#include "stat.h"
#include "user.h"
#include "procstat.h"

// Parent forks two children, waits for them to exit and then finally exits
int main(int argc, char* argv[]) {
#ifdef FCFS
    int count = 10, lim = 1e7;
    for (int j = 0; j < count; j++) {
        if (fork() == 0) {
            volatile int a = 0;
            for (volatile int i = 0; i <= lim; i++) {
                a += 3;
            }
            printf(1, "%d\n", a);
            exit();
        }
    }

    for (int i = 0; i < count; i++) {
        wait();
    }

    exit();
#endif
#ifdef PBS
    // forks ten processes with priorities initially of the value pid/2,
    // but after that halfway through it resets the priorities to 1000-j/2
    int count = 10, lim = 1e7, halfLim = lim / 2;
    for (int j = 0; j < count; j++) {
        if (fork() == 0) {
            volatile int a = 0;
            for (volatile int i = 0; i <= lim; i++) {
                if (i == halfLim) {
                    set_priority(100 - j / 2);
                } else {
                    a += 3;
                }
            }
            printf(1, "%d\n", a);
            exit();
        }
    }

    for (int i = 0; i < count; i++) {
        wait();
    }

    exit();
#endif
#ifdef MLFQ
    struct proc_stat* ps = (struct proc_stat*)malloc(sizeof(struct proc_stat));

    printf(1, "Starting MLFQ testing - fork process\n");
    const int count = 10, lim = 1e7;
    int pids[count];

    for (int j = 0; j < count; j++) {
        int pid = fork();
        if (pid < 0)
            printf(1, "Fork failed!!\n");
        else if (pid == 0) {
            volatile int a = 0;

            for (volatile int i = 0; i <= lim; i++) {
                if (i % (lim / 10) == 0) {
                    getpinfo(ps, getpid());
                    printf(1, "Status %d of proc %d: RT %d NR %d Q %d TQ %d\n",
                           i / (lim / 10), ps->pid, ps->runtime, ps->num_run,
                           ps->allotedQ[0], ps->ticks[ps->allotedQ[0]]);
                } else {
                    a += 3;
                }
            }
            getpinfo(ps, getpid());
            printf(1, "Status of proc %d: RT %d NR %d Q %d TQ %d\n", ps->pid,
                   ps->runtime, ps->num_run, ps->allotedQ[0],
                   ps->ticks[ps->allotedQ[0]]);
            exit();
        }
    }

    for (int i = 0; i < count; i++) {
        wait();
    }

    exit();
#endif
}