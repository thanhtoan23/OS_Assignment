/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef OSMM_H
#define OSMM_H

#include <stdint.h>

#define MM_PAGING
#define PAGING_MAX_MMSWP 4 /* max number of supported swapped space */
#define PAGING_MAX_SYMTBL_SZ 30

/* TLB Configuration */
#define TLB_SIZE 64        /* Số lượng entry trong TLB */
#define TLB_ENTRY_INVALID 0
#define TLB_ENTRY_VALID   1

/* 
 * @bksysnet: in long address mode of 64bit or original 32bit
 * the address type need to be redefined
 */

#ifdef MM64
#define ADDR_TYPE uint64_t
#else
#define ADDR_TYPE uint32_t
#endif

typedef char BYTE;
typedef ADDR_TYPE addr_t;

/* 
 * @bksysnet: the format string need to be redefined
 *            based on the address mode
 */
#ifdef MM64
#define FORMAT_ADDR "%ld"
#define FORMATX_ADDR "%16llx"
#else
#define FORMAT_ADDR "%d"
#define FORMATX_ADDR "%08x"
#endif

/* TLB Entry Structure (LRU implementation) */
struct tlb_entry_t {
    addr_t vpn;           /* Virtual Page Number */
    addr_t fpn;           /* Frame Physical Number */
    uint8_t valid;        /* Valid bit */
    uint8_t dirty;        /* Dirty bit */
    uint8_t referenced;   /* Referenced bit */
    uint32_t pid;         /* Process ID for multi-process TLB */
    uint64_t last_used;   /* Timestamp for LRU - incremented on each access */
    struct tlb_entry_t *next; /* For chaining in hash table */
};

/* TLB Structure with LRU support */
struct tlb_t {
    struct tlb_entry_t *entries[TLB_SIZE];  /* Hash table chaining */
    int hits;
    int misses;
    int size;
    uint64_t access_counter; /* Global counter for LRU timestamp */
};

struct pgn_t{
   addr_t pgn;
   struct pcb_t *owner;
   struct pgn_t *pg_next; 
};

/*
 *  Memory region struct
 */
struct vm_rg_struct {
   addr_t rg_start;
   addr_t rg_end;
   struct vm_rg_struct *rg_next;
};

/*
 *  Memory area struct
 */
struct vm_area_struct {
   unsigned long vm_id;
   addr_t vm_start;
   addr_t vm_end;
   addr_t sbrk;
   struct mm_struct *vm_mm;
   struct vm_rg_struct *vm_freerg_list;
   struct vm_area_struct *vm_next;
};

/* 
 * Memory management struct
 */
struct mm_struct {
#ifdef MM64
   addr_t *pgd;
   addr_t *p4d;
   addr_t *pud;
   addr_t *pmd;
   addr_t *pt;
#else
   uint32_t *pgd;
#endif

   struct vm_area_struct *mmap;

   /* Currently we support a fixed number of symbol */
   struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

   struct pgn_t *fifo_pgn;
   struct pgn_t *clock_hand;
};

/*
 * FRAME/MEM PHY struct
 */
struct framephy_struct { 
   addr_t fpn;
   struct framephy_struct *fp_next;
   struct mm_struct* owner;
};

struct memphy_struct {
   BYTE *storage;
   int maxsz;
   int rdmflg;
   int cursor;
   struct framephy_struct *free_fp_list;
   struct framephy_struct *used_fp_list;
};

#endif