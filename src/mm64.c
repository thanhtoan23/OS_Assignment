/**
 * vmap_pgd_memset - Map a range of virtual pages into page table structure without physical allocation
 * 
 * @caller: Process control block of the calling process
 * @addr:   Starting virtual address (must be page-aligned)
 * @pgnum:  Number of pages to map
 * 
 * Return: 0 on success, -1 on error
 * 
 * Description:
 * This function emulates page directory/page table creation for a range of virtual pages
 * without actually allocating physical frames. It's used for:
 * 1. Testing sparse virtual memory allocation in 64-bit address space
 * 2. Pre-populating page table structures for large memory regions
 * 3. Supporting lazy allocation mechanisms
 * 
 * The function ensures that all necessary page table levels (PGD, P4D, PUD, PMD, PT)
 * exist for the specified virtual address range. If they don't exist, they are created.
 * No physical frames are allocated - PTE entries remain with PRESENT=0.
 * 
 * [Cite: 749, 750 in the assignment document]
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
    /* Validate input parameters */
    if (caller == NULL) {
        printf("ERROR vmap_pgd_memset: caller is NULL\n");
        return -1;
    }
    
    if (caller->mm == NULL) {
        printf("ERROR vmap_pgd_memset: caller->mm is NULL (PID=%d)\n", caller->pid);
        return -1;
    }
    
    if (pgnum <= 0) {
        printf("ERROR vmap_pgd_memset: invalid pgnum=%d (must be >0)\n", pgnum);
        return -1;
    }
    
    /* Check address alignment (must be page-aligned) */
    if (addr % PAGING64_PAGESZ != 0) {
        printf("WARNING vmap_pgd_memset: address 0x%lx not page-aligned, aligning...\n", addr);
        addr = addr & ~(PAGING64_PAGESZ - 1);  /* Align down to page boundary */
    }
    
    /* Calculate starting page number */
    addr_t pgn_start = PAGING64_PGN(addr);
    
    printf(">>> vmap_pgd_memset: PID=%d, start_addr=0x%lx, start_pgn=%lu, num_pages=%d\n",
           caller->pid, addr, pgn_start, pgnum);
    
    /* For each page in the range, ensure page table structure exists */
    for (int i = 0; i < pgnum; i++) {
        addr_t current_pgn = pgn_start + i;
        
        /* Lock to protect concurrent access to page tables */
        pthread_mutex_lock(&mm_lock);
        
        /* Get PTE pointer, creating missing page table levels if needed (alloc=1) */
        addr_t *pte_ptr = __get_pte_ptr(caller->mm, current_pgn, 1);
        
        if (pte_ptr == NULL) {
            /* This should not happen if __get_pte_ptr succeeds with alloc=1 */
            printf("ERROR vmap_pgd_memset: Failed to get/create PTE for pgn=%lu\n", current_pgn);
            pthread_mutex_unlock(&mm_lock);
            return -1;
        }
        
        /* THAY ĐỔI QUAN TRỌNG: Set PTE entry thành 0xFFFFFFFF (all bits set) */
        *pte_ptr = 0xFFFFFFFF;  // Thay vì để giá trị 0
        
        printf("  Mapped pgn=%lu, set PTE to 0xFFFFFFFF at address %p\n", 
               current_pgn, pte_ptr);
        
        pthread_mutex_unlock(&mm_lock);
    }
    
    /* Track statistics for debugging/optimization */
#ifdef VMAP_STATISTICS
    caller->mm->vmap_count += pgnum;
    printf("Statistics: PID=%d total vmap pages=%lu\n", 
           caller->pid, caller->mm->vmap_count);
#endif
    
    printf("<<< vmap_pgd_memset: Successfully mapped %d pages (PID=%d)\n", 
           pgnum, caller->pid);
    
    /* THAY ĐỔI QUAN TRỌNG: In ra page table sau khi mapping */
    printf("\n=== PAGE TABLE DUMP AFTER VMAP_PGD_MEMSET ===\n");
    print_pgtbl(caller, addr, addr + pgnum * PAGING64_PAGESZ);
    printf("=== END PAGE TABLE DUMP ===\n\n");
    
    return 0;
}




/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */
/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Synchronization: Mutex for protecting Physical Memory access and Page Table expansion */
static pthread_mutex_t mm_lock = PTHREAD_MUTEX_INITIALIZER;

#if defined(MM64)

/*
 * Helper: Allocate a new page table level (array of addr_t)  
 */
static addr_t* alloc_table_level() {
    addr_t* table = (addr_t*)malloc(sizeof(addr_t) * 512); // 9 bits = 512 entries
    if (table) {
        memset(table, 0, sizeof(addr_t) * 512);
    }
    return table;
}

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      // if (fpn == 0) {
      //   printf("Invalid setting\n");
      //   return -1;  // Invalid setting
      // }
      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      
      // Set dirty bit theo tham số
      if (drt) {
        SETBIT(*pte, PAGING_PTE_DIRTY_MASK);
      } else {
        CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      }
      
      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK); // Trang trong swap không có dirty bit
      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

/*
 * get_pd_from_address - Parse address to 5 page directory level
 *  
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  /* Extract page directories using masks defined in mm64.h/os-mm.h */
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt  = (addr & PAGING64_ADDR_PT_MASK)  >> PAGING64_ADDR_PT_LOBIT;

  return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  /* Shift the address to get page num and perform the mapping*/
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_LOBIT, /* Correct shift based on OFFSET len */
                         pgd, p4d, pud, pmd, pt);
}

/*
 * Helper: Traverse 5-level page table to get the pointer to the PTE.
 * If alloc=1, it creates missing levels.  
 * If alloc=0, it returns NULL if path is broken.
 * Warning: Must be called within a critical section if alloc=1.
 */
static addr_t *__get_pte_ptr(struct mm_struct *mm, addr_t pgn, int alloc) {
    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    
    // 1. Calculate indices
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
    
    // 2. Traverse PGD (Level 5) - cấp phát nếu cần
    if (mm->pgd == NULL) {
        if (!alloc) return NULL;
        mm->pgd = alloc_table_level();
    }
    
    addr_t *p4d_table = (addr_t *)mm->pgd[pgd_idx];
    
    if (p4d_table == NULL) {
        if (!alloc) return NULL;
        p4d_table = alloc_table_level();
        mm->pgd[pgd_idx] = (addr_t)p4d_table;
    }

    // 3. Traverse P4D (Level 4)
    addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
    if (pud_table == NULL) {
        if (!alloc) return NULL;
        pud_table = alloc_table_level();
        p4d_table[p4d_idx] = (addr_t)pud_table;
    }

    // 4. Traverse PUD (Level 3)
    addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
    if (pmd_table == NULL) {
        if (!alloc) return NULL;
        pmd_table = alloc_table_level();
        pud_table[pud_idx] = (addr_t)pmd_table;
    }

    // 5. Traverse PMD (Level 2)
    addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
    if (pt_table == NULL) {
        if (!alloc) return NULL;
        pt_table = alloc_table_level();
        pmd_table[pmd_idx] = (addr_t)pt_table;
    }

    // 6. Return pointer to PTE in Level 1 (PT)
    return &pt_table[pt_idx];
}

/* pte_set_swap - Set PTE entry for swapped page */
int pte_set_swap(struct pcb_t *owner, addr_t pgn, int swptyp, addr_t swpoff) {
    pthread_mutex_lock(&mm_lock);
    
    addr_t *pte = __get_pte_ptr(owner->mm, pgn, 1);
    if (pte == NULL) {
        pthread_mutex_unlock(&mm_lock);
        return -1;
    }
    
    printf(">>> pte_set_swap: PID=%d, pgn=%lu -> SWAP(fpn=%lu)\n",
            owner->pid, pgn, swpoff);
    
    /* Invalidate TLB entry for this page */
    if (owner->krnl->tlb) {
        tlb_invalidate_entry(owner->krnl->tlb, pgn, owner->pid);
    }
    
    SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
    SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
    CLRBIT(*pte, PAGING_PTE_REFERENCED_MASK);
    CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
    SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
    SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    
    printf("New PTE value: 0x%08x (dirty=0)\n", (uint32_t)*pte);
    
    pthread_mutex_unlock(&mm_lock);
    return 0;
}

/* pte_set_fpn - Set PTE entry for on-line page */
int pte_set_fpn(struct pcb_t *owner, addr_t pgn, addr_t fpn, int is_dirty) {
    pthread_mutex_lock(&mm_lock);
    addr_t *pte = __get_pte_ptr(owner->mm, pgn, 1);
    if (pte == NULL) {
        pthread_mutex_unlock(&mm_lock);
        return -1;
    }
    
    printf(">>> pte_set_fpn: PID=%d, pgn=%lu -> RAM(fpn=%lu), dirty=%d\n",
            owner->pid, pgn, fpn, is_dirty);
    
    /* Don't invalidate TLB here - we want to keep the entry if it exists */
    // Đây là QUAN ĐIỂM SAI - cần invalidate để đồng bộ
    
    // FIX: Invalidate TLB khi PTE thay đổi
    if (owner->krnl->tlb) {
        tlb_invalidate_entry(owner->krnl->tlb, pgn, owner->pid);
    }
    
    SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
    CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
    SETBIT(*pte, PAGING_PTE_REFERENCED_MASK);
    SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
    if (is_dirty) {
        SETBIT(*pte, PAGING_PTE_DIRTY_MASK);
    } else {
        CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
    }
    
    printf("New PTE value: 0x%08x (dirty=%d)\n", (uint32_t)*pte, is_dirty);
    
    pthread_mutex_unlock(&mm_lock);
    return 0;
}

/* Get PTE page table entry */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn) {
  pthread_mutex_lock(&mm_lock);
  addr_t *pte_ptr = __get_pte_ptr(caller->mm, pgn, 0); // Do not alloc
  if (pte_ptr == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1; // Page not present / invalid
  }
  pthread_mutex_unlock(&mm_lock);
  return (uint32_t)(*pte_ptr);
}

/* Set PTE page table entry */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val) {
    // Note: This overrides the entry directly.
    
    pthread_mutex_lock(&mm_lock);
    addr_t *pte_ptr = __get_pte_ptr(caller->mm, pgn, 1);
    if (pte_ptr == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1; // Page not present / invalid
    }
    else {
        *pte_ptr = (addr_t)pte_val;
    }
    pthread_mutex_unlock(&mm_lock);
    return 0;
}


/**
 * vmap_pgd_memset - Map a range of virtual pages into page table structure without physical allocation
 * 
 * @caller: Process control block of the calling process
 * @addr:   Starting virtual address (must be page-aligned)
 * @pgnum:  Number of pages to map
 * 
 * Return: 0 on success, -1 on error
 * 
 * Description:
 * This function emulates page directory/page table creation for a range of virtual pages
 * without actually allocating physical frames. It's used for:
 * 1. Testing sparse virtual memory allocation in 64-bit address space
 * 2. Pre-populating page table structures for large memory regions
 * 3. Supporting lazy allocation mechanisms
 * 
 * The function ensures that all necessary page table levels (PGD, P4D, PUD, PMD, PT)
 * exist for the specified virtual address range. If they don't exist, they are created.
 * No physical frames are allocated - PTE entries remain with PRESENT=0.
 * 
 * [Cite: 749, 750 in the assignment document]
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
  if (caller == NULL) {
        printf("ERROR vmap_pgd_memset: caller is NULL\n");
        return -1;
    }
    
    if (caller->mm == NULL) {
        printf("ERROR vmap_pgd_memset: caller->mm is NULL (PID=%d)\n", caller->pid);
        return -1;
    }
    
    if (pgnum <= 0) {
        printf("ERROR vmap_pgd_memset: invalid pgnum=%d (must be >0)\n", pgnum);
        return -1;
    }
    
    /* Check address alignment (must be page-aligned) */
    if (addr % PAGING64_PAGESZ != 0) {
        printf("WARNING vmap_pgd_memset: address 0x%lx not page-aligned, aligning...\n", addr);
        addr = addr & ~(PAGING64_PAGESZ - 1);  /* Align down to page boundary */
    }
    
    /* Calculate starting page number */
    addr_t pgn_start = PAGING64_PGN(addr);
    
    printf(">>> vmap_pgd_memset: PID=%d, start_addr=0x%lx, start_pgn=%lu, num_pages=%d\n",
           caller->pid, addr, pgn_start, pgnum);
    
    /* For each page in the range, ensure page table structure exists */
    for (int i = 0; i < pgnum; i++) {
        addr_t current_pgn = pgn_start + i;
        
        /* Lock to protect concurrent access to page tables */
        pthread_mutex_lock(&mm_lock);
        
        /* Get PTE pointer, creating missing page table levels if needed (alloc=1) */
        addr_t *pte_ptr = __get_pte_ptr(caller->mm, current_pgn, 1);
        
        if (pte_ptr == NULL) {
            /* This should not happen if __get_pte_ptr succeeds with alloc=1 */
            printf("ERROR vmap_pgd_memset: Failed to get/create PTE for pgn=%lu\n", current_pgn);
            pthread_mutex_unlock(&mm_lock);
            return -1;
        }
        
        *pte_ptr = 0xFFFFFFFF;  
        
        printf("  Mapped pgn=%lu, set PTE to 0xFFFFFFFF at address %p\n", 
               current_pgn, pte_ptr);
        
        pthread_mutex_unlock(&mm_lock);
    }
    
    /* Track statistics for debugging/optimization */
#ifdef VMAP_STATISTICS
    caller->mm->vmap_count += pgnum;
    printf("Statistics: PID=%d total vmap pages=%lu\n", 
           caller->pid, caller->mm->vmap_count);
#endif
    
    printf("<<< vmap_pgd_memset: Successfully mapped %d pages (PID=%d)\n", 
           pgnum, caller->pid);
    
    printf("\n=== PAGE TABLE DUMP AFTER VMAP_PGD_MEMSET ===\n");
    print_pgtbl(caller, addr, addr + pgnum * PAGING64_PAGESZ);
    printf("=== END PAGE TABLE DUMP ===\n\n");
    
    return 0;
}



/*
 * vmap_page_range - map a range of page at aligned address
 * [cite: 588]
 */
addr_t vmap_page_range(struct pcb_t *caller,
                    addr_t addr,
                    int pgnum,
                    struct framephy_struct *frames,
                    struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = PAGING64_PGN(addr);

  printf("Page num %lu -> Address: %lu \n",pgn,addr);

  /* Update the mapped region information */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;

  /* Map range of frames to address space */
  for (pgit = 0; pgit < pgnum; pgit++)
  {
      if (fpit == NULL) break; // Should not happen if alloc matches req

      // Trang mới cấp phát -> dirty = 1
      pte_set_fpn(caller, pgn + pgit, fpit->fpn, 1);
      
      // Tracking for FIFO replacement (enlisting)
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit, caller);

      fpit = fpit->fp_next;
  }

  return 0;
}

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  printf("ALLOC PAGE RANGE, PID: %d\n", caller->pid);
  addr_t ret_fpn;
  struct framephy_struct *newfp_str;
  struct framephy_struct *last_fp = NULL;
  
  *frm_lst = NULL;

  for (addr_t pgit = 0; pgit < req_pgnum; pgit++) {
    newfp_str = malloc(sizeof(struct framephy_struct));
    if (!newfp_str) {
      /* Rollback: free any already allocated frames */
      while (*frm_lst) {
        struct framephy_struct *temp = *frm_lst;
        *frm_lst = (*frm_lst)->fp_next;
        free(temp);
      }
      return -1;
    }

    /* Lock access to global Physical Memory Manager (MEMPHY) */
    int res = MEMPHY_get_freefp(caller->krnl->mram, (addr_t *)&ret_fpn);

    if (res == 0) {
      /* Successfully got a free frame from RAM */
      newfp_str->fpn = ret_fpn;
      newfp_str->fp_next = NULL;

      if (*frm_lst == NULL) {
        *frm_lst = newfp_str;
      } else {
        last_fp->fp_next = newfp_str;
      }
      last_fp = newfp_str;
    }
    else {
      /* RAM is full, need to swap out a page */
      printf("RAM is full! Need to find VICTIM for SWAP OUT in alloc_pages_range\n");
      
      addr_t vicpgn, vicfpn, swpfpn;
      uint32_t vicpte;
      struct sc_regs regs;
      struct pcb_t *vic_owner;

      /* Find a victim page to swap out */
      if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) == -1) {
        printf("ERROR: Cannot find victim page in alloc_pages_range\n");

        free(newfp_str);

        if (pgit > 0) {
            printf("  Rolling back %lu allocated frames\n", pgit);
            while (*frm_lst) {
              struct framephy_struct *temp = *frm_lst;
              MEMPHY_put_freefp(caller->krnl->mram, temp->fpn);
              *frm_lst = (*frm_lst)->fp_next;
              free(temp);
            }
          }
        
        /* Rollback: free any already allocated frames */
        while (*frm_lst) {
          struct framephy_struct *temp = *frm_lst;
          *frm_lst = (*frm_lst)->fp_next;
          free(temp);
        }
        
        return -1;
      }

      vicpte = pte_get_entry(vic_owner, vicpgn);
      vicfpn = PAGING_FPN(vicpte);
      int vic_is_dirty = PAGING_PTE_GET_DIRTY(vicpte);

      printf("Selected VICTIM: PID=%d, pgn=%lu, fpn=%lu, pte=0x%08x, dirty=%d\n",
            vic_owner->pid, vicpgn, vicfpn, vicpte, vic_is_dirty);

      /* CHỈ SWAP OUT NẾU DIRTY */
      if (vic_is_dirty) {
        /* ROUND ROBIN SWAP SELECTION - với xử lý swap đầy */
        int found_swp_id = -1;
        for (int i = 0; i < PAGING_MAX_MMSWP; i++) {
            int swp_idx = (caller->krnl->active_mswp_id + i) % PAGING_MAX_MMSWP;
            
            /* Kiểm tra thiết bị swap có tồn tại và còn slot trống không */
            if (caller->krnl->mswp[swp_idx] != NULL && 
                MEMPHY_get_freefp(caller->krnl->mswp[swp_idx], &swpfpn) == 0) {
                found_swp_id = swp_idx;
                /* Cập nhật con trỏ Round Robin cho hệ thống */
                caller->krnl->active_mswp_id = (swp_idx + 1) % PAGING_MAX_MMSWP;
                break;
            }
        }

        if (found_swp_id == -1) {
          /* TẤT CẢ SWAP ĐỀU ĐẦY - XỬ LÝ DEADLOCK */
          printf("ALL SWAP DEVICES ARE FULL!\n");
          
          /* 1. Giải phóng newfp_str đã cấp phát */
          free(newfp_str);
          
          /* 2. Trả lại frame RAM nếu đã lấy được */
          if (pgit > 0) {
            printf("  Rolling back %lu allocated frames\n", pgit);
            while (*frm_lst) {
              struct framephy_struct *temp = *frm_lst;
              MEMPHY_put_freefp(caller->krnl->mram, temp->fpn);
              *frm_lst = (*frm_lst)->fp_next;
              free(temp);
            }
          }

          /* Rollback: free any already allocated frames */
          while (*frm_lst) {
            struct framephy_struct *temp = *frm_lst;
            *frm_lst = (*frm_lst)->fp_next;
            free(temp);
          }
          
          printf("ERROR: Cannot allocate pages - swap full and insufficient clean pages\n");
          return -1; 
        }
        
        printf("Free SWAP frame obtained at SWAP %d: swpfpn=%lu\n", found_swp_id, swpfpn);

        /* Swap Out: RAM -> SWAP được chọn */
        printf("SWAP OUT: RAM(%lu) -> SWAP %d(%lu) because dirty=1\n", vicfpn, found_swp_id, swpfpn);
        regs.a1 = SYSMEM_SWP_OP;
        regs.a2 = vicfpn;
        regs.a3 = swpfpn;
        regs.a4 = 0;             /* Direction: 0 for Out */
        regs.a5 = found_swp_id;  /* Swap device ID */
        syscall(caller->krnl, caller->pid, 17, &regs);

        /* Update victim PTE to point to đúng thiết bị SWAP */
        pte_set_swap(vic_owner, vicpgn, found_swp_id, swpfpn);
        
        /* TLB COHERENCE: Invalidate victim TLB entry */
        if (vic_owner->krnl->tlb) {
            tlb_invalidate_entry(vic_owner->krnl->tlb, vicpgn, vic_owner->pid);
            printf("  Invalidated TLB entry for swapped out victim in alloc_pages_range: VPN %lu (PID=%d)\n",
                   vicpgn, vic_owner->pid);
        }
        
        printf("Updated VICTIM PTE (PID=%d, pgn=%lu) to point to SWAP %d(%lu)\n",
              vic_owner->pid, vicpgn, found_swp_id, swpfpn);
      } else {
        printf("VICTIM is CLEAN (dirty=0), no need to write to SWAP\n");
        /* Chỉ cần invalidate PTE của victim */
        pte_set_entry(vic_owner, vicpgn, 0);
        
        /* TLB COHERENCE: Invalidate clean victim TLB entry */
        if (vic_owner->krnl->tlb) {
            tlb_invalidate_entry(vic_owner->krnl->tlb, vicpgn, vic_owner->pid);
            printf("  Invalidated TLB entry for clean victim in alloc_pages_range: VPN %lu (PID=%d)\n",
                   vicpgn, vic_owner->pid);
        }
        
        printf("Invalidated VICTIM PTE (PID=%d, pgn=%lu)\n",
              vic_owner->pid, vicpgn);
      }

      /* Now we can use the victim's frame for the new page */
      ret_fpn = vicfpn;
      printf("Victim frame %lu now available for new page allocation\n", ret_fpn);

      newfp_str->fpn = ret_fpn;
      newfp_str->fp_next = NULL;

      if (*frm_lst == NULL) {
        *frm_lst = newfp_str;
      } else {
        last_fp->fp_next = newfp_str;
      }
      last_fp = newfp_str;
    }
  }

  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  int pgnum = incpgnum;

  ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  if (ret_alloc == -3000)
  {
    printf("Out of memory\n");
    return -1; // Out of Memory
  }

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn, struct pcb_t *caller, int active_mswp_id)
{
  printf("=== SWAP OPERATION ===\n");
  
  /* XÁC ĐỊNH ĐÚNG LOẠI SWAP */
  int is_swap_out = (mpsrc == caller->krnl->mram && mpdst == caller->krnl->mswp[active_mswp_id]);
  int is_swap_in = (mpsrc == caller->krnl->mswp[active_mswp_id] && mpdst == caller->krnl->mram);
  
  if (is_swap_out) {
    printf("SWAP OUT: RAM(fpn=%lu) -> SWAP[%u](fpn=%lu)\n", srcfpn, active_mswp_id, dstfpn);
  } else if (is_swap_in) {
    printf("SWAP IN: SWAP[%u](fpn=%lu) -> RAM(fpn=%lu)\n", active_mswp_id, srcfpn, dstfpn);
  } else {
    printf("UNKNOWN SWAP DIRECTION: src=%s, dst=%s\n",
           (mpsrc == caller->krnl->mram) ? "RAM" : "SWAP",
           (mpdst == caller->krnl->mram) ? "RAM" : "SWAP");
  }
  
  addr_t cellidx;
  addr_t addrsrc, addrdst;

  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  printf("Swap completed successfully\n");
  printf("=== End SWAP ===\n\n");
  return 0;
}

/*
 * Initialize a empty Memory Management instance
 * [cite: 628, 631]
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* PGD, P4D, PUD, PMD, PT will be allocated dynamically on demand */
  mm->pgd = NULL;
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt  = NULL;

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;

  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  vma0->vm_freerg_list = NULL;
  vma0->vm_next = NULL;
  vma0->vm_mm = mm;

  mm->mmap = vma0;
  mm->fifo_pgn = NULL;
  mm->clock_hand = NULL;
  
  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn, struct pcb_t *caller)
{
    /* Kiểm tra đầu vào hợp lệ */
    if (caller == NULL || caller->pid <= 0) {
        printf("ERROR: Invalid caller (PID=%d) in enlist_pgn_node\n", 
               caller ? caller->pid : -1);
        return -1;
    }
    
    if (pgn >= PAGING_MAX_PGN) {
        printf("ERROR: Invalid page number %lu (max=%d)\n", pgn, PAGING_MAX_PGN);
        return -1;
    }
    
    /* KIỂM TRA TRÙNG LẶP: Đảm bảo page này chưa có trong danh sách */
    struct pgn_t *existing = *plist;
    while (existing != NULL) {
        if (existing->owner == caller && existing->pgn == pgn) {
            printf("WARNING: Page %lu (PID=%d) already exists in FIFO list, skipping\n", 
                   pgn, caller->pid);
            return 0; // Không thêm trùng, nhưng không phải lỗi
        }
        existing = existing->pg_next;
    }
    
    /* Kiểm tra PTE để đảm bảo page thực sự tồn tại và hợp lệ */
    uint32_t pte = pte_get_entry(caller, pgn);
    if (pte == (uint32_t)-1) {
        printf("WARNING: Cannot get PTE for pgn=%lu (PID=%d), page may not exist\n", 
               pgn, caller->pid);
        return -1;
    }
    
    if (!PAGING_PTE_GET_PRESENT(pte)) {
        printf("WARNING: Page %lu (PID=%d) is not present, not adding to FIFO\n", 
               pgn, caller->pid);
        return 0; // Không thêm page không present
    }
    
    int is_swapped = PAGING_PTE_GET_SWAPPED(pte);
    if (is_swapped) {
        printf("WARNING: Page %lu (PID=%d) is swapped out, not adding to FIFO\n", 
               pgn, caller->pid);
        return 0; // Không thêm page đang ở swap
    }
    
    /* Tạo node mới */
    struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
    if (!pnode) {
        printf("ERROR: malloc failed in enlist_pgn_node\n");
        return -1;
    }
    
    pnode->pgn = pgn;
    pnode->owner = caller;
    pnode->pg_next = NULL;
    
    /* Thêm vào CUỐI danh sách (FIFO đúng nghĩa) */
    if (*plist == NULL) {
        *plist = pnode;
    } else {
        struct pgn_t *last = *plist;
        while (last->pg_next != NULL) {
            last = last->pg_next;
        }
        last->pg_next = pnode;
    }
    
    printf("===== Added to FIFO: pgn=%lu (PID=%d) =====\n", pgn, caller->pid);
    print_list_pgn(*plist);
    
    return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");

  while (fp != NULL)
  {
    printf("fp[%ld]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");

  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");

  while (rg != NULL)
  {
    printf("rg[%ld->%ld]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");

  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");

  while (vma != NULL)
  {
    printf("va[%ld->%ld]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");

  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: \n");
  if (ip == NULL) { 
    printf("NULL list\n"); 
    return -1; 
  }

  struct pgn_t *curr = ip;
  int count = 0;
  while (curr != NULL)
  {
    printf("  va[%ld] (PID=%d)\n", curr->pgn, curr->owner->pid);
    curr = curr->pg_next;
    count++;
  }
  printf("Total: %d pages\n", count);
  printf("\n");

  return 0;
}

/* Helper to recursively print page table entries */
void print_pgtbl_recursive(addr_t *table, int level, addr_t current_prefix) {
    if (table == NULL) return;

    int i;
    int count = 0;
    for (i = 0; i < 512; i++) {
        if (table[i] == 0) continue;

        if (level == 1) { // PT Level, table[i] is PTE 
            printf("  %05lx: [%08x] (FPN: %ld) (PRE: %d) (SWA: %d) (DIR: %d) (REF: %d)\n",
                    (current_prefix << 9) | i,
                    (uint32_t)table[i],
                    PAGING_FPN(table[i]),
                    PAGING_PTE_GET_PRESENT(table[i]),
                    PAGING_PTE_GET_SWAPPED(table[i]),
                    PAGING_PTE_GET_DIRTY(table[i]) ? 1 : 0,
                    PAGING_PTE_GET_REFERENCED(table[i]) ? 1 : 0);
            
            ++count;
        } else {
            // Intermediate levels, table[i] is pointer to next table
            print_pgtbl_recursive((addr_t *)table[i], level - 1, (current_prefix << 9) | i);
        }
    }
    if (level == 1) printf("Count: %d\n", count);
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  printf("Page Table Dump for PID %d:\n", caller->pid);

  if (caller == NULL || caller->mm == NULL || caller->mm->pgd == NULL) {
      printf("Page table not initialized.\n");
      return -1;
  }

  // We will use address start (0 for default) to find index
  addr_t pgn_start = start >> PAGING64_ADDR_PT_SHIFT;
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_pagenum(pgn_start, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  /* Start traversing from PGD */
  addr_t *p4d_table = (addr_t*)caller->mm->pgd[pgd_idx];
  addr_t *pud_table = (addr_t*)p4d_table[p4d_idx];
  addr_t *pmd_table = (addr_t*)pud_table[pud_idx];

  //printf result
  printf("print_pgtbl:\n PDG=%016lx P4G=%016lx PUD=%016lx PMD=%016lx\n",
        (unsigned long)caller->mm->pgd,
        (unsigned long)p4d_table,
        (unsigned long)pud_table,
        (unsigned long)pmd_table);
  
  // Start recursion from PGD (Level 5)
  print_pgtbl_recursive(caller->mm->pgd, 5, 0);

  return 0;
}

#endif  //def MM64