/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
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
  if (mm == NULL || rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
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
  int inc_sz=0;
  

  if (cur_vma == NULL) {
    printf("DEBUG ERROR: cur_vma is NULL for vmaid %d. Process memory not initialized?\n", vmaid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1; 
  }

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
  
// #ifdef MM64
//   inc_sz = (uint32_t)(size/(int)PAGING64_PAGESZ);
//   inc_sz = inc_sz + 1;
  
// #else
//   inc_sz = PAGING_PAGE_ALIGNSZ(size);
  
// #endif  
//   int old_sbrk;
//   inc_sz = inc_sz + 1;
//   old_sbrk = cur_vma->sbrk;
//   /* TODO INCREASE THE LIMIT
//    * SYSCALL 1 sys_memmap
//    */
//   struct sc_regs regs;
//   regs.a1 = SYSMEM_INC_OP;
//   regs.a2 = vmaid;

// #ifdef MM64
//   //regs.a3 = size;
//   regs.a3 = PAGING64_PAGE_ALIGNSZ(size);
// #else
//   regs.a3 = PAGING_PAGE_ALIGNSZ(size);
// #endif  

  addr_t inc_amt;
  #ifdef MM64
    inc_amt = PAGING64_PAGE_ALIGNSZ(size);  // ← Align đúng
  #else
    inc_amt = PAGING_PAGE_ALIGNSZ(size);
  #endif
  
  // Gọi syscall với inc_amt đã align
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = inc_amt;  // ← Dùng inc_amt đã align

  syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */
  
  /* Try to get free region again after expanding HEAP */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0){
      caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
      caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
      *alloc_addr = rgnode.rg_start;
  } else {
      /* Still failed after expanding HEAP - return error */
      printf("ERROR: Cannot allocate %lu bytes even after expanding HEAP\n", (unsigned long)size);
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }



  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
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
 *
 */
// int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
// {

//   uint32_t pte = pte_get_entry(caller, pgn);

//   if (!PAGING_PAGE_PRESENT(pte))
//   { /* Page is not online, make it actively living */
//     addr_t vicpgn, swpfpn;
// //    addr_t vicfpn;
// //    addr_t vicpte;
// //  struct sc_regs regs;

//     /* TODO Initialize the target frame storing our variable */
// //  addr_t tgtfpn 

//     /* TODO: Play with your paging theory here */
//     /* Find victim page */
//     if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
//     {
//       return -1;
//     }

//     /* Get free frame in MEMSWP */
//     if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
//     {
//       return -1;
//     }

//     /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

//     /* TODO copy victim frame to swap 
//      * SWP(vicfpn <--> swpfpn)
//      * SYSCALL 1 sys_memmap
//      */


//     /* Update page table */
//     //pte_set_swap(...);

//     /* Update its online status of the target page */
//     //pte_set_fpn(...);

//     enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
//   }

//   *fpn = PAGING_FPN(pte_get_entry(caller,pgn));

//   return 0;
// }
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  // BƯỚC 1: Lấy PTE (Page Table Entry) của trang
  uint32_t pte = pte_get_entry(caller, pgn);

  // BƯỚC 2: Check xem trang này có ở RAM không?
  if (!PAGING_PAGE_PRESENT(pte))
  { 
    /* PAGE FAULT! Trang không ở RAM → phải SWAP */
    
    addr_t vicpgn;           // Victim page number (trang cũ nhất)
    addr_t vicfpn;           // Victim frame number (frame của trang cũ)
    addr_t swpfpn;           // Frame trong SWAP device
    uint32_t vicpte;         // PTE của victim page
    
    // BƯỚC 2.1: Tìm victim page (trang cũ nhất theo FIFO)
    if (find_victim_page(caller->mm, &vicpgn) == -1)
    {
      return -1;
    }

    // BƯỚC 2.2: Lấy PTE của victim page
    vicpte = pte_get_entry(caller, vicpgn);
    
    // BƯỚC 2.3: Lấy frame number của victim (đang ở RAM)
    vicfpn = PAGING_PTE_FPN(vicpte);

    // BƯỚC 2.4: Cấp một frame trống trong SWAP device
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    // BƯỚC 2.5: SWAP victim frame từ RAM → SWAP
    // Copy dữ liệu từ frame RAM đến frame SWAP
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);

    // BƯỚC 2.6: SWAP requested page từ SWAP → RAM (vào frame cũ của victim)
    // Lấy swap offset từ PTE cũ của requested page
    addr_t old_swpfpn = PAGING_SWP(pte);  // Requested page đang ở đâu trong SWAP
    
    // Copy dữ liệu từ SWAP vào frame RAM (reuse victim's frame)
    __swap_cp_page(caller->krnl->active_mswp, old_swpfpn, caller->krnl->mram, vicfpn);

    // BƯỚC 2.7: Cập nhật PTE của victim: đánh dấu nó ở SWAP
    // Victim page không ở RAM nữa, ở SWAP device
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    // BƯỚC 2.8: Cập nhật PTE của requested page: đánh dấu nó ở RAM
    // Requested page bây giờ ở RAM ở frame vicfpn
    pte_set_fpn(caller, pgn, vicfpn);

    // BƯỚC 2.9: Thêm requested page vào FIFO list (để lần sau biết trang nào cũ nhất)
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }

  // BƯỚC 3: Lấy FPN từ PTE (dù page mới load hay đã ở sẵn)
  *fpn = PAGING_PTE_FPN(pte_get_entry(caller, pgn));

  return 0;
}


/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
// int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
// {
//   int pgn = PAGING_PGN(addr);
// //  int off = PAGING_OFFST(addr);
//   int fpn;

//   if (pg_getpage(mm, pgn, &fpn, caller) != 0)
//     return -1; /* invalid page access */

// //  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

//   /* TODO 
//    *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
//    *  MEMPHY READ 
//    *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
//    */

//   return 0;
// }
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  // BƯỚC 1: Tách địa chỉ ảo thành page number + offset
  int pgn = PAGING64_PGN(addr);        // Page number (phần cao của VA)
  int off = addr & ((1 << 12) - 1);    // Offset (12 bits thấp)
  int fpn;                              // Frame number sẽ lấy từ pg_getpage

  // BƯỚC 2: Đảm bảo trang ở RAM (nếu không → trigger page fault → swap)
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  // BƯỚC 3: Tính địa chỉ vật lý (Physical Address)
  // PA = (FPN * PAGE_SIZE) + offset
  int phyaddr = (fpn * PAGING64_PAGESZ) + off;

  // BƯỚC 4: Đọc byte từ RAM bằng syscall
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;  // Lệnh = "đọc từ RAM"
  regs.a2 = phyaddr;         // Địa chỉ vật lý cần đọc
  regs.a3 = 0;               // Chỗ lưu kết quả (được kernel điền)
  
  if (syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    return -1;

  // BƯỚC 5: Lấy giá trị byte đọc được từ regs.a3
  *data = (BYTE)regs.a3;

  return 0;
}



/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
// int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
// {
//   int pgn = PAGING_PGN(addr);
// //  int off = PAGING_OFFST(addr);
//   int fpn;

//   /* Get the page to MEMRAM, swap from MEMSWAP if needed */
//   if (pg_getpage(mm, pgn, &fpn, caller) != 0)
//     return -1; /* invalid page access */


//   /* TODO 
//    *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
//    *  MEMPHY WRITE with SYSMEM_IO_WRITE 
//    * SYSCALL 17 sys_memmap
//    */

//   return 0;
// }
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  // BƯỚC 1: Tách địa chỉ ảo thành page number + offset
  int pgn = PAGING64_PGN(addr);        // Page number (phần cao của VA)
  int off = addr & ((1 << 12) - 1);    // Offset (12 bits thấp)
  int fpn;                              // Frame number sẽ lấy từ pg_getpage

  // BƯỚC 2: Đảm bảo trang ở RAM (nếu không → trigger page fault → swap)
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  // BƯỚC 3: Tính địa chỉ vật lý (Physical Address)
  // PA = (FPN * PAGE_SIZE) + offset
  int phyaddr = (fpn * PAGING64_PAGESZ) + off;

  // BƯỚC 4: Ghi byte vào RAM bằng syscall
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;  // Lệnh = "ghi vào RAM"
  regs.a2 = phyaddr;          // Địa chỉ vật lý cần ghi
  regs.a3 = value;            // Giá trị byte cần ghi
  
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
// int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
// {
//   struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

//   /* TODO Invalid memory identify */

//   pg_getval(caller->mm, currg->rg_start + offset, data, caller);

//   return 0;
// 
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  
  if (currg == NULL) {
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
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  
  // Check bounds
  if (currg->rg_start + offset >= currg->rg_end) {
    printf("ERROR: Write offset %d out of bounds [%d, %d)\n", 
           offset, currg->rg_start, currg->rg_end);
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
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = pte_get_entry(caller,pagenum);

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  
  if (prev != NULL) {
    prev->pg_next = NULL;
  } else {
    mm->fifo_pgn = NULL; // List chỉ có 1 phần tử, giờ rỗng
  }

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  struct vm_rg_struct **prevnext = &cur_vma->vm_freerg_list; // Track pointer to update
  
  if (rgit == NULL) {
    return -1;
  }

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        /* Partial allocation - update region start */
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          /* Remove this node entirely by updating the previous pointer */
          *prevnext = NULL;
          free(rgit);
        }
      }
      break;
    }
    else
    {
      prevnext = &rgit->rg_next; // Move to next node
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1){
    return -1;
  } // new region not found
    

  return 0;
}

// #endif
