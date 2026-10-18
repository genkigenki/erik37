/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* icount PROG ARGS: user-mode instructions PROG executes between raise(SIGUSR1) and raise(SIGUSR2), counted by
 * single-stepping (ptrace; for hosts without hardware counters, as WSL2). Linux only. */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
    pid_t p;
    int st;
    unsigned long long n = 0;
    if (argc < 2)
        return 2;
    p = fork();
    if (!p) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execv(argv[1], argv + 1);
        _exit(127);
    }
    for (;;) {
        if (waitpid(p, &st, 0) < 0 || WIFEXITED(st) || WIFSIGNALED(st))
            break;
        if (WIFSTOPPED(st) && WSTOPSIG(st) == SIGUSR1) {
            ptrace(PTRACE_SINGLESTEP, p, 0, 0);
            for (;;) {
                waitpid(p, &st, 0);
                if (!WIFSTOPPED(st))
                    goto done;
                if (WSTOPSIG(st) == SIGUSR2)
                    break;
                n++;
                ptrace(PTRACE_SINGLESTEP, p, 0, WSTOPSIG(st) == SIGTRAP ? 0 : WSTOPSIG(st));
            }
            printf("icount %llu\n", n);
            fflush(stdout);
            ptrace(PTRACE_CONT, p, 0, 0);
            continue;
        }
        ptrace(PTRACE_CONT, p, 0, WIFSTOPPED(st) && WSTOPSIG(st) != SIGTRAP ? WSTOPSIG(st) : 0);
    }
done:
    return 0;
}
