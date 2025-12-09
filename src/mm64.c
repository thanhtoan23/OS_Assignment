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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h> 

/* Synchronization: Mutex for protecting Physical Memory access and Page Table expansion */
static pthread_mutex_t mm_lock = PTHREAD_MUTEX_INITIALIZER;

#if defined(MM64)

/* Helper: Allocate a new page table level (array of addr_t) */
static addr_t* alloc_table_level() {
    addr_t* table = (addr_t*)calloc(512, sizeof(addr_t)); // 9 bits = 512 entries
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
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      
      if (drt)
        SETBIT(*pte, PAGING_PTE_DIRTY_MASK);
      else
        CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
      
      /* Clear swap fields */
      CLRBIT(*pte, PAGING_PTE_SWPTYP_MASK);
      CLRBIT(*pte, PAGING_PTE_SWPOFF_MASK);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
      
      /* Clear FPN field */
      CLRBIT(*pte, PAGING_PTE_FPN_MASK);
    }
  }
  else {
    /* Page not present */
    CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
    CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
    CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
    CLRBIT(*pte, PAGING_PTE_FPN_MASK);
    CLRBIT(*pte, PAGING_PTE_SWPTYP_MASK);
    CLRBIT(*pte, PAGING_PTE_SWPOFF_MASK);
  }

  return 0;
}

/*
 * get_pd_from_address - Parse address to 5 page directory level
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
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_LOBIT,
                         pgd, p4d, pud, pmd, pt);
}

/*
 * Helper: Traverse 5-level page table to get the pointer to the PTE.
 * If alloc=1, it creates missing levels. 
 * If alloc=0, it returns NULL if path is broken.
 */
static addr_t *__get_pte_ptr(struct mm_struct *mm, addr_t pgn, int alloc) {
    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    
    // 1. Calculate indices
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    // 2. Traverse PGD (Level 5)
    if (mm->pgd == NULL) {
        if (!alloc) return NULL;
        mm->pgd = alloc_table_level();
        if (mm->pgd == NULL) return NULL;
    }
    
    addr_t *p4d_table = (addr_t *)mm->pgd[pgd_idx];
    if (p4d_table == NULL) {
        if (!alloc) return NULL;
        p4d_table = alloc_table_level();
        if (p4d_table == NULL) return NULL;
        mm->pgd[pgd_idx] = (addr_t)p4d_table;
    }

    // 3. Traverse P4D (Level 4)
    addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
    if (pud_table == NULL) {
        if (!alloc) return NULL;
        pud_table = alloc_table_level();
        if (pud_table == NULL) return NULL;
        p4d_table[p4d_idx] = (addr_t)pud_table;
    }

    // 4. Traverse PUD (Level 3)
    addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
    if (pmd_table == NULL) {
        if (!alloc) return NULL;
        pmd_table = alloc_table_level();
        if (pmd_table == NULL) return NULL;
        pud_table[pud_idx] = (addr_t)pmd_table;
    }

    // 5. Traverse PMD (Level 2)
    addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
    if (pt_table == NULL) {
        if (!alloc) return NULL;
        pt_table = alloc_table_level();
        if (pt_table == NULL) return NULL;
        pmd_table[pmd_idx] = (addr_t)pt_table;
    }

    // 6. Return pointer to PTE in Level 1 (PT)
    return &pt_table[pt_idx];
}

/*
 * pte_set_swap - Set PTE entry for swapped page
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  pthread_mutex_lock(&mm_lock);
  
  addr_t *pte = __get_pte_ptr(caller->krnl->mm, pgn, 1);
  
  if (pte == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1;
  }

  // Initialize PTE as swapped page
  init_pte(pte, 1, 0, 0, 1, swptyp, swpoff);
  
  pthread_mutex_unlock(&mm_lock);
  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  pthread_mutex_lock(&mm_lock);

  addr_t *pte = __get_pte_ptr(caller->krnl->mm, pgn, 1);

  if (pte == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1;
  }

  // Initialize PTE as resident page (present, not swapped)
  init_pte(pte, 1, fpn, 0, 0, 0, 0);
  
  pthread_mutex_unlock(&mm_lock);
  return 0;
}

/*
 * pte_set_dirty - Mark page as dirty
 */
int pte_set_dirty(struct pcb_t *caller, addr_t pgn)
{
  pthread_mutex_lock(&mm_lock);
  
  addr_t *pte = __get_pte_ptr(caller->krnl->mm, pgn, 0);
  
  if (pte == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1;
  }
  
  SETBIT(*pte, PAGING_PTE_DIRTY_MASK);
  
  pthread_mutex_unlock(&mm_lock);
  return 0;
}

/* Get PTE page table entry */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  pthread_mutex_lock(&mm_lock);
  addr_t *pte_ptr = __get_pte_ptr(caller->krnl->mm, pgn, 0);
  pthread_mutex_unlock(&mm_lock);

  if (pte_ptr == NULL) {
      return 0; // Page not present / invalid
  }
  
  return (uint32_t)(*pte_ptr);
}

/* Set PTE page table entry */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
    pthread_mutex_lock(&mm_lock);
    addr_t *pte_ptr = __get_pte_ptr(caller->krnl->mm, pgn, 1);
    if (pte_ptr) {
        *pte_ptr = (addr_t)pte_val;
    }
    pthread_mutex_unlock(&mm_lock);
    return pte_ptr ? 0 : -1;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
  int i;
  addr_t pgn_start = PAGING_PGN(addr);
  
  for(i = 0; i < pgnum; i++) {
      pthread_mutex_lock(&mm_lock);
      __get_pte_ptr(caller->krnl->mm, pgn_start + i, 1);
      pthread_mutex_unlock(&mm_lock);
  }
  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,
                    addr_t addr,
                    int pgnum,
                    struct framephy_struct *frames,
                    struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = PAGING_PGN(addr);

  /* Update the mapped region information */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING_PAGESZ;

  /* Map range of frames to address space */
  for (pgit = 0; pgit < pgnum; pgit++)
  {
      if (fpit == NULL) break;

      pte_set_fpn(caller, pgn + pgit, fpit->fpn);
      
      // Tracking for FIFO replacement (enlisting)
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit);

      fpit = fpit->fp_next;
  }

  return 0;
}

/*
 * swap_out_page - Swap out a victim page to backing store
 */
static int swap_out_page(struct pcb_t *caller, addr_t vicpgn, addr_t vicfpn)
{
    addr_t swpfpn;
    
    // Get free frame in swap device
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) < 0) {
        return -1; // No space in swap
    }
    
    // Copy page from RAM to swap
    __swap_cp_page(caller->krnl->mram, vicfpn, 
                   caller->krnl->active_mswp, swpfpn);
    
    // Update PTE to mark as swapped
    pte_set_swap(caller, vicpgn, 0, swpfpn);
    
    // Free the RAM frame
    MEMPHY_put_freefp(caller->krnl->mram, vicfpn);
    
    return 0;
}

/*
 * swap_in_page - Swap in a page from backing store to RAM
 */
static int swap_in_page(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
    uint32_t pte = pte_get_entry(caller, pgn);
    
    if (!PAGING_PAGE_SWAPPED(pte)) {
        return -1; // Not a swapped page
    }
    
    addr_t swpfpn = PAGING_PTE_SWPOFF(pte);
    
    // Copy page from swap to RAM
    __swap_cp_page(caller->krnl->active_mswp, swpfpn,
                   caller->krnl->mram, fpn);
    
    // Update PTE to mark as resident
    pte_set_fpn(caller, pgn, fpn);
    
    // Free the swap frame
    MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
    
    return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * Handles page replacement when RAM is full
 */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  int pgit;
  int ret_fpn;
  struct framephy_struct *newfp_str;
  struct framephy_struct *last_fp = NULL;
  
  *frm_lst = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    newfp_str = malloc(sizeof(struct framephy_struct));
    if (newfp_str == NULL) {
        // Cleanup allocated frames on failure
        while (*frm_lst != NULL) {
            struct framephy_struct *temp = *frm_lst;
            *frm_lst = (*frm_lst)->fp_next;
            MEMPHY_put_freefp(caller->krnl->mram, temp->fpn);
            free(temp);
        }
        return -3000;
    }
    
    // 1. Try to get free frame from RAM
    pthread_mutex_lock(&mm_lock);
    int res = MEMPHY_get_freefp(caller->krnl->mram, (addr_t *)&ret_fpn);
    pthread_mutex_unlock(&mm_lock);

    if (res != 0) { 
        // 2. RAM is full -> Perform page replacement
        addr_t vicpgn; // Victim page number
        
        // Find victim page using FIFO algorithm
        if (find_victim_page(caller->krnl->mm, &vicpgn) < 0) {
            free(newfp_str);
            // Cleanup
            while (*frm_lst != NULL) {
                struct framephy_struct *temp = *frm_lst;
                *frm_lst = (*frm_lst)->fp_next;
                MEMPHY_put_freefp(caller->krnl->mram, temp->fpn);
                free(temp);
            }
            return -3000;
        }

        // Get victim's PTE
        uint32_t vicpte = pte_get_entry(caller, vicpgn);
        
        if (PAGING_PAGE_PRESENT(vicpte) && !PAGING_PAGE_SWAPPED(vicpte)) {
            // Victim is in RAM, need to swap it out
            int vicfpn = PAGING_PTE_FPN(vicpte);
            
            if (swap_out_page(caller, vicpgn, vicfpn) < 0) {
                free(newfp_str);
                // Cleanup
                while (*frm_lst != NULL) {
                    struct framephy_struct *temp = *frm_lst;
                    *frm_lst = (*frm_lst)->fp_next;
                    MEMPHY_put_freefp(caller->krnl->mram, temp->fpn);
                    free(temp);
                }
                return -3000;
            }
            
            // Now we can use victim's frame
            ret_fpn = vicfpn;
        } else {
            // Victim is already swapped, just free its swap slot
            if (PAGING_PAGE_SWAPPED(vicpte)) {
                addr_t swpfpn = PAGING_PTE_SWPOFF(vicpte);
                MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
            }
            
            // Get a new free frame (should succeed now)
            pthread_mutex_lock(&mm_lock);
            res = MEMPHY_get_freefp(caller->krnl->mram, (addr_t *)&ret_fpn);
            pthread_mutex_unlock(&mm_lock);
            
            if (res != 0) {
                free(newfp_str);
                // Cleanup
                while (*frm_lst != NULL) {
                    struct framephy_struct *temp = *frm_lst;
                    *frm_lst = (*frm_lst)->fp_next;
                    MEMPHY_put_freefp(caller->krnl->mram, temp->fpn);
                    free(temp);
                }
                return -3000;
            }
        }
        
        // Clear the victim's PTE entry
        pte_set_entry(caller, vicpgn, 0);
    }

    // 3. Assign frame to the list
    newfp_str->fpn = ret_fpn;
    newfp_str->fp_next = NULL;

    if (*frm_lst == NULL) {
        *frm_lst = newfp_str;
    } else {
        last_fp->fp_next = newfp_str;
    }
    last_fp = newfp_str;
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
    return -1; // Out of Memory
  }

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  
  // Free frame list structure (frames themselves are owned by mm)
  while (frm_lst != NULL) {
      struct framephy_struct *temp = frm_lst;
      frm_lst = frm_lst->fp_next;
      free(temp);
  }

  return 0;
}

/* Swap copy content page from source frame to destination frame */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  
  if (mpsrc == NULL || mpdst == NULL)
    return -1;
    
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    if (MEMPHY_read(mpsrc, addrsrc, &data) < 0)
        return -1;
    if (MEMPHY_write(mpdst, addrdst, data) < 0)
        return -1;
  }

  return 0;
}

/*
 * Initialize a empty Memory Management instance
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (vma0 == NULL) return -1;

  /* Init PGD (Level 5) - Root of the page table */
  pthread_mutex_lock(&mm_lock);
  mm->pgd = alloc_table_level();
  pthread_mutex_unlock(&mm_lock);
  
  if (mm->pgd == NULL) {
      free(vma0);
      return -1;
  }

  /* P4D, PUD, PMD, PT will be allocated dynamically on demand */
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt  = NULL;

  /* Initialize FIFO page list */
  mm->fifo_pgn = NULL;

  /* Initialize symbol table */
  memset(mm->symrgtbl, 0, sizeof(struct vm_rg_struct) * PAGING_MAX_SYMTBL_SZ);

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start + PAGING_PAGESZ; // Initial 1 page
  vma0->sbrk = vma0->vm_start;
  
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  if (first_rg == NULL) {
      free(vma0);
      free(mm->pgd);
      return -1;
  }
  
  vma0->vm_freerg_list = first_rg;
  vma0->vm_next = NULL;
  vma0->vm_mm = mm;

  mm->mmap = vma0;
  
  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
  if (rgnode == NULL) return NULL;

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  if (rgnode == NULL) return -1;
  
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
  if (pnode == NULL) return -1;

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

/*
 * remove_pgn_node - Remove a page node from FIFO list
 */
static int remove_pgn_node(struct pgn_t **plist, addr_t pgn)
{
    struct pgn_t *curr = *plist;
    struct pgn_t *prev = NULL;
    
    while (curr != NULL) {
        if (curr->pgn == pgn) {
            if (prev == NULL) {
                *plist = curr->pg_next;
            } else {
                prev->pg_next = curr->pg_next;
            }
            free(curr);
            return 0;
        }
        prev = curr;
        curr = curr->pg_next;
    }
    
    return -1; // Not found
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[%d]\n", fp->fpn);
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
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[%d]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

/* Helper to recursively print page table entries */
void print_pgtbl_recursive(addr_t *table, int level, addr_t current_prefix) {
    if (table == NULL) return;
    
    int i;
    for (i = 0; i < 512; i++) {
        if (table[i] == 0) continue;

        if (level == 1) { // PT Level, table[i] is PTE
            uint32_t pte = (uint32_t)table[i];
            printf("  %09lx: [%08x] ", (current_prefix << 9) | i, pte);
            
            if (PAGING_PAGE_PRESENT(pte)) {
                if (PAGING_PAGE_SWAPPED(pte)) {
                    printf("SWAP(type=%d, off=%d)\n", 
                           PAGING_PTE_SWPTYP(pte), PAGING_PTE_SWPOFF(pte));
                } else {
                    printf("RAM(FPN=%d%s)\n", 
                           PAGING_PTE_FPN(pte),
                           PAGING_PTE_DIRTY(pte) ? ",Dirty" : "");
                }
            } else {
                printf("NOT PRESENT\n");
            }
        } else {
            // Intermediate levels, table[i] is pointer to next table
            print_pgtbl_recursive((addr_t *)table[i], level - 1, (current_prefix << 9) | i);
        }
    }
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  printf("Page Table Dump for PID %d:\n", caller->pid);
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || caller->krnl->mm->pgd == NULL) {
      printf("Page table not initialized.\n");
      return -1;
  }
  
  // Start recursion from PGD (Level 5)
  print_pgtbl_recursive(caller->krnl->mm->pgd, 5, 0);

  return 0;
}

/*
 * pg_getpage - get the page in ram, handle page fault if needed
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
    uint32_t pte = pte_get_entry(caller, pgn);
    
    if (!PAGING_PAGE_PRESENT(pte)) {
        // Page fault - page not present
        return -1;
    }
    
    if (PAGING_PAGE_SWAPPED(pte)) {
        // Page is in swap, need to swap it in
        addr_t swpfpn = PAGING_PTE_SWPOFF(pte);
        addr_t ramfpn;
        
        // Get free frame in RAM
        pthread_mutex_lock(&mm_lock);
        if (MEMPHY_get_freefp(caller->krnl->mram, &ramfpn) < 0) {
            // RAM full, need victim
            addr_t vicpgn;
            if (find_victim_page(caller->krnl->mm, &vicpgn) < 0) {
                pthread_mutex_unlock(&mm_lock);
                return -1;
            }
            
            uint32_t vicpte = pte_get_entry(caller, vicpgn);
            if (PAGING_PAGE_PRESENT(vicpte) && !PAGING_PAGE_SWAPPED(vicpte)) {
                // Swap out victim
                addr_t vicfpn = PAGING_PTE_FPN(vicpte);
                addr_t new_swpfpn;
                
                if (MEMPHY_get_freefp(caller->krnl->active_mswp, &new_swpfpn) < 0) {
                    pthread_mutex_unlock(&mm_lock);
                    return -1;
                }
                
                // Copy victim to swap
                __swap_cp_page(caller->krnl->mram, vicfpn,
                             caller->krnl->active_mswp, new_swpfpn);
                
                // Update victim's PTE
                pte_set_swap(caller, vicpgn, 0, new_swpfpn);
                
                // Use victim's frame
                ramfpn = vicfpn;
            } else {
                // Victim already swapped
                pthread_mutex_unlock(&mm_lock);
                return -1;
            }
        }
        pthread_mutex_unlock(&mm_lock);
        
        // Swap in requested page
        if (swap_in_page(caller, pgn, ramfpn) < 0) {
            MEMPHY_put_freefp(caller->krnl->mram, ramfpn);
            return -1;
        }
        
        // Add to FIFO list
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
        
        *fpn = ramfpn;
    } else {
        // Page is in RAM
        *fpn = PAGING_PTE_FPN(pte);
        
        // Update FIFO: move to front (most recently used)
        remove_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
    }
    
    return 0;
}

/*
 * find_victim_page - find victim page using FIFO algorithm
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
    if (mm->fifo_pgn == NULL) {
        return -1;
    }
    
    // FIFO: remove the oldest (first in list)
    struct pgn_t *victim = mm->fifo_pgn;
    *retpgn = victim->pgn;
    mm->fifo_pgn = victim->pg_next;
    free(victim);
    
    return 0;
}

/*
 * free_pcb_memph - free all memory resources of a PCB
 */
int free_pcb_memph(struct pcb_t *caller)
{
    pthread_mutex_lock(&mm_lock);
    
    // Free all pages in FIFO list
    while (caller->krnl->mm->fifo_pgn != NULL) {
        struct pgn_t *temp = caller->krnl->mm->fifo_pgn;
        caller->krnl->mm->fifo_pgn = temp->pg_next;
        free(temp);
    }
    
    // Free page table structures recursively
    void free_table_recursive(addr_t *table, int level) {
        if (table == NULL) return;
        
        if (level > 1) {
            // Intermediate level, free child tables
            for (int i = 0; i < 512; i++) {
                if (table[i] != 0) {
                    free_table_recursive((addr_t *)table[i], level - 1);
                }
            }
        }
        free(table);
    }
    
    if (caller->krnl->mm->pgd != NULL) {
        free_table_recursive(caller->krnl->mm->pgd, 5);
        caller->krnl->mm->pgd = NULL;
    }
    
    // Free VMAs
    struct vm_area_struct *vma = caller->krnl->mm->mmap;
    while (vma != NULL) {
        struct vm_area_struct *next = vma->vm_next;
        
        // Free free region list
        struct vm_rg_struct *rg = vma->vm_freerg_list;
        while (rg != NULL) {
            struct vm_rg_struct *next_rg = rg->rg_next;
            free(rg);
            rg = next_rg;
        }
        
        free(vma);
        vma = next;
    }
    caller->krnl->mm->mmap = NULL;
    
    pthread_mutex_unlock(&mm_lock);
    return 0;
}

#endif  // def MM64