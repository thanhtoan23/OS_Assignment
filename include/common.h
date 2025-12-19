#ifndef COMMON_H
#define COMMON_H

/* Define structs and routine could be used by every source files */

#include <stdint.h>
#include <stdio.h>

#ifndef OSCFG_H
#include "os-cfg.h"
#endif

#ifndef OSMM_H
#include "os-mm.h"
#endif

#define ADDRESS_SIZE 20
#define OFFSET_LEN 10
#define FIRST_LV_LEN 5
#define SECOND_LV_LEN 5
#define SEGMENT_LEN FIRST_LV_LEN
#define PAGE_LEN SECOND_LV_LEN

#define NUM_PAGES (1 << (ADDRESS_SIZE - OFFSET_LEN))
#define PAGE_SIZE (1 << OFFSET_LEN)

/* 
 * @bksysnet: long address mode of 64bit argument support
 */

#ifdef MM64
#define ARG_TYPE uint64_t	
#else
#define ARG_TYPE uint32_t
#endif

typedef ARG_TYPE arg_t;

/* 
 * @bksysnet: the format string need to be redefined
 *            based on the address mode
 */
#ifdef MM64
#define FORMAT_ARG "%lu"
#else
#define FORMAT_ARG "%u"
#endif

enum ins_opcode_t
{
	CALC,  // Just perform calculation, only use CPU
	ALLOC, // Allocate memory
	FREE,  // Deallocated a memory block
	READ,  // Write data to a byte on memory
	WRITE, // Read data from a byte on memory
	SYSCALL,
};

/* instructions executed by the CPU */
struct inst_t
{
	enum ins_opcode_t opcode;
	arg_t arg_0;
	arg_t arg_1;
	arg_t arg_2;
	arg_t arg_3;
	arg_t arg_4;
	arg_t arg_5;
};

struct code_seg_t
{
	struct inst_t *text;
	uint32_t size;
};

struct trans_table_t
{
	struct
	{
		addr_t v_index;
		addr_t p_index;
	} table[1 << SECOND_LV_LEN];
	int size;
};

struct page_table_t
{
	struct
	{
		addr_t v_index;
		struct trans_table_t *next_lv;
	} table[1 << FIRST_LV_LEN];
	int size;
};

/* PCB, describe information about a process */
struct pcb_t
{
	uint32_t pid;		 // PID
	uint32_t priority;	 // Default priority
	char path[100];
	struct code_seg_t *code;
	addr_t regs[10];
	uint64_t pc;
#ifdef MLQ_SCHED
	uint32_t prio;
#endif

#ifdef MM_PAGING
    struct mm_struct *mm;         
    struct memphy_struct *mram;
    struct memphy_struct **mswp;
    struct memphy_struct *active_mswp;
#endif

	struct krnl_t *krnl;	
	struct page_table_t *page_table;
	uint64_t bp;
};

/* Kernel structure */
struct krnl_t
{
	struct queue_t *ready_queue;
	struct queue_t *running_list;
#ifdef MLQ_SCHED
	struct queue_t *mlq_ready_queue;
#endif
#ifdef MM_PAGING
	struct mm_struct *mm;
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	uint32_t active_mswp_id;
	struct tlb_t *tlb;           /* <--- ADD TLB HERE */
#endif
};

#endif