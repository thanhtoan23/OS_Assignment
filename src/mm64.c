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
#include <stdlib.h>
#include <pthread.h>
#include "string.h" //include to use memset and calloc
#if defined(MM64)

/* Synchronization: Mutex for protecting Physical Memory access and Page Table expansion */
static pthread_mutex_t mm_lock = PTHREAD_MUTEX_INITIALIZER;

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
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct krnl_t *krnl = caller->krnl; //uncomment

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
	
  // dummy pte alloc to avoid runtime error
  // pte = malloc(sizeof(addr_t));
#ifdef MM64	
  pthread_mutex_lock(&mm_lock);
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
  //my implementation
/* helper pattern: allocate table if missing, and store pointer (addr_t) */
addr_t *p4d_table, *pud_table, *pmd_table, *pt_table;

/* allocate/get P4D from PGD */
if (krnl->mm->pgd[pgd] == 0) {
    p4d_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    krnl->mm->pgd[pgd] = (addr_t)p4d_table;
} else {
    p4d_table = (addr_t*)krnl->mm->pgd[pgd];
}

/* allocate/get PUD from P4D */
if (p4d_table[p4d] == 0) {
    pud_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    p4d_table[p4d] = (addr_t)pud_table;
} else {
    pud_table = (addr_t*)p4d_table[p4d];
}

/* allocate/get PMD from PUD */
if (pud_table[pud] == 0) {
    pmd_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    pud_table[pud] = (addr_t)pmd_table;
} else {
    pmd_table = (addr_t*)pud_table[pud];
}

/* allocate/get PT from PMD */
if (pmd_table[pmd] == 0) {
    pt_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    pmd_table[pmd] = (addr_t)pt_table;
} else {
    pt_table = (addr_t*)pmd_table[pmd];
}

/* PTE is then &pt_table[pt] */
  pte = &pt_table[pt];
  // end my implementation
#else
  pte = &krnl->mm->pgd[pgn];
#endif
	
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  pthread_mutex_unlock(&mm_lock);
  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct krnl_t *krnl = caller->krnl; //uncomment

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
	
  // dummy pte alloc to avoid runtime error
  // pte = malloc(sizeof(addr_t));
#ifdef MM64	
  pthread_mutex_lock(&mm_lock);
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
  // my implementation
/* helper pattern: allocate table if missing, and store pointer (addr_t) */
  addr_t *p4d_table, *pud_table, *pmd_table, *pt_table;

/* allocate/get P4D from PGD */
  if (krnl->mm->pgd[pgd] == 0) {
    p4d_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    krnl->mm->pgd[pgd] = (addr_t)p4d_table;
  } else {
    p4d_table = (addr_t*)krnl->mm->pgd[pgd];
  }

/* allocate/get PUD from P4D */
  if (p4d_table[p4d] == 0) {
    pud_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    p4d_table[p4d] = (addr_t)pud_table;
  } else {
    pud_table = (addr_t*)p4d_table[p4d];
  } 

/* allocate/get PMD from PUD */
  if (pud_table[pud] == 0) {
    pmd_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    pud_table[pud] = (addr_t)pmd_table;
  } else {
    pmd_table = (addr_t*)pud_table[pud];
  }

/* allocate/get PT from PMD */
  if (pmd_table[pmd] == 0) {
    pt_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    pmd_table[pmd] = (addr_t)pt_table;
  } else {
    pt_table = (addr_t*)pmd_table[pmd];
  }

/* PTE is then &pt_table[pt] */
  pte = &pt_table[pt];
#else
  pte = &krnl->mm->pgd[pgn];
#endif

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  pthread_mutex_unlock(&mm_lock);
  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  // printf("DEBUG: [pte_set_fpn] PID %d, mm->pgd: %p\n", 
  //          caller->pid, caller->krnl->mm->pgd);
  struct krnl_t *krnl = caller->krnl; //uncomment
  uint32_t pte = 0;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t	pt=0;
	
  /* TODO Perform multi-level page mapping */
  pthread_mutex_lock(&mm_lock);
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;	
  //my implementation
#ifdef MM64
  addr_t *p4d_table, *pud_table, *pmd_table, *pt_table;
  
  // take P4D
  p4d_table = (addr_t*)krnl->mm->pgd[pgd];
  if (p4d_table == NULL) return 0;
  
  // take PUD
  pud_table = (addr_t*)p4d_table[p4d];
  if (pud_table == NULL) return 0;
  
  // take PMD
  pmd_table = (addr_t*)pud_table[pud];
  if (pmd_table == NULL) return 0;
  
  // take PT
  pt_table = (addr_t*)pmd_table[pmd];
  if (pt_table == NULL) return 0;
  
  // take PTE value
  pte = pt_table[pt];
#else
  pte = krnl->mm->pgd[pgn];
#endif
  pthread_mutex_unlock(&mm_lock);
  return pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
	// struct krnl_t *krnl = caller->krnl;
	// krnl->mm->pgd[pgn]=pte_val;
	//my implementation
  // printf("DEBUG: [pte_set_fpn] PID %d, mm->pgd: %p\n", 
  //          caller->pid, caller->krnl->mm->pgd);
#ifdef MM64
  //implement 64 bit similar to pte_set_fpn/swap function but assign directly
  struct krnl_t *krnl = caller->krnl;
  addr_t *pte;
  addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;
  
  pthread_mutex_lock(&mm_lock);
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

/* helper pattern: allocate table if missing, and store pointer (addr_t) */
  addr_t *p4d_table, *pud_table, *pmd_table, *pt_table;

/* allocate/get P4D from PGD */
  if (krnl->mm->pgd[pgd] == 0) {
    p4d_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    krnl->mm->pgd[pgd] = (addr_t)p4d_table;
  } else {
    p4d_table = (addr_t*)krnl->mm->pgd[pgd];
  }

/* allocate/get PUD from P4D */
  if (p4d_table[p4d] == 0) {
    pud_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    p4d_table[p4d] = (addr_t)pud_table;
  } else {
    pud_table = (addr_t*)p4d_table[p4d];
  }

/* allocate/get PMD from PUD */
  if (pud_table[pud] == 0) {
    pmd_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    pud_table[pud] = (addr_t)pmd_table;
  } else {
    pmd_table = (addr_t*)pud_table[pud];
  }

/* allocate/get PT from PMD */
  if (pmd_table[pmd] == 0) {
    pt_table = calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
    pmd_table[pmd] = (addr_t)pt_table;
  } else {
    pt_table = (addr_t*)pmd_table[pmd];
  }

/* PTE is then &pt_table[pt] */
  pte = &pt_table[pt];
  *pte = pte_val;
#else
	struct krnl_t *krnl = caller->krnl;
	krnl->mm->pgd[pgn]=pte_val;
#endif
  pthread_mutex_unlock(&mm_lock);
	return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  int pgit = 0;
  addr_t pgn;
  uint64_t pattern = 0xdeadbeefdeadbeef;
  addr_t pgn_start = addr >> PAGING64_ADDR_PT_SHIFT; //add variable
  /* TODO memset the page table with given pattern
   */
  //MY IMPLEMENTATION
  for (pgit = 0; pgit < pgnum; pgit++){
    pgn = pgn_start + pgit;
    //call pte_set_fpn with fpn dummy to create level page table
    pte_set_fpn(caller,pgn, pattern);
  }
  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  /* TODO: update the rg_end and rg_start of ret_rg 
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  //enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn64 + pgit);
  // my implementation
  //update rg_end and rg_start of ret_rg
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr; //will be updated later
  //map range of frame to address space
  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++){
    //map page (pgn + pgit) to frame (fpit -> fpn)
    pte_set_fpn(caller, pgn + pgit, fpit->fpn);
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit);
    fpit = fpit ->fp_next;
  }
  //update rg_end base on real pages mapped
  ret_rg->rg_end = addr + (pgit * PAGING64_PAGESZ);
  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *head = NULL; // pointer from head

  /* TODO: allocate the page 
  //caller-> ...
  //frm_lst-> ...
  */
  //my implementation
  if (caller->krnl->mram == NULL)
  {
    *frm_lst = NULL;
    return -1; // Lỗi nghiêm trọng: mram chưa được khởi tạo
  }
  for (pgit = 0; pgit < req_pgnum; pgit++){
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0){
      newfp_str = (struct framephy_struct*)malloc(sizeof(struct framephy_struct));
      if (newfp_str == NULL){
        //malloc error, free all frame allocated and send error
        while (head != NULL){
          struct framephy_struct *tmp = head;
          MEMPHY_put_freefp(caller->krnl->mram, tmp->fpn);
          head = head->fp_next;
          free(tmp);
        }
        *frm_lst = NULL;
        return -1; //allocating memory error
      }
      newfp_str->fpn = fpn;
      newfp_str->fp_next = head; //add to top of the list
      head = newfp_str;
    } else {
      //error: not enough frame 
      //free frames allocated because not enought
      while (head != NULL){
        struct framephy_struct *tmp = head;
        MEMPHY_put_freefp(caller->krnl->mram, tmp->fpn);
        head = head->fp_next;
        free(tmp);
      }
      *frm_lst = NULL;
      return -3000; //error out of memory
    }
  }
  *frm_lst = head; //return list of allocated frames

/*
  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    // TODO: allocate the page 
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
    }
    else
    { // TODO: ERROR CODE of obtaining somes but not enough frames
    }
  }
*/


  /* End TODO */
  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
//  int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);
  //my implementation
  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1; // failed

  /* Out of memory */
  if (ret_alloc == -3000){
    return -1;
  }
  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
//   struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
//   printf("DEBUG: [init_mm] Initializing for PID %d...\n", caller->pid);
//   /* TODO init page table directory */
//    //mm->pgd = ...
//    //mm->p4d = ...
//    //mm->pud = ...
//    //mm->pmd = ...
//    //mm->pt = ...
//   //my implementation
// #ifdef MM64
//    //allocate PGD (level 5 page table), 512 entries (4096 bytes/ 8 bytes/entry)
//    mm->pgd = (uint64_t *)calloc(PAGING64_PAGESZ / sizeof(uint64_t), sizeof(uint64_t));
//    printf("DEBUG: [init_mm] PID %d mm->pgd allocated at %p\n",caller->pid, mm->pgd);
//    // others will be allocated on demand
//    mm->p4d = NULL;
//    mm->pud = NULL;
//    mm->pmd = NULL;
//    mm->pt = NULL;
// #else
//    // Cấp phát PGD cho 32-bit
//    mm->pgd = (uint32_t *)calloc(PAGING_MAX_PGN, sizeof(uint32_t));
// #endif
//   /* By default the owner comes with at least one vma */
//   vma0->vm_id = 0;
//   vma0->vm_start = 0;
//   vma0->vm_end = vma0->vm_start;
//   vma0->sbrk = vma0->vm_start;
//   struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
//   enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

//   // /* TODO update VMA0 next */
//   // // vma0->next = ...
//   vma0->vm_next = NULL;
//   // /* Point vma owner backward */
//   // //vma0->vm_mm = mm; 
//   vma0->vm_mm = mm;
//   // /* TODO: update mmap */
//   // //mm->mmap = ...
//   // //mm->symrgtbl = ...
//   memset(mm->symrgtbl, 0, sizeof(struct vm_rg_struct) * PAGING_MAX_SYMTBL_SZ);
//   //init empty fifo list
//   mm->fifo_pgn = NULL;
//   return 0;
  //my implementation
  // allocate and init vma0
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start; // Giả sử vma0 có thể lớn tối đa = RAM
  vma0->sbrk = vma0->vm_start;
  vma0->vm_next = NULL;
  vma0->vm_mm = mm; // Trỏ ngược lại mm
  vma0->vm_freerg_list = NULL; // Khởi tạo danh sách free

  /* TODO init page table directory */
#ifdef MM64
   /* Cấp phát PGD (bảng cấp 5) */
  mm->pgd = (uint64_t *)calloc(PAGING64_PAGESZ / sizeof(uint64_t), sizeof(uint64_t));
   
  //2. CẤP PHÁT TRƯỚC (PRE-ALLOCATE) PATHWAY ĐẦU TIÊN ([0][0][0][0])
  addr_t *p4d_table = (addr_t *)calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
  mm->pgd[0] = (addr_t)p4d_table;

  addr_t *pud_table = (addr_t *)calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
  p4d_table[0] = (addr_t)pud_table;
   
  addr_t *pmd_table = (addr_t *)calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
  pud_table[0] = (addr_t)pmd_table;
   
  addr_t *pt_table = (addr_t *)calloc(PAGING64_PAGESZ / sizeof(addr_t), sizeof(addr_t));
  pmd_table[0] = (addr_t)pt_table;
   
   // Assign NULL for all base pointers
   mm->p4d = NULL; //
   mm->pud = NULL; //
   mm->pmd = NULL; //
   mm->pt = NULL;  //
#else
   /* Cấp phát PGD cho 32-bit (dự phòng) */
   mm->pgd = (uint32_t *)calloc(PAGING64_MAX_PGN, sizeof(uint32_t));
#endif

  //Gán VMA (mmap) và khởi tạo các trường khác
  mm->mmap = vma0;

  //Khởi tạo bảng symbol (danh sách các biến) về 0
  memset(mm->symrgtbl, 0, sizeof(struct vm_rg_struct) * PAGING_MAX_SYMTBL_SZ);

  // Khởi tạo danh sách FIFO rỗng (dùng cho page replacement)
  mm->fifo_pgn = NULL;

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

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
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
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
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
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
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
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
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

  struct krnl_t *krnl = caller->krnl;

  // We will use address start (0 for default) to find index
  addr_t pgn_start = start >> PAGING64_ADDR_PT_SHIFT;
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_pagenum(pgn_start, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  /* Start traversing from PGD */
  addr_t *p4d_table = (addr_t*)krnl->mm->pgd[pgd_idx];
  addr_t *pud_table = (addr_t*)p4d_table[p4d_idx];
  addr_t *pmd_table = (addr_t*)pud_table[pud_idx];
  //printf result
  printf("print_pgtbl:\n PDG=%016lx P4G=%016lx PUD=%016lx PMD=%016lx\n", 
       (unsigned long)krnl->mm->pgd, 
       (unsigned long)p4d_table, 
       (unsigned long)pud_table, 
       (unsigned long)pmd_table);
  
  // Start recursion from PGD (Level 5)
  print_pgtbl_recursive(caller->krnl->mm->pgd, 5, 0);

  return 0;
}

#endif  //def MM64
