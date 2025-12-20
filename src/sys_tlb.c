#include "common.h"
#include "syscall.h"
#include "os-mm.h"
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t sys_tlb_lock = PTHREAD_MUTEX_INITIALIZER;
int __sys_print_tlb(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    pthread_mutex_lock(&sys_tlb_lock);
    printf("--- [SYSCALL TLB DUMP] Request from PID: %d ---\n", pid);

#ifdef MM_PAGING
    if (krnl->tlb == NULL) {
        printf("Warning: TLB structure is NOT initialized in Kernel.\n");
        return -1;
    }
    tlb_dump(krnl->tlb);
#else
    printf("Error: MM_PAGING is not defined. TLB not supported.\n");
#endif
pthread_mutex_unlock(&sys_tlb_lock);
    return 0;
}