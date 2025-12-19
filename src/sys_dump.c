#include "common.h"
#include "syscall.h"
#include "os-mm.h" 
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t dump_lock = PTHREAD_MUTEX_INITIALIZER;

int __sys_dump(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    
    printf("--- [SYSCALL DUMP] Request from PID: %d ---\n", pid);

    if (krnl->mram == NULL) {
        printf("Error: Physical memory (MRAM) is not initialized.\n");
        return -1;
    }
    MEMPHY_dump(krnl->mram);
    pthread_mutex_unlock(&dump_lock);

    return 0;
}