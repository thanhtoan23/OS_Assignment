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
#include <time.h>
#include <pthread.h> 

/* Synchronization: Mutex for protecting Physical Memory access and Page Table expansion */
static pthread_mutex_t mm_lock = PTHREAD_MUTEX_INITIALIZER;

#if defined(MM64)

/* * Helper: Allocate a new page table level (array of addr_t) 
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
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

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

    // 2. Traverse PGD (Level 5)
    if (mm->pgd == NULL) return NULL;
    
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


/*
 * pte_set_swap - Set PTE entry for swapped page
 * [cite: 526, 527]
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  pthread_mutex_lock(&mm_lock); // Protect table structure
  
  addr_t *pte = __get_pte_ptr(caller->krnl->mm, pgn, 1); // Alloc if missing
  
  if (pte == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1;
  }

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  CLRBIT(*pte, PAGING_PTE_DIRTY_MASK); // Usually cleared on swap out

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  pthread_mutex_unlock(&mm_lock);
  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * [cite: 522]
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  pthread_mutex_lock(&mm_lock); // Protect table structure

  addr_t *pte = __get_pte_ptr(caller->krnl->mm, pgn, 1); // Alloc if missing

  if (pte == NULL) {
      pthread_mutex_unlock(&mm_lock);
      return -1;
  }

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
  
  pthread_mutex_unlock(&mm_lock);
  return 0;
}


/* Get PTE page table entry */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  addr_t *pte_ptr = __get_pte_ptr(caller->krnl->mm, pgn, 0); // Do not alloc

  if (pte_ptr == NULL) {
      return 0; // Page not present / invalid
  }
  
  return (uint32_t)(*pte_ptr);
}

/* Set PTE page table entry */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
    // Note: This overrides the entry directly. 
    pthread_mutex_lock(&mm_lock);
    addr_t *pte_ptr = __get_pte_ptr(caller->krnl->mm, pgn, 1);
    if (pte_ptr) {
        *pte_ptr = (addr_t)pte_val;
    }
    pthread_mutex_unlock(&mm_lock);
    return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 * [cite: 749, 750]
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
  /* Emulate page directory working without real allocation (for sparse testing) */
  int i;
  addr_t pgn_start = PAGING_PGN(addr);
  
  for(i = 0; i < pgnum; i++) {
      // Just ensure the directories exist
      pthread_mutex_lock(&mm_lock);
      __get_pte_ptr(caller->krnl->mm, pgn_start + i, 1);
      pthread_mutex_unlock(&mm_lock);
  }
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
  addr_t pgn = PAGING_PGN(addr);

  /* Update the mapped region information */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING_PAGESZ;

  /* Map range of frames to address space */
  for (pgit = 0; pgit < pgnum; pgit++)
  {
      if (fpit == NULL) break; // Should not happen if alloc matches req

      pte_set_fpn(caller, pgn + pgit, fpit->fpn);
      
      // Tracking for FIFO replacement (enlisting)
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit);

      fpit = fpit->fp_next;
  }

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * [cite: 578, 471]
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
    
    // Lock access to global Physical Memory Manager (MEMPHY)
    pthread_mutex_lock(&mm_lock);
    int res = MEMPHY_get_freefp(caller->krnl->mram, (addr_t *)&ret_fpn);
    pthread_mutex_unlock(&mm_lock);

    if (res == 0)
    {
      newfp_str->fpn = ret_fpn;
      newfp_str->fp_next = NULL;

      if (*frm_lst == NULL) {
          *frm_lst = newfp_str;
      } else {
          last_fp->fp_next = newfp_str;
      }
      last_fp = newfp_str;
    }
    else
    { 
      // Rollback not implemented for simplicity, but critical error
      return -3000; // Out of memory code defined in vm_map_ram
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
    return -1; // Out of Memory
  }

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 * Initialize a empty Memory Management instance
 * [cite: 628, 631]
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* Init PGD (Level 5) - Root of the page table */
  pthread_mutex_lock(&mm_lock);
  mm->pgd = alloc_table_level();
  pthread_mutex_unlock(&mm_lock);

  /* P4D, PUD, PMD, PT will be allocated dynamically on demand */
  mm->p4d = NULL; // Not used as root
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

  vma0->vm_next = NULL;
  vma0->vm_mm = mm;

  mm->mmap = vma0;
  
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

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

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
  printf("n");
  return 0;
}

/* Helper to recursively print page table entries */
void print_pgtbl_recursive(addr_t *table, int level, addr_t current_prefix) {
    if (table == NULL) return;
    
    int i;
    for (i = 0; i < 512; i++) {
        if (table[i] == 0) continue;

        if (level == 1) { // PT Level, table[i] is PTE
             printf("  %05lx: [%08x] (FPN: %ld)\n", 
                    (current_prefix << 9) | i, 
                    (uint32_t)table[i], 
                    PAGING_FPN(table[i]));
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

#endif  //def MM64