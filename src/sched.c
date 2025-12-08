/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;

	for (i = 0; i < MAX_PRIO; i ++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; 
	}
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) {

	struct pcb_t * proc = NULL;
    pthread_mutex_lock(&queue_lock);

    // Luôn quét từ Priority cao nhất (0) -> Thấp nhất
    for (int i = 0; i < MAX_PRIO; i++) {
        
        // TRƯỜNG HỢP 1: Có process và Còn slot -> LẤY NGAY
        if (!empty(&mlq_ready_queue[i]) && slot[i] > 0) {
            proc = dequeue(&mlq_ready_queue[i]);
            slot[i]--;
            break; // Tìm thấy process ở priority cao nhất có thể -> Thoát
        } 
        
        // TRƯỜNG HỢP 2: Hết slot HOẶC Queue rỗng -> Cần nạp lại slot cho lần sau
        // Nếu không nạp lại, lần sau quay lại đây slot vẫn bằng 0 -> Process bị kẹt vĩnh viễn
        else if (slot[i] == 0 || empty(&mlq_ready_queue[i])) {
            slot[i] = MAX_PRIO - i; // Reset slot
            
            // Lưu ý: Không break ở đây. 
            // Vì queue này hiện tại không đủ điều kiện chạy (do vừa hết slot hoặc rỗng),
            // nên ta phải continue để kiểm tra queue ưu tiên thấp hơn (i+1).
        }
    }

    if (proc != NULL) {
        enqueue(&running_list, proc);
    }
    
    pthread_mutex_unlock(&queue_lock);
    return proc;


	// struct pcb_t * proc = NULL;
    // pthread_mutex_lock(&queue_lock);

    // // QUAN TRỌNG: Luôn khởi đầu i = 0 để ưu tiên hàng đợi cao nhất
    // for (int i = 0; i < MAX_PRIO; i++) {
    //     if (!empty(&mlq_ready_queue[i]) && slot[i] > 0) {
    //         proc = dequeue(&mlq_ready_queue[i]);
    //         slot[i]--;
    //         break; // Tìm thấy ở độ ưu tiên cao nhất có thể -> Lấy ngay và thoát
    //     } else if (empty(&mlq_ready_queue[i])) {
    //         // Chỉ reset slot khi hàng đợi rỗng (hoặc tùy logic đề bài)
    //         // Nhưng quan trọng là phải duyệt tuần tự 0 -> 1 -> 2...
    //         slot[i] = MAX_PRIO - i;
    //     }
    // }

    // if (proc != NULL) enqueue(&running_list, proc);
    // pthread_mutex_unlock(&queue_lock);
    // return proc;

/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// struct pcb_t * proc = NULL;
    // /* * Hien thuc co che State ma khong can bien toan cuc

    //  * static var se giu gia tri giua cac lan goi ham

    //  */

    // static int curr_prio = 0;
	// pthread_mutex_lock(&queue_lock);

	// /*TODO: get a process from PRIORITY [ready_queue].

	//  * It worth to protect by a mechanism.

	//  * */

    // // Duyet qua cac queue de tim process

    // // Gioi han vong lap de tranh loop vo han neu toan bo he thong rong

    // for (int i = 0; i < MAX_PRIO; i++) {
    //     // Kiem tra queue hien tai co process khong VA con slot khong
    //     if (!empty(&mlq_ready_queue[curr_prio]) && slot[curr_prio] > 0) {
    //         // Lay process ra khoi ready queue
    //         proc = dequeue(&mlq_ready_queue[curr_prio]);
    //         // Giam slot
    //         slot[curr_prio]--;
    //         // Tim thay roi thi dung lai
    //         break;
    //     } else {
    //         // Neu queue rong HOAC het slot:
    //         // 1. Reset slot cho lan sau
    //         slot[curr_prio] = MAX_PRIO - curr_prio;
    //         // 2. Chuyen sang priority tiep theo
    //         curr_prio++;
    //         if (curr_prio >= MAX_PRIO) {
    //             curr_prio = 0;
    //         }
    //     }
    // }

	// if (proc != NULL) {
    //     // Dua vao danh sach dang chay (running_list)
    //     // Day la phan co san cua skeleton, khong phai enqueu lai ready_queue
	// 	enqueue(&running_list, proc);
    // }

    // pthread_mutex_unlock(&queue_lock);
	// return proc;	
}

void put_mlq_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 * 
	 */
       
	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);	
}

struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
	return add_mlq_proc(proc);
}
#else
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;
	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from [ready_queue].
	 *       It worth to protect by a mechanism.
	 * 
	 */

    // Logic cho Non-MLQ: uu tien lay tu ready_queue, neu khong thi run_queue
    if (!empty(&ready_queue)) {

        proc = dequeue(&ready_queue);

    } else if (!empty(&run_queue)) {

        proc = dequeue(&run_queue);

    }



    if (proc != NULL) {

        enqueue(&running_list, proc);

    }



	pthread_mutex_unlock(&queue_lock);



	return proc;
}

void put_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);	
}
#endif


