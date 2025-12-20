#include "common.h"
#include "syscall.h"
#include <stdio.h>
#include "queue.h"
#include <pthread.h>

static pthread_mutex_t regs_lock = PTHREAD_MUTEX_INITIALIZER;
int __sys_print_regs(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    
    pthread_mutex_lock(&regs_lock);
    struct pcb_t *caller = NULL;
    struct queue_t *q = krnl->running_list;

    for (int i = 0; i < q->size; ++i) {
        struct pcb_t *proc = q->proc[i];
        if (proc->pid == pid) {
            caller = proc;
            break;
        }
    }

    if (caller == NULL) {
        printf("ERROR: Cannot find process with PID %d\n", pid);
        return -1;
    }

    printf("--- [SYSCALL PRINT REGS] PID: %d ---\n", pid);
    for (int i = 0; i < 10; i++) {
        printf("  Reg[%d] = %lu (0x%lx)\n", i, (unsigned long)caller->regs[i], (unsigned long)caller->regs[i]);
    }
    printf("------------------------------------\n");
    pthread_mutex_unlock(&regs_lock);
    return 0;
}