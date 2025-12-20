/* src/sys_pgtbl.c */
#include "common.h"
#include "syscall.h"
#include "mm64.h" 
#include <stdio.h>
#include "queue.h"
#include <pthread.h>

static pthread_mutex_t dump_lock = PTHREAD_MUTEX_INITIALIZER;

int __sys_printpgtbl(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    pthread_mutex_lock(&dump_lock);
    
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
        printf("ERROR: Cannot find process with PID %d to print Page Table\n", pid);
        return -1;
    }

    /* 2. Gọi hàm in bảng trang có sẵn */
    printf("--- [SYSCALL PRINT PGTBL] Request from PID: %d ---\n", pid);
    
    // Tham số 0, -1 nghĩa là in toàn bộ dải địa chỉ hợp lệ
    print_pgtbl(caller, 0, -1); 
    pthread_mutex_unlock(&dump_lock);
    return 0;
}