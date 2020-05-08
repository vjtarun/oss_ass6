#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>

#include "oss.h"

#define LAST_ADDRESS (NPAGES * PAGE_SIZE)

static int id = -1;
struct shared *oss = NULL;

static int addr_request(enum ref_type op, const int addr){
	if(oss_lock(id) < 0){
		return -1;
	}

	oss->refs[id].addr  = addr;
	oss->refs[id].type		= op;
	oss->refs[id].res   = REF_TODO;

	if(oss_unlock(id) < 0){
		return -1;
	}
	return 0;
}

static enum ref_result addr_wait(){

	enum ref_result res = REF_ILLEGAL;
	int i;

	for(i=0; i < 10; i++){	//wait 10 cycle for OSS to return result

		if(oss_lock(id) < 0){
			return REF_ILLEGAL;
		}

		res = oss->refs[id].res;
		if(res != REF_TODO){	//if we have a result
			i = 10;
		}

		if(oss_unlock(id) < 0){
			return REF_ILLEGAL;
		}

		usleep(10);
	}
	return res;
}

static enum ref_result addr_ref(enum ref_type type, const int addr){

	addr_request(type, addr);
	return addr_wait();
}

static int weighted_address(const float * weights){
	int i = 0;
	int addr = 0;

	const float x = ((float)rand() / (float)RAND_MAX) * weights[NPAGES-1];

	//update weights
  for(i=0; i < NPAGES; ++i){
		 if(x <= weights[i]){
       addr = (i*PAGE_SIZE) + (rand() % PAGE_SIZE);
			 break;
     }
  }

	return addr;
}

static void update_weights(float * weights){
	int i;
	for(i=0; i < NPAGES; ++i){
     weights[i] += weights[i - 1];
	 }
}

static void init_weights(float * weights){
	int i;
	for(i=0; i < NPAGES; i++){
		 weights[i] =  1.0 / (float)(i + 1);
	}
}

int main(const int argc, char * const argv[]){

	float weights[NPAGES];

	id = atoi(argv[1]);
	const int m = atoi(argv[2]);

	oss = user_initialize();
	if(oss == NULL){
		return EXIT_FAILURE;
	}
	struct user *proc = &oss->ptbl.procs[id];

	srand(getpid());
	if(m == 1)
		init_weights(weights);


	int alive = 1;
	int next_check = 900 + (rand() % 200);	//1000 +-100

	while(alive){

		//check if we should terminated
		if(--next_check <= 0){

			next_check = 900 + (rand() % 200);	//1000 +-100

			if(oss_lock( 0) == -1)
				break;

			alive = (proc->state == TERMINATION_STATE) ? 0 : 1;
			if(oss_unlock(0) == -1)
				break;

			if(!alive)
				break;
		}

		//since user process will terminate only on interrupt or alarm,
		//and we want to generate 100 processes, we add a "last refence" change
		if((rand() % 100) < 5){	//5% chance to be last reference
			alive = 0;
		}

		int addr = 0;
		if(m == 0){
			addr = rand() % LAST_ADDRESS;
		}else{
			addr = weighted_address(weights);
			update_weights(weights);
		}

		// 77 % chance to read, than to write
		enum ref_type type = ((rand() % 100) < 77) ? ADDR_RD : ADDR_WR;
		if(addr_ref(type, addr) < 0){
			break;
		}
	}

	oss_lock(id);
	proc->state = TERMINATION_STATE;
	oss_unlock(id);

	user_deinitialize(oss);
  return EXIT_SUCCESS;
}
