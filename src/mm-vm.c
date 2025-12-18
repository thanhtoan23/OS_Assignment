/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "mm64.h"

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  
  struct vm_area_struct *pvma = mm->mmap; // start from head
  
  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  /* Traverse until we find an area whose vm_id is >= vmaid */
  while (vmait < vmaid) {  
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }
  
  return pvma;
}

/* __mm_swap_page - generic swap function that can handle both directions
 * @caller: caller process
 * @src_fpn: source frame number
 * @dst_fpn: destination frame number
 * @direction: 0 = RAM->SWAP (swap out), 1 = SWAP->RAM (swap in)
 */
int __mm_swap_page(struct pcb_t *caller, addr_t src_fpn, addr_t dst_fpn, int direction, int swp_type)
{
    if (direction == 0) { // SWAP OUT: RAM -> SWAP[swp_type]
        __swap_cp_page(caller->krnl->mram, src_fpn, caller->krnl->mswp[swp_type], dst_fpn, caller, swp_type);
        printf("SYSCALL: Swap OUT to SWAP[%d] (RAM:%lu -> SWAP:%lu)\n", swp_type, src_fpn, dst_fpn);
    } else { // SWAP IN: SWAP[swp_type] -> RAM
        __swap_cp_page(caller->krnl->mswp[swp_type], src_fpn, caller->krnl->mram, dst_fpn, caller, swp_type);
        printf("SYSCALL: Swap IN from SWAP[%d] (SWAP:%lu -> RAM:%lu)\n", swp_type, src_fpn, dst_fpn);
    }
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->kernl->mm, vmaid);

  //newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = cur_vma->sbrk + size;
  newrg->rg_next = NULL;
  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
// int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
// {
//   //struct vm_area_struct *vma = caller->krnl->mm->mmap;

//   /* TODO validate the planned memory area is not overlapped */
//   if (vmastart >= vmaend)
//   {
//     return -1;
//   }

//   struct vm_area_struct *vma = caller->krnl->mm->mmap;
//   if (vma == NULL)
//   {
//     return -1;
//   }

//   /* TODO validate the planned memory area is not overlapped */

//   struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
//   if (cur_area == NULL)
//   {
//     return -1;
//   }

//   while (vma != NULL)
//   {
//     if (vma != cur_area && OVERLAP(cur_area->vm_start, cur_area->vm_end, vma->vm_start, vma->vm_end))
//     {
//       return -1;
//     }
//     vma = vma->vm_next;
//   }
//   /* End TODO*/

//   return 0;
// }
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  struct vm_area_struct *vma = caller->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  while (vma != NULL)
  {
    if (vma->vm_id == vmaid)
    {
      vma = vma->vm_next;
      continue;
    }
    if ((vmastart >= vma->vm_start && vmastart < vma->vm_end) ||
        (vmaend > vma->vm_start && vmaend <= vma->vm_end) ||
        (vmastart <= vma->vm_start && vmaend >= vma->vm_end))
    {
      return -1;
    }

    vma = vma->vm_next;
  }
  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
// int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
// {
//   // printf("Syscall dc goi \n");
//   //struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));

//   /* TOTO with new address scheme, the size need tobe aligned 
//    *      the raw inc_sz maybe not fit pagesize
//    */ 
//   //addr_t inc_amt;

// //  int incnumpage =  inc_amt / PAGING_PAGESZ;

//   /* TODO Validate overlap of obtained region */
//   //if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
//   //  return -1; /*Overlap and failed allocation */

//   /* TODO: Obtain the new vm area based on vmaid */
//   //cur_vma->vm_end... 
//   // inc_limit_ret...
//   /* The obtained vm area (only)
//    * now will be alloc real ram region */

// //  if (vm_map_ram(caller, area->rg_start, area->rg_end, 
// //                   old_end, incnumpage , newrg) < 0)
// //    return -1; /* Map the memory to MEMRAM */
//   int inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz); // align size 
//   int incnumpage = inc_amt / PAGING64_PAGESZ; // number of pages to alloc

//   // lấy region mới tại sbrk
//   struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
//   if (area == NULL) {
//     return -1;
//   }
  
//   // lấy vma hiện tại
//   struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
//   if (cur_vma == NULL) {
//     free(area);
//     return -1;
//   }
  
//   // lưu lại giá trị cũ của vm_end
//   int old_end = cur_vma->vm_end;
//   // kiểm tra overlap
//   if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0) {
//     free(area);
//     return -1;
//   }
//   // mở rộng HEAP
//   cur_vma->vm_end = area->rg_end;
//   cur_vma->sbrk  = area->rg_end;
//   printf("vm_end sau khi mo rong: %d \n", cur_vma->vm_end);
//   // map physical memory cho vungf mới

//   printf("Map area start: %d \n", area->rg_start);
//   printf("Map area end: %d \n", area->rg_end);
//   printf("So page cap phat: %d \n", incnumpage);
//   struct memphy_struct *temp = caller->krnl->mram;
//   struct framephy_struct *fp = temp->free_fp_list;
//   if (vm_map_ram(caller, area->rg_start, area->rg_end, old_end, incnumpage, area) < 0) {
//     free(area);
//     return -1;
//   }
  
//   return 0;
// }
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  int inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = inc_amt / PAGING64_PAGESZ;
  struct vm_rg_struct *area =
      get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  /*Validate overlap of obtained region */
  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) <
      0)
    return -1; /*Overlap and failed allocation */

  /* The obtained vm area (only)
   * now will be alloc real ram region */

  if (vm_map_ram(caller, area->rg_start, area->rg_end, cur_vma->sbrk, incnumpage,
                 newrg) < 0)
  {
    printf("Error: Can't mapping memory!\n");
    return -1; /* Map the memory to MEMRAM */
  }
  
  cur_vma->sbrk += inc_amt;
  return 0;
}

// #endif
