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
  // printf("old sbrk: %d \n",old_sbrk);
  // printf("New sbrk: %d \n",new_sbrk);
  if (get_free_vmrg_area(caller, vmaid, inc_sz, &rgnode) == 0){
    caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
    caller->mm->symrgtbl[rgid].rg_end = old_sbrk + inc_sz;
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

    printf("=== pg_getpage DEBUG ===\n");
    printf("PID: %d, Request page: pgn=%d, old_pte=0x%08x\n", 
           caller->pid, pgn, old_pte);
    printf("Page Present? %s\n", 
           PAGING_PAGE_PRESENT(old_pte) ? "YES" : "NO");
    printf("Page Swapped? %s\n", 
           (old_pte & PAGING_PTE_SWAPPED_MASK) ? "YES" : "NO");

    if (!PAGING_PAGE_PRESENT(old_pte))
    { 
        printf(">>> PAGE FAULT TRIGGERED! <<<\n");
        addr_t tgtfpn;
        struct sc_regs regs;

        int is_swapped = (old_pte & PAGING_PTE_SWAPPED_MASK);
        addr_t old_swpfpn = 0;
        if (is_swapped) {
            old_swpfpn = PAGING_SWP(old_pte);
            printf("Page is in SWAP at swpfpn=%lu\n", old_swpfpn);
        } else {
            printf("Page not in SWAP (first access)\n");
        }
        
        // --- 1. CỐ GẮNG LẤY FRAME TRỐNG TRONG RAM ---
        if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0) 
        { 
            printf("RAM has free frame: fpn=%lu\n", tgtfpn);
            
            if (is_swapped) 
            {
                printf("SWAP IN: SWAP(%lu) -> RAM(%lu)\n", 
                       old_swpfpn, tgtfpn);
                
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = old_swpfpn;
                regs.a3 = tgtfpn;
                regs.a4 = 1;
                syscall(caller->krnl, caller->pid, 17, &regs); 
                
                MEMPHY_put_freefp(caller->krnl->active_mswp, old_swpfpn);
                printf("Freed swap frame %lu back to SWAP\n", old_swpfpn);
            } 
            else 
            {
                printf("First allocation in RAM at fpn=%lu\n", tgtfpn);
            }
            
            pte_set_fpn(caller, pgn, tgtfpn); 
            printf("Updated PTE for pgn=%d -> fpn=%lu\n", pgn, tgtfpn);
        } 
        else 
        {
            // --- 2. RAM FULL - THAY THẾ TRANG (GLOBAL FIFO) ---
            printf("RAM FULL! Need to find VICTIM for SWAP OUT\n");
            
            addr_t vicpgn, vicfpn, swpfpn;
            uint32_t vicpte;
            struct pcb_t *vic_owner; // Biến nhận chủ sở hữu từ FIFO

            // Gọi hàm find_victim đã sửa (nhận thêm vic_owner)
            if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) == -1) {
                printf("ERROR: Cannot find victim page\n");
                return -1;
            }

            // Lấy PTE từ bảng trang của ĐÚNG tiến trình sở hữu (vic_owner)
            vicpte = pte_get_entry(vic_owner, vicpgn);
            vicfpn = PAGING_FPN(vicpte);
            
            printf("Selected VICTIM: PID=%d, pgn=%lu, fpn=%lu, pte=0x%08x\n", 
                   vic_owner->pid, vicpgn, vicfpn, vicpte);

            if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1) {
                printf("ERROR: Cannot get free frame in SWAP\n");
                return -1;
            }
            printf("Free SWAP frame obtained: swpfpn=%lu\n", swpfpn);

            // Swap Out: Copy RAM nạn nhân -> SWAP
            printf("SWAP OUT: RAM(%lu) -> SWAP(%lu)\n", vicfpn, swpfpn);
            regs.a1 = SYSMEM_SWP_OP;
            regs.a2 = vicfpn;
            regs.a3 = swpfpn;
            regs.a4 = 0;
            syscall(caller->krnl, caller->pid, 17, &regs);

            // Cập nhật PTE của NẠN NHÂN (vic_owner)
            pte_set_swap(vic_owner, vicpgn, 0, swpfpn);
            printf("Updated VICTIM PTE (PID=%d, pgn=%lu) to point to SWAP(%lu)\n", 
                   vic_owner->pid, vicpgn, swpfpn);

            // Dùng lại frame vật lý vừa giải phóng cho caller
            tgtfpn = vicfpn;
            printf("Victim frame %lu now available for new page\n", tgtfpn);

            if (is_swapped) 
            {
                printf("SWAP IN: SWAP(%lu) -> RAM(%lu)\n", 
                       old_swpfpn, tgtfpn);
                regs.a1 = SYSMEM_SWP_OP;
                regs.a2 = old_swpfpn;
                regs.a3 = tgtfpn;
                regs.a4 = 1;
                syscall(caller->krnl, caller->pid, 17, &regs); 

                MEMPHY_put_freefp(caller->krnl->active_mswp, old_swpfpn);
                printf("Freed swap frame %lu back to SWAP\n", old_swpfpn);
            } else {
                printf("New page allocated to RAM frame %lu\n", tgtfpn);
            }

            // Cập nhật PTE cho trang yêu cầu của caller
            pte_set_fpn(caller, pgn, tgtfpn);
            printf("Updated PTE for pgn=%d -> fpn=%lu\n", pgn, tgtfpn);
        }
        
        // Luôn enlist trang mới nạp vào RAM vào FIFO kèm theo chủ sở hữu
        enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn, caller);
        printf("Added pgn=%d (PID=%d) to FIFO list\n", pgn, caller->pid);
        
        *fpn = (int)tgtfpn; // Sử dụng trực tiếp tgtfpn thay vì gọi lại pte_get_entry
    } else {
        /* * Trường hợp trang ĐÃ PRESENT (đã được khởi tạo/ánh xạ trong bảng trang)
          * Cần kiểm tra xem nó đang thực sự nằm ở RAM hay đang bị SWAP OUT.
          */
        int is_swapped = (old_pte & PAGING_PTE_SWAPPED_MASK);

        if (is_swapped) 
        {
            /* * TRƯỜNG HỢP 1: Trang đang ở SWAP
              */
            printf("Page is present but currently SWAPPED OUT. Triggering Swap-In...\n");
            
            addr_t old_swpfpn = PAGING_SWP(old_pte);
            addr_t tgtfpn;
            struct sc_regs regs;

            // 1. Tìm frame trống trong RAM
            if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) != 0) 
            {
                // Nếu RAM đầy, phải chọn nạn nhân để Swap Out (Global FIFO)
                addr_t vicpgn, vicfpn, swpfpn;
                uint32_t vicpte;
                struct pcb_t *vic_owner;

                if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) == -1) {
                    printf("ERROR: Cannot find victim page\n");
                    return -1;
                }

                vicpte = pte_get_entry(vic_owner, vicpgn);
                vicfpn = PAGING_FPN(vicpte);

                // Lấy chỗ trống trong Swap cho nạn nhân
                if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1) {
                  printf("Out of SWAP\n");
                  return -1;
                }

                // Thực hiện Swap Out nạn nhân
                regs.a1 = SYSMEM_SWP_OP; 
                regs.a2 = vicfpn; 
                regs.a3 = swpfpn;
                regs.a4 = 0;
                syscall(caller->krnl, caller->pid, 17, &regs);

                // Cập nhật PTE của nạn nhân
                pte_set_swap(vic_owner, vicpgn, 0, swpfpn);
                tgtfpn = vicfpn; // Lấy frame vừa trống
            }

            // 2. Đưa trang từ Swap về RAM
            regs.a1 = SYSMEM_SWP_OP;
            regs.a2 = old_swpfpn;
            regs.a3 = tgtfpn;
            regs.a4 = 1;
            syscall(caller->krnl, caller->pid, 17, &regs);

            // 3. Trả lại slot Swap cũ và cập nhật PTE cho tiến trình hiện tại
            MEMPHY_put_freefp(caller->krnl->active_mswp, old_swpfpn);
            pte_set_fpn(caller, pgn, tgtfpn);
            
            // 4. Đưa vào danh sách FIFO để quản lý thay thế sau này
            enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn, caller);
            
            *fpn = (int)tgtfpn;
        } 
        else 
        {
            /* * TRƯỜNG HỢP 2: Trang thực sự đang nằm trong RAM
              */
            printf("Page already in RAM\n");
            *fpn = PAGING_FPN(old_pte);
        }
    }

    printf("Returning fpn=%d for pgn=%d\n", *fpn, pgn);
    printf("=== End pg_getpage ===\n\n");

    return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
  // BƯỚC 1: Tách địa chỉ ảo thành page number + offset
  addr_t pgn = PAGING64_PGN(addr);        // Page number (phần cao của VA)
  addr_t off = PAGING64_OFFST(addr);      // Offset (phần thấp của VA)
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
  
  if (syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
    return -1;
  }

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
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
  // BƯỚC 1: Tách địa chỉ ảo thành page number + offset
  addr_t pgn = PAGING64_PGN(addr);        // Page number (phần cao của VA)
  addr_t off = PAGING64_OFFST(addr);      // Offset (phần thấp của VA)
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

  /* 1. Giải phóng các khung trang vật lý (RAM và SWAP) */
  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = pte_get_entry(caller, pagenum);

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if (pte != 0) // Chỉ giải phóng nếu PTE có dữ liệu (tránh giải phóng nhầm swap 0)
    {
      fpn = PAGING_SWP(pte);
      if (fpn != 0) // Giả sử swap offset 0 là không hợp lệ hoặc dùng cờ khác
        MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  /* 2. Dọn dẹp danh sách FIFO toàn cục */
  printf("Cleaning up FIFO nodes for PID=%d\n", caller->pid);
  struct pgn_t *curr = caller->krnl->mm->fifo_pgn;
  struct pgn_t *prev = NULL;

  while (curr != NULL)
  {
    if (curr->owner == caller) // Tìm thấy node thuộc về tiến trình này
    {
      struct pgn_t *tmp = curr;

      if (prev == NULL) /* Node nằm ở đầu danh sách */
      {
        caller->krnl->mm->fifo_pgn = curr->pg_next;
        curr = caller->krnl->mm->fifo_pgn;
      }
      else /* Node nằm ở giữa hoặc cuối */
      {
        prev->pg_next = curr->pg_next;
        curr = curr->pg_next;
      }

      free(tmp); // Giải phóng node
    }
    else
    {
      prev = curr;
      curr = curr->pg_next;
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
int find_victim_page(struct mm_struct *mm, addr_t *retpgn, struct pcb_t **ret_owner)
{
  struct pgn_t *pg = mm->fifo_pgn;
  print_list_pgn(pg);

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    printf("ERROR: FIFO list is empty, no victim found\n");
    return -1;
  }
  
  // DEBUG: In ra danh sách FIFO hiện tại
  printf("\n=== FIFO List before victim selection ===\n");
  struct pgn_t *temp = mm->fifo_pgn;
  int count = 0;
  while (temp) {
    printf("  [%d] pgn=%lu\n", count++, temp->pgn);
    temp = temp->pg_next;
  }
  printf("=== End FIFO List ===\n\n");
  
  /* Special case: only one page in FIFO list */
  if (pg->pg_next == NULL)
  {
    *retpgn = pg->pgn;
    *ret_owner = pg->owner;
    printf("Selected victim (only page): pgn=%lu\n", pg->pgn);
    mm->fifo_pgn = NULL;
    free(pg);
    return 0;
  }
  
  /* FIFO: Select the first page (oldest) */
  *retpgn = pg->pgn;
  *ret_owner = pg->owner;
  printf("Selected victim (FIFO first): pgn=%lu\n", pg->pgn);
  
  /* Remove from list */
  mm->fifo_pgn = pg->pg_next;
  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, addr_t size, struct vm_rg_struct *newrg)
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