
#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int time_slot;
static int num_cpus;
static int done = 0;
static struct krnl_t os;

#ifdef MM_PAGING
static int memramsz;
static int memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
	/* A dispatched argument struct to compact many-fields passing to loader */
	int vmemsz;
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	struct memphy_struct *active_mswp;
	int active_mswp_id;
	struct timer_id_t  *timer_id;
};
#endif

static struct ld_args{
	char ** path;
	unsigned long * start_time;
#ifdef MLQ_SCHED
	unsigned long * prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
	struct timer_id_t * timer_id;
	int id;
};


static void * cpu_routine(void * args) {
	struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
	int id = ((struct cpu_args*)args)->id;
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t * proc = NULL;
	while (1) {
		/* Check the status of current process */
		if (proc == NULL) {
			/* No process is running, the we load new process from
		 	* ready queue */
			proc = get_proc();
			if (proc == NULL) {

				if (done) {
                    printf("\tCPU %d stopped\n", id);  ///////////// TH: CPU > process
                    break;
                }
                next_slot(timer_id);
                continue; /* First load failed. skip dummy load */
            }
		}else if (proc->pc == proc->code->size) {
			/* The porcess has finish it job */
			printf("\tCPU %d: Processed %2d has finished\n",
				id ,proc->pid);
			free(proc);
			proc = get_proc();
			time_left = 0;
		}else if (time_left == 0) {
			/* The process has done its job in current time slot */
			printf("\tCPU %d: Put process %2d to run queue\n",
				id, proc->pid);
			put_proc(proc);
			proc = get_proc();
		}
		
		/* Recheck process status after loading new process */
		if (proc == NULL && done) {
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}else if (proc == NULL) {
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}else if (time_left == 0) {
			printf("\tCPU %d: Dispatched process %2d\n",
				id, proc->pid);
			time_left = time_slot;
		}
		
		/* Run current process */
		run(proc);
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void * ld_routine(void * args) {
#ifdef MM_PAGING
	struct mmpaging_ld_args *ld_ptr = (struct mmpaging_ld_args *)args;
    struct memphy_struct* mram = ld_ptr->mram;
    struct memphy_struct** mswp = ld_ptr->mswp;
    struct timer_id_t * timer_id = ld_ptr->timer_id;
#else
	struct timer_id_t * timer_id = (struct timer_id_t*)args;
#endif
	int i = 0;
	printf("ld_routine\n");
	while (i < num_processes) {
		struct pcb_t * proc = load(ld_processes.path[i]);
		struct krnl_t * krnl = proc->krnl = &os;	

#ifdef MLQ_SCHED
		proc->prio = ld_processes.prio[i];
#endif
		while (current_time() < ld_processes.start_time[i]) {
			next_slot(timer_id);
		}
		usleep(1000);
#ifdef MM_PAGING
		proc->mm = malloc(sizeof(struct mm_struct));
		init_mm(proc->mm, proc);
		krnl->mram = mram;
		krnl->mswp = mswp;
		krnl->active_mswp_id = ld_ptr->active_mswp_id;
#endif
		printf("\tLoaded a process at %s, PID: %d PRIO: %ld\n",
			ld_processes.path[i], proc->pid, ld_processes.prio[i]);
		add_proc(proc);
		free(ld_processes.path[i]);
		i++;

		// Thêm cái này
		if (i == num_processes || ld_processes.start_time[i] > current_time()) {
			next_slot(timer_id);
		}
		//
		//next_slot(timer_id); Xóa cái này
	}
	free(ld_processes.path);
	free(ld_processes.start_time);
	done = 1;
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void read_config(const char * path) {
	FILE * file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("Cannot find configure file at %s\n", path);
		exit(1);
	}
	fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);
	ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
	ld_processes.start_time = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#ifdef MM_PAGING
	int sit;
#ifdef MM_FIXED_MEMSZ
	/* We provide here a back compatible with legacy OS simulatiom config file
         * In which, it have no addition config line for Mema, keep only one line
	 * for legacy info 
         *  [time slice] [N = Number of CPU] [M = Number of Processes to be run]
         */
        memramsz  =  0x10000000;
        memswpsz[0] = 0x1000000;
	for(sit = 1; sit < PAGING_MAX_MMSWP; sit++)
		memswpsz[sit] = 0;
#else
	/* Read input config of memory size: MEMRAM and upto 4 MEMSWP (mem swap)
	 * Format: (size=0 result non-used memswap, must have RAM and at least 1 SWAP)
	 *        MEM_RAM_SZ MEM_SWP0_SZ MEM_SWP1_SZ MEM_SWP2_SZ MEM_SWP3_SZ
	*/
	fscanf(file, "%d\n", &memramsz);
	printf("Dung luong Ram: %d \n", memramsz);
	for(sit = 0; sit < PAGING_MAX_MMSWP; sit++)
		fscanf(file, "%d", &(memswpsz[sit])); 

       fscanf(file, "\n"); /* Final character */
#endif
#endif

#ifdef MLQ_SCHED
	ld_processes.prio = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#endif
	int i;
	for (i = 0; i < num_processes; i++) {
		ld_processes.path[i] = (char*)malloc(sizeof(char) * 100);
		ld_processes.path[i][0] = '\0';
		strcat(ld_processes.path[i], "input/proc/");
		char proc[100];
#ifdef MLQ_SCHED
		fscanf(file, "%lu %s %lu\n", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
#else
		fscanf(file, "%lu %s\n", &ld_processes.start_time[i], proc);
#endif
		strcat(ld_processes.path[i], proc);
	}
	
	//Thêm cái này
    for (int k = 0; k < num_processes - 1; k++) {
        for (int j = k + 1; j < num_processes; j++) {
            if (ld_processes.start_time[k] > ld_processes.start_time[j]) {
                // 1. Swap Start Time
                unsigned long temp_time = ld_processes.start_time[k];
                ld_processes.start_time[k] = ld_processes.start_time[j];
                ld_processes.start_time[j] = temp_time;
                // 2. Swap Path (Con trỏ chuỗi)
                char * temp_path = ld_processes.path[k];
                ld_processes.path[k] = ld_processes.path[j];
                ld_processes.path[j] = temp_path;
#ifdef MLQ_SCHED
                // 3. Swap Priority (Nếu dùng chế độ MLQ)
                unsigned long temp_prio = ld_processes.prio[k];
                ld_processes.prio[k] = ld_processes.prio[j];
                ld_processes.prio[j] = temp_prio;
#endif
            }
        }
    }
	//



}

int main(int argc, char * argv[]) {
	/* Read config */
	if (argc != 2) {
		printf("Usage: os [path to configure file]\n");
		return 1;
	}
	char path[100];
	path[0] = '\0';
	strcat(path, "input/");
	strcat(path, argv[1]);
	read_config(path);

	pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
	struct cpu_args * args =
		(struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
	pthread_t ld;
	
	/* Init timer */
	int i;
	for (i = 0; i < num_cpus; i++) {
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t * ld_event = attach_event();
	start_timer();

#ifdef MM_PAGING
    /* 1. Khởi tạo RAM */
    int rdmflag = 1;
    struct memphy_struct *mram = malloc(sizeof(struct memphy_struct));
    init_memphy(mram, memramsz, rdmflag);

    /* 2. Khởi tạo mảng các thiết bị SWAP */
    // Thay vì dùng mảng tĩnh, ta dùng mảng các con trỏ để dễ quản lý trong krnl_t
    struct memphy_struct **mswp = malloc(PAGING_MAX_MMSWP * sizeof(struct memphy_struct *));
    
    int sit;
    for(sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
        if (memswpsz[sit] > 0) {
            mswp[sit] = malloc(sizeof(struct memphy_struct));
            init_memphy(mswp[sit], memswpsz[sit], rdmflag);
        } else {
            mswp[sit] = NULL; // Đánh dấu vùng swap không sử dụng
        }
    }

    /* 3. Khởi tạo cấu trúc đối số cho Loader */
    struct mmpaging_ld_args *mm_ld_args = malloc(sizeof(struct mmpaging_ld_args));
    
    // Khởi tạo Kernel MM nếu chưa có
    if (os.mm == NULL) {
        os.mm = malloc(sizeof(struct mm_struct));
        init_mm(os.mm, NULL);
    }

    // Gán các tài nguyên vào Kernel hệ thống (os)
    os.mram = mram;
    os.mswp = mswp;
    os.active_mswp_id = 0; // Bắt đầu Round Robin từ Swap 0

    // Truyền tham số cho loader thread
    mm_ld_args->timer_id = ld_event;
    mm_ld_args->mram = mram;
    mm_ld_args->mswp = mswp;
    mm_ld_args->active_mswp_id = 0;
#endif

	/* Init scheduler */
	init_scheduler();

	/* Run CPU and loader */
#ifdef MM_PAGING
	pthread_create(&ld, NULL, ld_routine, (void*)mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif
	for (i = 0; i < num_cpus; i++) {
		pthread_create(&cpu[i], NULL,
			cpu_routine, (void*)&args[i]);
	}

	/* Wait for CPU and loader finishing */
	for (i = 0; i < num_cpus; i++) {
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

#ifdef MM_PAGING
    free(os.mram);
    for(int i = 0; i < PAGING_MAX_MMSWP; i++) {
        if (os.mswp[i] != NULL) free(os.mswp[i]);
    }
    free(os.mswp);
    free(mm_ld_args);
#endif

	/* Stop timer */
	stop_timer();

	return 0;

}