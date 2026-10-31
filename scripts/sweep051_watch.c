/* SWEEP-051 SW-1261: ptrace hardware-watchpoint tracer.
 * Runs minwin_loader under ptrace, sets DR0 = watch addr (write, 8 bytes),
 * reports every SIGTRAP with the guest RIP/RSP that performed the store,
 * then continues. Usage: mw_watch <loader> <args...> --watch <hexaddr>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/types.h>
#include <stddef.h>
#include <signal.h>
#include <sys/personality.h>

static unsigned long watch_addr = 0;
static int max_hits = 40;

static int set_dr(pid_t pid) {
    /* DR0 = watch address, DR7 = enable local RW0=01 (write), LEN0=11 (8 bytes) */
    unsigned long dr7 = 0;
    dr7 |= (1UL << 0);        /* L0 local enable for DR0 */
    dr7 |= (1UL << 16);       /* R/W0 = 01 -> break on writes */
    dr7 |= (1UL << 19);       /* LEN0 = 10 -> 8 bytes */
    if (ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg) + 8 * 0, (void*)watch_addr) < 0) {
        perror("POKEUSER DR0"); return -1;
    }
    if (ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg) + 8 * 7, (void*)dr7) < 0) {
        perror("POKEUSER DR7"); return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s loader args... --watch hexaddr [maxhits]\n", argv[0]); return 1; }
    int sep = -1;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--watch")) { sep = i; break; }
    if (sep < 0 || sep + 1 >= argc) { fprintf(stderr, "no --watch\n"); return 1; }
    watch_addr = strtoul(argv[sep + 1], NULL, 16);
    if (sep + 2 < argc) max_hits = atoi(argv[sep + 2]);
    if (sep - 1 < 1) { fprintf(stderr, "no loader\n"); return 1; }

    pid_t pid = fork();
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) { perror("TRACEME"); _exit(127); }
        /* deterministic mmap layout so a no-watch run's addresses are reusable */
        personality(ADDR_NO_RANDOMIZE);
        execv(argv[1], &argv[1]);
        perror("execv"); _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);           /* exec stop */
    ptrace(PTRACE_SETOPTIONS, pid, 0, (void*)(PTRACE_O_EXITKILL));
    if (set_dr(pid) < 0) { kill(pid, 9); return 2; }
    fprintf(stderr, "[WATCH] DR0=0x%lx write/8B armed (max %d hits)\n", watch_addr, max_hits);

    int hits = 0;
    while (1) {
        if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) break;
        if (waitpid(pid, &status, 0) < 0) break;
        if (WIFEXITED(status)) { fprintf(stderr, "[WATCH] child exit %d (hits=%d)\n", WEXITSTATUS(status), hits); break; }
        if (WIFSIGNALED(status)) { fprintf(stderr, "[WATCH] child killed by %d (hits=%d)\n", WTERMSIG(status), hits); break; }
        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            if (sig == SIGTRAP) {
                unsigned long dr6 = ptrace(PTRACE_PEEKUSER, pid, offsetof(struct user, u_debugreg) + 8 * 6, NULL);
                if (dr6 & 0x1) { /* DR0 hit */
                    struct user_regs_struct r;
                    ptrace(PTRACE_GETREGS, pid, NULL, &r);
                    hits++;
                    fprintf(stderr, "[WATCH-HIT %d] store at RIP=0x%llx RSP=0x%llx RAX=0x%llx RCX=0x%llx RDX=0x%llx RSI=0x%llx RDI=0x%llx R8=0x%llx R9=0x%llx\n",
                            hits, (unsigned long long)r.rip, (unsigned long long)r.rsp,
                            (unsigned long long)r.rax, (unsigned long long)r.rcx,
                            (unsigned long long)r.rdx, (unsigned long long)r.rsi,
                            (unsigned long long)r.rdi, (unsigned long long)r.r8,
                            (unsigned long long)r.r9);
                    /* stack: return address + 8 qwords */
                    fprintf(stderr, "[WATCH-HIT %d] stack:", hits);
                    for (int si = 0; si < 8; si++) {
                        errno = 0;
                        long w = ptrace(PTRACE_PEEKDATA, pid, (void*)(r.rsp + 8 * si), NULL);
                        if (errno) break;
                        fprintf(stderr, " %llx", (unsigned long long)w);
                    }
                    fprintf(stderr, "\n");
                    /* /proc maps at hit time */
                    {
                        char mp[64];
                        snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
                        FILE* mf = fopen(mp, "r");
                        if (mf) {
                            char line[512];
                            while (fgets(line, sizeof line, mf)) {
                                unsigned long ms = 0, me = 0;
                                if (sscanf(line, "%lx-%lx", &ms, &me) == 2) {
                                    if (r.rip >= ms && r.rip < me) {
                                        fprintf(stderr, "[WATCH-HIT %d] RIP-MAP %s", hits, line);
                                    }
                                    if (r.rsp >= ms && r.rsp < me) {
                                        fprintf(stderr, "[WATCH-HIT %d] RSP-MAP %s", hits, line);
                                    }
                                }
                            }
                            fclose(mf);
                        }
                    }
                    /* clear DR6 status bit and continue */
                    ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg) + 8 * 6, (void*)0);
                    if (hits >= max_hits) {
                        fprintf(stderr, "[WATCH] max hits reached; killing\n");
                        kill(pid, 9);
                        break;
                    }
                    continue;
                }
                /* non-watchpoint trap: forward nothing, continue */
                continue;
            }
            /* forward other signals (e.g. the loader's own SIGSEGV handling?) */
            ptrace(PTRACE_CONT, pid, NULL, (void*)(long)sig);
            /* note: double-cont; loop continues with wait */
            if (waitpid(pid, &status, 0) < 0) break;
            if (WIFEXITED(status)) { fprintf(stderr, "[WATCH] exit %d\n", WEXITSTATUS(status)); break; }
        }
    }
    return 0;
}
