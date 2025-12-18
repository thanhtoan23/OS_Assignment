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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "timer.h" // Thêm thư viện này

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;

#define MAX_PID_TRACKING 1000 //Thêm define
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
static uint64_t proc_start_time[MAX_PID_TRACKING]; //Thêm cả cái này
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			return 0;
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
	// Thêm luôn vòng for
    for (i = 0; i < MAX_PID_TRACKING; i++) {
        proc_start_time[i] = 0;
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


	int retry_count = 0;
	const int MAX_RETRIES = 2;

	while (queue_empty() && retry_count < MAX_RETRIES) {
		pthread_mutex_unlock(&queue_lock); 
		usleep(1000); 
		pthread_mutex_lock(&queue_lock);  
		retry_count++;
	}

	for (int i = 0; i < MAX_PRIO; i++) {
		if (!empty(&mlq_ready_queue[i])) {
			if (slot[i] > 0) {
				proc = dequeue(&mlq_ready_queue[i]);
				//slot[i]--; Bỏ
				break;
			} else {
				slot[i] = MAX_PRIO - i;
			}
		}
	}

	if (proc == NULL && !queue_empty()) {
		for (int i = 0; i < MAX_PRIO; i++) {
			if (!empty(&mlq_ready_queue[i])) {
				proc = dequeue(&mlq_ready_queue[i]);
				//slot[i]--; Bỏ nốt
				break;
			}
		}
	}



	if (proc != NULL) {
		enqueue(&running_list, proc);
		// Thêm if cho thời gian bắt đầu
        if (proc->pid < MAX_PID_TRACKING) {
            proc_start_time[proc->pid] = current_time();
        }

	}

	

	pthread_mutex_unlock(&queue_lock);

	return proc;

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

	// Thêm cái này
    if (proc->pid < MAX_PID_TRACKING) {
        uint64_t end_time = current_time();
        uint64_t start = proc_start_time[proc->pid];

        if (end_time >= start) {
            int diff = (int)(end_time - start);
            slot[proc->prio] -= diff;
        } else {
            slot[proc->prio]--;
        }
    } else {
        slot[proc->prio]--;
    }


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

/*
struct pcb_t * get_proc(void) {

	struct pcb_t * proc = NULL;

	pthread_mutex_lock(&queue_lock);





	int retry_count = 0;

	const int MAX_RETRIES = 5;



	while (queue_empty() && retry_count < MAX_RETRIES) {

		pthread_mutex_unlock(&queue_lock);

		usleep(1000); 

		pthread_mutex_lock(&queue_lock);

		retry_count++;

	}



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
*/

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