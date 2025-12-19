/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */
/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * System Library
 * Memory Module Library libmem.c  
 * IMPROVED VERSION WITH DIRTY BIT SUPPORT
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 * 
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1; // Không hợp lệ (start ≥ end)

  // Liên kết danh sách
  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 * 
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 * 
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
   /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  addr_t inc_sz=0;
  
  if (cur_vma == NULL) {
    printf("DEBUG ERROR: cur_vma is NULL for vmaid %d. Process memory not initialized?\n", vmaid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
   }

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    caller->mm->symrgtbl[rgid].rg_next = NULL;
    
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  
  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
  
 #ifdef MM64
  inc_sz = PAGING64_PAGE_ALIGNSZ(size); 
 #else
  inc_sz = PAGING_PAGE_ALIGNSZ(size); 
 #endif
  
  int old_sbrk;
  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  //regs.a3 = size;
  regs.a3 = PAGING64_PAGE_ALIGNSZ(size);
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
   
  syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */

  int new_sbrk = cur_vma->sbrk;
  struct vm_rg_struct *new_rg_free = init_vm_rg(old_sbrk, new_sbrk);
  enlist_vm_rg_node(&cur_vma->vm_freerg_list, new_rg_free);

  /*Successful increase limit */
  if (get_free_vmrg_area(caller, vmaid, inc_sz, &rgnode) == 0){
    caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
    caller->mm->symrgtbl[rgid].rg_end = old_sbrk + inc_sz;
    caller->mm->symrgtbl[rgid].rg_next = NULL;
    *alloc_addr = old_sbrk;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory (LAZY FREE IMPLEMENTATION)
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 * 
 * IMPLEMENTATION NOTES:
 * - Lazy free: Only updates logical structures, does NOT free physical frames immediately
 * - Physical frames are reclaimed only when process terminates or during page replacement
 * - Region is reset in symbol table and added to free region list for future allocations
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);
  
  printf("[FREE LAZY] PID=%d, vmaid=%d, rgid=%d\n", caller->pid, vmaid, rgid);

  /* 1. Validate region ID */
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    printf("ERROR: Invalid rgid %d\n", rgid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* 2. Get region from symbol table */
  struct vm_rg_struct *rgnode = &caller->mm->symrgtbl[rgid];
  
  /* 3. Check if already freed */
  if (rgnode->rg_start == 0 && rgnode->rg_end == 0) {
    printf("WARNING: Region %d already freed\n", rgid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t start_addr = rgnode->rg_start;
  addr_t end_addr = rgnode->rg_end;
  
  if (start_addr >= end_addr) {
    printf("ERROR: Invalid region (start >= end)\n");
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* 4. Create free region node */
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (!freerg_node) {
    printf("ERROR: malloc failed\n");
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  
  freerg_node->rg_start = start_addr;
  freerg_node->rg_end = end_addr;
  freerg_node->rg_next = NULL;

  /* 5. Reset symbol table entry */
  rgnode->rg_start = 0;
  rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /* 6. Add to free list */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (!cur_vma) {
    printf("ERROR: Cannot find vma %d\n", vmaid);
    free(freerg_node);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Add to head of free list (simple LIFO) */
  freerg_node->rg_next = cur_vma->vm_freerg_list;
  cur_vma->vm_freerg_list = freerg_node;

  printf("  Freed region [%lu-%lu] (size=%lu). Physical frames NOT freed (LAZY).\n",
         start_addr, end_addr, end_addr - start_addr);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbol table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);

  if (val == -1)
  {
    return -1;
  }

  proc->regs[reg_index] = addr;
printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
*/
int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);

  if (val == -1)
  {
    return -1;
  }
printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
    uint32_t old_pte = pte_get_entry(caller, pgn);
    
    printf("=== pg_getpage DEBUG ===\n");
    printf("PID: %d, Request page: pgn=%d, old_pte=0x%08x\n",
            caller->pid, pgn, old_pte);
    printf("Page Present? %s\n",
            PAGING_PAGE_PRESENT(old_pte) ? "YES" : "NO");
    printf("Page Swapped? %s\n",
            (old_pte & PAGING_PTE_SWAPPED_MASK) ? "YES" : "NO");
    printf("Page Dirty? %s\n",
            (old_pte & PAGING_PTE_DIRTY_MASK) ? "YES" : "NO");

    if (!PAGING_PAGE_PRESENT(old_pte))
    {
        printf(">>> PAGE FAULT TRIGGERED! <<<\n");
        addr_t tgtfpn;
        struct sc_regs regs;
        int is_swapped = (old_pte & PAGING_PTE_SWAPPED_MASK);
        addr_t old_swpfpn = 0;
        int old_swp_id = 0; // Thêm biến lưu ID swap cũ

        if (is_swapped) {
            old_swpfpn = PAGING_SWP(old_pte);
            old_swp_id = PAGING_PTE_GET_SWPTYP(old_pte); // Lấy swap device ID từ PTE
            printf("Page is in SWAP %d at swpfpn=%lu\n", old_swp_id, old_swpfpn);
        } else {
            printf("Page not in SWAP (first access)\n");
        }
        
        // --- 1. CỐ GẮNG LẤY FRAME TRỐNG TRONG RAM ---
        if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0) 
        {
            printf("RAM has free frame: fpn=%lu\n", tgtfpn);
            
            if (is_swapped) 
            {
                printf("SWAP IN: SWAP %d(%lu) -> RAM(%lu)\n",
                        old_swp_id, old_swpfpn, tgtfpn);
                
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = old_swpfpn;
                regs.a3 = tgtfpn;
                regs.a4 = 1;          // Direction IN
                regs.a5 = old_swp_id; // Chọn đúng vùng swap chứa trang
                syscall(caller->krnl, caller->pid, 17, &regs);
                
                MEMPHY_put_freefp(caller->krnl->mswp[old_swp_id], old_swpfpn);
                printf("Freed swap frame %lu back to SWAP %d\n", old_swpfpn, old_swp_id);
                
                // SWAP IN: dirty = 0 (trang từ swap về chưa bị sửa)
                pte_set_fpn(caller, pgn, tgtfpn, 0);
                printf("Updated PTE for pgn=%d -> fpn=%lu (dirty=0, swap in)\n", pgn, tgtfpn);
            } 
            else 
            {
                printf("First allocation in RAM at fpn=%lu\n", tgtfpn);
                // TẠO MỚI: dirty = 1 (trang mới cấp phát cần được ghi xuống swap nếu bị đuổi)
                pte_set_fpn(caller, pgn, tgtfpn, 1);
                printf("Updated PTE for pgn=%d -> fpn=%lu (dirty=1, new page)\n", pgn, tgtfpn);
            }
        } 
        else 
        {
            // --- 2. RAM FULL - THAY THẾ TRANG (ROUND ROBIN) ---
            printf("RAM FULL! Need to find VICTIM for SWAP OUT\n");
            
            addr_t vicpgn, vicfpn, swpfpn;
            uint32_t vicpte;
            struct pcb_t *vic_owner;

            if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) == -1) {
                printf("ERROR: Cannot find victim page\n");
                return -1;
            }

            vicpte = pte_get_entry(vic_owner, vicpgn);
            vicfpn = PAGING_FPN(vicpte);
            int vic_is_dirty = PAGING_PTE_GET_DIRTY(vicpte);
            
            printf("Selected VICTIM: PID=%d, pgn=%lu, fpn=%lu, pte=0x%08x, dirty=%d\n",
                    vic_owner->pid, vicpgn, vicfpn, vicpte, vic_is_dirty);

            // CHỈ SWAP OUT NẾU VICTIM LÀ DIRTY
            if (vic_is_dirty) {
                /* ROUND ROBIN TRÊN MẢNG SWAP */
                int found_swp_id = -1;
                for (int i = 0; i < PAGING_MAX_MMSWP; i++) {
                    int swp_idx = (caller->krnl->active_mswp_id + i) % PAGING_MAX_MMSWP;
                    if (caller->krnl->mswp[swp_idx] != NULL && 
                        MEMPHY_get_freefp(caller->krnl->mswp[swp_idx], &swpfpn) == 0) {
                        found_swp_id = swp_idx;
                        caller->krnl->active_mswp_id = (swp_idx + 1) % PAGING_MAX_MMSWP;
                        break;
                    }
                }

                if (found_swp_id == -1) {
                    printf("ERROR: ALL SWAP DEVICES ARE FULL!\n");
                    return -1;
                }
                printf("Free SWAP frame obtained at SWAP %d: swpfpn=%lu\n", found_swp_id, swpfpn);

                // Swap Out: RAM -> SWAP được chọn
                printf("SWAP OUT: RAM(%lu) -> SWAP %d(%lu) because dirty=1\n", vicfpn, found_swp_id, swpfpn);
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = vicfpn;
                regs.a3 = swpfpn;
                regs.a4 = 0;            // Direction OUT
                regs.a5 = found_swp_id; // Chọn vùng swap đích
                syscall(caller->krnl, caller->pid, 17, &regs);

                // Cập nhật PTE nạn nhân với đúng ID vùng swap
                pte_set_swap(vic_owner, vicpgn, found_swp_id, swpfpn);
                printf("Updated VICTIM PTE (PID=%d, pgn=%lu) to point to SWAP %d(%lu)\n",
                        vic_owner->pid, vicpgn, found_swp_id, swpfpn);
            } else {
                printf("VICTIM is CLEAN (dirty=0), no need to write to SWAP\n");
                // Chỉ cần invalidate PTE của victim
                pte_set_entry(vic_owner, vicpgn, 0);
                printf("Invalidated VICTIM PTE (PID=%d, pgn=%lu)\n",
                        vic_owner->pid, vicpgn);
            }

            // Dùng lại frame vật lý của nạn nhân
            tgtfpn = vicfpn;
            printf("Victim frame %lu now available for new page\n", tgtfpn);

            if (is_swapped) 
            {
                printf("SWAP IN: SWAP %d(%lu) -> RAM(%lu)\n",
                        old_swp_id, old_swpfpn, tgtfpn);
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = old_swpfpn;
                regs.a3 = tgtfpn;
                regs.a4 = 1;          // Direction IN
                regs.a5 = old_swp_id; // Chọn đúng vùng swap nguồn
                syscall(caller->krnl, caller->pid, 17, &regs);

                MEMPHY_put_freefp(caller->krnl->mswp[old_swp_id], old_swpfpn);
                printf("Freed swap frame %lu back to SWAP %d\n", old_swpfpn, old_swp_id);
                
                // SWAP IN: dirty = 0
                pte_set_fpn(caller, pgn, tgtfpn, 0);
                printf("Updated PTE for pgn=%d -> fpn=%lu (dirty=0, swap in)\n", pgn, tgtfpn);
            } else {
                printf("New page allocated to RAM frame %lu\n", tgtfpn);
                // TẠO MỚI: dirty = 1
                pte_set_fpn(caller, pgn, tgtfpn, 1);
                printf("Updated PTE for pgn=%d -> fpn=%lu (dirty=1, new page)\n", pgn, tgtfpn);
            }
        }
        
        // Enlist vào danh sách FIFO
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn, caller);
        printf("Added pgn=%d (PID=%d) to FIFO list\n", pgn, caller->pid);
        
        *fpn = (int)tgtfpn;
    } else {
        /*
         * Trường hợp trang ĐÃ PRESENT (đã được khởi tạo/ánh xạ trong bảng trang)
         * Cần kiểm tra xem nó đang thực sự nằm ở RAM hay đang bị SWAP OUT.
         */
        int is_swapped = (old_pte & PAGING_PTE_SWAPPED_MASK);

        if (is_swapped) 
        {
            /*
             * TRƯỜNG HỢP 1: Trang đang ở SWAP
             */
            printf("Page is present but currently SWAPPED OUT. Triggering Swap-In...\n");
            
            addr_t old_swpfpn = PAGING_SWP(old_pte);
            int old_swp_id = PAGING_PTE_GET_SWPTYP(old_pte); // Lấy swap device ID
            addr_t tgtfpn;
            struct sc_regs regs;

            // 1. Tìm frame trống trong RAM
            if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) != 0) 
            {
                // Nếu RAM đầy, phải chọn nạn nhân để Swap Out
                addr_t vicpgn, vicfpn, swpfpn;
                uint32_t vicpte;
                struct pcb_t *vic_owner;

                if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) == -1) {
                    printf("ERROR: Cannot find victim page\n");
                    return -1;
                }

                vicpte = pte_get_entry(vic_owner, vicpgn);
                vicfpn = PAGING_FPN(vicpte);
                int vic_is_dirty = PAGING_PTE_GET_DIRTY(vicpte);

                // CHỈ SWAP OUT NẾU DIRTY
                if (vic_is_dirty) {
                    /* ROUND ROBIN TRÊN MẢNG SWAP */
                    int found_swp_id = -1;
                    for (int i = 0; i < PAGING_MAX_MMSWP; i++) {
                        int swp_idx = (caller->krnl->active_mswp_id + i) % PAGING_MAX_MMSWP;
                        if (caller->krnl->mswp[swp_idx] != NULL && 
                            MEMPHY_get_freefp(caller->krnl->mswp[swp_idx], &swpfpn) == 0) {
                            found_swp_id = swp_idx;
                            caller->krnl->active_mswp_id = (swp_idx + 1) % PAGING_MAX_MMSWP;
                            break;
                        }
                    }
                    if (found_swp_id == -1) return -1;

                    regs.a1 = SYSMEM_SWP_OP; 
                    regs.a2 = vicfpn; 
                    regs.a3 = swpfpn;
                    regs.a4 = 0;             // OUT
                    regs.a5 = found_swp_id;  // Chọn vùng swap đích
                    syscall(caller->krnl, caller->pid, 17, &regs);

                    pte_set_swap(vic_owner, vicpgn, found_swp_id, swpfpn);
                } else {
                    // Clean victim, no swap needed
                    pte_set_entry(vic_owner, vicpgn, 0);
                }

                tgtfpn = vicfpn;
            }

            // 2. Đưa trang từ Swap về RAM
            regs.a1 = SYSMEM_SWP_OP;
            regs.a2 = old_swpfpn;
            regs.a3 = tgtfpn;
            regs.a4 = 1;          // IN
            regs.a5 = old_swp_id; // Chọn đúng vùng swap nguồn
            syscall(caller->krnl, caller->pid, 17, &regs);

            // 3. Trả lại slot Swap cũ và cập nhật PTE
            MEMPHY_put_freefp(caller->krnl->mswp[old_swp_id], old_swpfpn);
            
            // SWAP IN: dirty = 0
            pte_set_fpn(caller, pgn, tgtfpn, 0);
            
            // 4. Đưa vào danh sách FIFO
            enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn, caller);
            
            *fpn = (int)tgtfpn;
        } 
        else 
        {
            /*
             * TRƯỜNG HỢP 2: Trang thực sự đang nằm trong RAM
             */
            printf("Page already in RAM\n");
            *fpn = PAGING_FPN(old_pte);
        }
    }

    printf("Returning fpn=%d for pgn=%d\n", *fpn, pgn);
    printf("=== End pg_getpage ===\n\n");
    return 0;
}

/*pg_getval - read value at given offset */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
    addr_t pgn = PAGING64_PGN(addr);
    addr_t off = PAGING64_OFFST(addr);
    int fpn;
    
    printf("READ: pid: %d, addr: %ld, pgn: %ld, off: %ld\n", 
           caller->pid, addr, pgn, off);
    
    /* 1. TRY TLB FIRST */
    addr_t tlb_fpn;
    if (caller->krnl->tlb && 
        tlb_lookup(caller->krnl->tlb, pgn, caller->pid, &tlb_fpn)) {
        /* TLB HIT */
        printf("  TLB HIT: VPN %lu -> FPN %lu\n", pgn, tlb_fpn);
        fpn = tlb_fpn;
        
        /* Update reference bit in PTE */
        uint32_t pte = pte_get_entry(caller, pgn);
        SETBIT(pte, PAGING_PTE_REFERENCED_MASK);
        pte_set_entry(caller, pgn, pte);
        
        /* Update referenced bit in TLB */
        tlb_set_referenced(caller->krnl->tlb, pgn, caller->pid);
    } else {
        /* TLB MISS - go through normal page lookup */
        printf("  TLB MISS for VPN %lu\n", pgn);
        
        if (pg_getpage(mm, pgn, &fpn, caller) != 0)
            return -1;
        
        /* Update reference bit */
        uint32_t pte = pte_get_entry(caller, pgn);
        SETBIT(pte, PAGING_PTE_REFERENCED_MASK);
        pte_set_entry(caller, pgn, pte);
        
        /* INSERT INTO TLB */
        if (caller->krnl->tlb) {
            int dirty = PAGING_PTE_GET_DIRTY(pte);
            int referenced = 1; /* Just accessed */
            tlb_insert(caller->krnl->tlb, pgn, fpn, caller->pid, 
                      dirty, referenced);
            printf("  Inserted into TLB: VPN %lu -> FPN %u\n", pgn, fpn);
        }
    }
    
    /* Calculate physical address and read */
    addr_t phyaddr = (fpn * PAGING64_PAGESZ) + off;
    struct sc_regs regs;
    regs.a1 = SYSMEM_IO_READ;
    regs.a2 = phyaddr;
    regs.a3 = 0;
    
    if (syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
        return -1;
    }
    
    *data = (BYTE)regs.a3;
    return 0;
}

/*pg_setval - write value to given offset */
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
    addr_t pgn = PAGING64_PGN(addr);
    addr_t off = PAGING64_OFFST(addr);
    int fpn;
    
    printf("WRITE: pid: %d, addr: %ld, pgn: %ld, off: %ld\n", 
           caller->pid, addr, pgn, off);
    
    /* 1. TRY TLB FIRST */
    addr_t tlb_fpn;
    if (caller->krnl->tlb && 
        tlb_lookup(caller->krnl->tlb, pgn, caller->pid, &tlb_fpn)) {
        /* TLB HIT */
        printf("  TLB HIT: VPN %lu -> FPN %lu\n", pgn, tlb_fpn);
        fpn = tlb_fpn;
        
        /* Update reference and dirty bits in PTE */
        uint32_t pte = pte_get_entry(caller, pgn);
        SETBIT(pte, PAGING_PTE_REFERENCED_MASK);
        SETBIT(pte, PAGING_PTE_DIRTY_MASK);
        pte_set_entry(caller, pgn, pte);
        
        /* Update bits in TLB */
        tlb_set_referenced(caller->krnl->tlb, pgn, caller->pid);
        tlb_set_dirty(caller->krnl->tlb, pgn, caller->pid);
    } else {
        /* TLB MISS - go through normal page lookup */
        printf("  TLB MISS for VPN %lu\n", pgn);
        
        if (pg_getpage(mm, pgn, &fpn, caller) != 0)
            return -1;
        
        /* Update reference and dirty bits in PTE */
        uint32_t pte = pte_get_entry(caller, pgn);
        SETBIT(pte, PAGING_PTE_REFERENCED_MASK);
        SETBIT(pte, PAGING_PTE_DIRTY_MASK);
        pte_set_entry(caller, pgn, pte);
        
        /* INSERT INTO TLB with dirty=1 (write operation) */
        if (caller->krnl->tlb) {
            tlb_insert(caller->krnl->tlb, pgn, fpn, caller->pid, 1, 1);
            printf("  Inserted into TLB: VPN %lu -> FPN %u (dirty=1)\n", pgn, fpn);
        }
    }
    
    /* Calculate physical address and write */
    addr_t phyaddr = (fpn * PAGING64_PAGESZ) + off;
    struct sc_regs regs;
    regs.a1 = SYSMEM_IO_WRITE;
    regs.a2 = phyaddr;
    regs.a3 = value;
    
    if (syscall(caller->krnl, caller->pid, 17, &regs) != 0)
        return -1;
    
    return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 * 
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  printf("READ: pid: %d\n", caller->pid);
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 * 
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  printf("WRITE: pid: %d\n", caller->pid);
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);

  if (val == -1)
  {
    return -1;
  }
printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  MEMPHY_dump(proc->krnl->mram);
#endif

  return val;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;

  /* 1. Giải phóng các khung trang vật lý (RAM và SWAP) */
  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = pte_get_entry(caller, pagenum);

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if (pte != 0) // Chỉ giải phóng nếu PTE có dữ liệu
    {
      fpn = PAGING_SWP(pte);
      if (fpn != 0)
        MEMPHY_put_freefp(caller->krnl->mswp[PAGING_PTE_GET_SWPTYP(pte)], fpn);
    }
  }

  /* 2. Dọn dẹp danh sách FIFO toàn cục */
  printf("Cleaning up FIFO nodes for PID=%d\n", caller->pid);
  struct pgn_t *curr = caller->krnl->mm->fifo_pgn;
  struct pgn_t *prev = NULL;

  while (curr != NULL)
  {
    if (curr->owner == caller)
    {
      struct pgn_t *tmp = curr;
      if (prev == NULL)
      {
        caller->krnl->mm->fifo_pgn = curr->pg_next;
        curr = caller->krnl->mm->fifo_pgn;
      }
      else
      {
        prev->pg_next = curr->pg_next;
        curr = curr->pg_next;
      }
      free(tmp);
    }
    else
    {
      prev = curr;
      curr = curr->pg_next;
    }
  }

  return 0;
}

/*find_victim_page - find victim page using CLOCK (Second Chance) algorithm
 *@mm: memory region
 *@retpgn: return page number
 *@ret_owner: return page owner
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn, struct pcb_t **ret_owner)
{
    struct pgn_t *current;
    int found = 0;
    
    if (mm->clock_hand == NULL) {
        mm->clock_hand = mm->fifo_pgn;
    }
    
    if (mm->clock_hand == NULL) {
        printf("ERROR: No pages in clock list\n");
        return -1;
    }
    
    current = mm->clock_hand;
    struct pgn_t *start = current;
    
    printf("\n=== CLOCK Algorithm Searching (List length: ");
    // Tính độ dài danh sách
    int list_len = 0;
    struct pgn_t *temp = mm->fifo_pgn;
    while (temp != NULL) {
        list_len++;
        temp = temp->pg_next;
    }
    printf("%d) ===\n", list_len);
    
    do {
        uint32_t pte = pte_get_entry(current->owner, current->pgn);
        int present = PAGING_PTE_GET_PRESENT(pte);
        int referenced = PAGING_PTE_GET_REFERENCED(pte);
        
        printf("Checking pgn=%lu (PID=%d): present=%d, referenced=%d\n",
               current->pgn, current->owner->pid, present, referenced);
        
        if (!present) {
            printf("  -> Page not in RAM, removing from list\n");
            // Xóa node này khỏi danh sách
            struct pgn_t *prev = NULL;
            struct pgn_t *iter = mm->fifo_pgn;
            while (iter != NULL && iter != current) {
                prev = iter;
                iter = iter->pg_next;
            }
            if (prev == NULL) {
                mm->fifo_pgn = current->pg_next;
            } else {
                prev->pg_next = current->pg_next;
            }
            struct pgn_t *next = current->pg_next;
            free(current);
            current = (next != NULL) ? next : mm->fifo_pgn;
            continue;
        }
        
        if (referenced == 0) {
            // Found victim
            *retpgn = current->pgn;
            *ret_owner = current->owner;
            found = 1;
            printf("  -> Selected as victim (ref=0)\n");
            
            // Xóa victim khỏi danh sách
            struct pgn_t *prev = NULL;
            struct pgn_t *iter = mm->fifo_pgn;
            while (iter != NULL && iter != current) {
                prev = iter;
                iter = iter->pg_next;
            }
            
            if (prev == NULL) {
                mm->fifo_pgn = current->pg_next;
                mm->clock_hand = (current->pg_next != NULL) ? current->pg_next : mm->fifo_pgn;
            } else {
                prev->pg_next = current->pg_next;
                mm->clock_hand = (current->pg_next != NULL) ? current->pg_next : mm->fifo_pgn;
            }
            
            free(current);
            break;
        } else {
            printf("  -> Giving second chance, clearing reference bit\n");
            CLRBIT(pte, PAGING_PTE_REFERENCED_MASK);
            pte_set_entry(current->owner, current->pgn, pte);
        }
        
        current = current->pg_next;
        if (current == NULL) {
            current = mm->fifo_pgn;
        }

        mm->clock_hand = current;
        
    } while (current != start && !found);
    
    if (!found && mm->fifo_pgn != NULL) {
        printf("All pages had ref=1, taking first page as victim\n");
        current = mm->fifo_pgn;
        *retpgn = current->pgn;
        *ret_owner = current->owner;
        
        mm->fifo_pgn = current->pg_next;
        mm->clock_hand = (current->pg_next != NULL) ? current->pg_next : mm->fifo_pgn;
        free(current);
        found = 1;
    }
    
    if (found) {
        printf("Selected victim: pgn=%lu (PID=%d)\n", *retpgn, (*ret_owner)->pid);
    }
    
    return found ? 0 : -1;
}

/*get_free_vmrg_area - get a free vm region using BEST FIT algorithm
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *@newrg: output region
 * 
 * BEST FIT: Find the smallest free region that is large enough to hold 'size'
 * This reduces fragmentation compared to First Fit
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, addr_t size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  
  if (cur_vma == NULL) {
    printf("ERROR: Cannot find vma %d\n", vmaid);
    return -1;
  }
  
  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  
  if (rgit == NULL) {
    printf("No free regions available in vma %d\n", vmaid);
    return -1;
  }

  /* Initialize newrg */
  newrg->rg_start = -1;
  newrg->rg_end = -1;
  newrg->rg_next = NULL;

  /* BEST FIT: Find the smallest region that fits 'size' */
  struct vm_rg_struct *best_fit = NULL;
  struct vm_rg_struct *best_fit_prev = NULL;
  struct vm_rg_struct *current = rgit;
  struct vm_rg_struct *prev = NULL;
  addr_t best_fit_size = (addr_t)-1; // Initialize to max value

  printf("BEST FIT search for size=%lu in vma %d:\n", size, vmaid);
  
  while (current != NULL) {
    addr_t region_size = current->rg_end - current->rg_start;
    printf("  Checking region [%lu-%lu] (size=%lu)\n", 
           current->rg_start, current->rg_end, region_size);
    
    if (current->rg_start + size <= current->rg_end) {
      /* This region can fit the request */
      if (region_size < best_fit_size) {
        /* Found a better (smaller) fit */
        best_fit = current;
        best_fit_prev = prev;
        best_fit_size = region_size;
        printf("    -> New best fit (size=%lu)\n", region_size);
      }
    }
    
    prev = current;
    current = current->rg_next;
  }

  /* If no suitable region found */
  if (best_fit == NULL) {
    printf("BEST FIT: No region found that can fit size=%lu\n", size);
    return -1;
  }

  printf("BEST FIT selected: [%lu-%lu] (size=%lu)\n",
         best_fit->rg_start, best_fit->rg_end, best_fit_size);

  /* Allocate from the best fit region */
  newrg->rg_start = best_fit->rg_start;
  newrg->rg_end = best_fit->rg_start + size;

  /* Update the free region list */
  if (best_fit->rg_start + size < best_fit->rg_end) {
    /* Region not fully used - shrink it */
    best_fit->rg_start = best_fit->rg_start + size;
    printf("  Shrinking region to [%lu-%lu]\n", 
           best_fit->rg_start, best_fit->rg_end);
  } else {
    /* Region fully used - remove it from free list */
    printf("  Region fully used, removing from free list\n");
    
    if (best_fit_prev == NULL) {
      /* best_fit is the head of the list */
      cur_vma->vm_freerg_list = best_fit->rg_next;
    } else {
      best_fit_prev->rg_next = best_fit->rg_next;
    }
    
    /* Free the node if it was dynamically allocated */
    /* Note: Check if this node was allocated with malloc in __free() */
    if (best_fit != &caller->mm->symrgtbl[0]) { // Not from symbol table
      free(best_fit);
    }
  }

  printf("BEST FIT allocated: [%lu-%lu] (size=%lu)\n", 
         newrg->rg_start, newrg->rg_end, size);
  
  return 0;
}