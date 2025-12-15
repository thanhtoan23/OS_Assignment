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
    caller->mm->symrgtbl[rgid].rg_next = NULL;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
  
#ifdef MM64
  inc_sz = (uint32_t)(size/(int)PAGING64_PAGESZ);
  inc_sz = inc_sz + 1;
  
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
  
#endif  
  int old_sbrk;
  inc_sz = inc_sz + 1;
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
  // printf("old sbrk: %d \n",old_sbrk);
  // printf("New sbrk: %d \n",new_sbrk);
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0){
    caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
    caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
    caller->mm->symrgtbl[rgid].rg_next = NULL;
    *alloc_addr = old_sbrk;
  }
  // printf("Da cap phat cho caller \n");
  // printf("caller start: %d \n", caller->mm->symrgtbl[rgid].rg_start );
  // printf("caller end: %d \n", caller->mm->symrgtbl[rgid].rg_end );



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

    if (!PAGING_PAGE_PRESENT(old_pte))
    { 
        /* Xảy ra Page Fault: Trang không có trong RAM */
        addr_t tgtfpn; // Target Frame Page Number
        struct sc_regs regs; // Dùng cho System Call

        // Kiểm tra xem trang này có đang nằm trong SWAP không (Khả năng 1)
        int is_swapped = (old_pte & PAGING_PTE_SWAPPED_MASK);
        addr_t old_swpfpn = 0;
        if (is_swapped) {
            old_swpfpn = PAGING_SWP(old_pte); // Lấy địa chỉ swap cũ
        }
        
        // --- 1. CỐ GẮNG LẤY FRAME TRỐNG TRONG RAM ---
        if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0) 
        { 
          printf("1\n");
            // RAM còn chỗ! Dùng frame này ngay.
            if (is_swapped) 
            {
                /* Trường hợp 1: Trang đã bị Swap Out, giờ Swap In về Frame trống */
                
                // Swap từ MEMSWP (old_swpfpn) sang MEMRAM (tgtfpn)
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = old_swpfpn;
                regs.a3 = tgtfpn;
                // PHẢI DÙNG SYSCALL vì di chuyển dữ liệu vật lý từ thiết bị Swap vào RAM
                syscall(caller->krnl, caller->pid, 17, &regs); 
                
                // Trả frame swap cũ về danh sách trống của Swap
                MEMPHY_put_freefp(caller->krnl->active_mswp, old_swpfpn);

            } 
            else 
            {
              printf("2\n");
                /* Trường hợp 2: Trang chưa từng được cấp phát/không nằm trong Swap (Lần đầu) */
                // Không cần di chuyển dữ liệu, Frame trống là đủ.
            }
            
            // Cập nhật PTE mới để trỏ đến Frame vật lý mới (tgtfpn) và đánh dấu Present
            pte_set_fpn(caller, pgn, tgtfpn); 

        } 
        else 
        {
            // --- 2. RAM ĐÃ ĐẦY! PHẢI TÌM VICTIM ĐỂ SWAP OUT ---
            addr_t vicpgn, vicfpn, swpfpn;
            uint32_t vicpte;

            /* Tìm victim page */
            if (find_victim_page(caller->krnl->mm, &vicpgn) == -1) {
                printf("ERROR: Cannot find victim page\n");
                return -1;
            }

            /* Lấy PTE và FPN của victim */
            vicpte = pte_get_entry(caller, vicpgn);
            vicfpn = PAGING_FPN(vicpte);

            /* Lấy free frame trong MEMSWP cho victim */
            if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1) {
                printf("ERROR: Cannot get free frame in SWAP\n");
                return -1;
            }

            /* Swap Out: Copy victim frame (vicfpn) đến swap (swpfpn) */
            regs.a1 = SYSMEM_SWP_OP;
            regs.a2 = vicfpn;
            regs.a3 = swpfpn;
            // PHẢI DÙNG SYSCALL vì di chuyển dữ liệu vật lý từ RAM ra thiết bị Swap
            syscall(caller->krnl, caller->pid, 17, &regs);

            /* Cập nhật PTE của victim để trỏ đến Swap */
            pte_set_swap(caller, vicpgn, 0, swpfpn);

            /* Frame của victim (vicfpn) giờ là Frame mục tiêu (tgtfpn) */
            tgtfpn = vicfpn;

            // --- XỬ LÝ TRANG HIỆN TẠI VÀO FRAME MỚI (tgtfpn) ---
            if (is_swapped) 
            {
                /* Trường hợp 1: Trang đã bị Swap Out, giờ Swap In về Frame vừa giải phóng */
                
                // Swap từ MEMSWP (old_swpfpn) sang MEMRAM (tgtfpn)
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = old_swpfpn;
                regs.a3 = tgtfpn;
                // PHẢI DÙNG SYSCALL
                syscall(caller->krnl, caller->pid, 17, &regs); 

                // Trả frame swap cũ về danh sách trống của Swap
                MEMPHY_put_freefp(caller->krnl->active_mswp, old_swpfpn);
            }
            /* Nếu không Swap Out (Trường hợp 2), Frame tgtfpn đã được làm sạch */

            // Cập nhật PTE mới để trỏ đến Frame vật lý mới (tgtfpn) và đánh dấu Present
            pte_set_fpn(caller, pgn, tgtfpn);
        }
        
        // Thêm/cập nhật trang mới này vào danh sách quản lý (ví dụ: FIFO)
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
    }

    /* Sau khi xử lý Page Fault (nếu có), trang đã có trong RAM */
    *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

    return 0;
}


/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  // BƯỚC 1: Tách địa chỉ ảo thành page number + offset
  addr_t pgn = PAGING_PGN(addr);        // Page number (phần cao của VA)
  addr_t off = PAGING_OFFST(addr);      // Offset (phần thấp của VA)
  int fpn;                            // Frame number sẽ lấy từ pg_getpage

  // BƯỚC 2: Đảm bảo trang ở RAM (nếu không → trigger page fault → swap)
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  // BƯỚC 3: Tính địa chỉ vật lý (Physical Address)
  // PA = (FPN * PAGE_SIZE) + offset
  addr_t phyaddr = (fpn * PAGING64_PAGESZ) + off;

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
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  // BƯỚC 1: Tách địa chỉ ảo thành page number + offset
  addr_t pgn = PAGING_PGN(addr);        // Page number (phần cao của VA)
  addr_t off = PAGING_OFFST(addr);      // Offset (phần thấp của VA)
  int fpn;                            // Frame number sẽ lấy từ pg_getpage

  // BƯỚC 2: Đảm bảo trang ở RAM (nếu không → trigger page fault → swap)
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  // BƯỚC 3: Tính địa chỉ vật lý (Physical Address)
  // PA = (FPN * PAGE_SIZE) + offset
  addr_t phyaddr = (fpn * PAGING64_PAGESZ) + off;

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
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
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
  
  /* Special case: only one page in FIFO list */
  if (pg->pg_next == NULL)
  {
    *retpgn = pg->pgn;
    mm->fifo_pgn = NULL;
    free(pg);
    return 0;
  }
  
  /* Find the last page (oldest in FIFO) */
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  prev->pg_next = NULL;

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
  if (rgit == NULL)
    return -1;

  // while( rgit2 != NULL){
  //   printf(" rgit start: %d \n",rgit2->rg_start);
  //   printf(" rgit end: %d \n",rgit2->rg_end);
  //   rgit2 = rgit2->rg_next;
  // }
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
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1){
    printf("Region not found \n");
    return -1;
  } // new region not found
    

  return 0;
}

// #endif