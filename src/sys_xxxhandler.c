// src/sys_xxxhandler.c
#include "common.h"
#include "syscall.h"
#include "stdio.h"

int __sys_xxxhandler(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
    printf("The first system call parameter %d\n", regs->a1); 
    
    return 0;
}