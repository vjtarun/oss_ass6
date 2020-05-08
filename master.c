#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "oss.h"

//returns index of page, from address
#define PIDX(addr) addr / PAGE_SIZE

//number of processes started and terminated
static unsigned int nstarted = 0, nterminated = 0;
//reference statistics
static unsigned int nreafs = 0, rd_refs = 0, wr_refs = 0, fault_refs = 0, nlines = 0;

static struct shared *oss = NULL;   /* shared memory region pointer */

static int interrupt_signal = 0;
static int option_m = 0;  //for child -m option - 0 or 1
static FILE *flog = NULL;

static void incr_clock(struct time* t, const struct time* t2){

  t->seconds     += t2->seconds;
	t->nanoseconds += t2->nanoseconds;

  if(t->nanoseconds > 1000000000){
		t->seconds     += 1;
		t->nanoseconds -= 1000000000;
	}
}

static int check_clock(const struct time *t, const struct time *t2){
  if(	t2->seconds  < t->seconds){
    return 1; //t2 is before t ( less )
  }else if((t2->seconds == t->seconds) && (t2->nanoseconds <= t->nanoseconds)){
    return 1; //t2 is before t ( less )
  }else{
    return 0; //times are equal
  }
}

static void load_frame(int page_index, int proc_index, int frame_index){
  bitmap_set(&oss->ftbl.bitmap, frame_index);
  oss->ftbl.frames[frame_index].page = page_index;
  oss->ftbl.frames[frame_index].proc = proc_index;
}

static void unload_frame(const int frame){
	struct frame *fr = &oss->ftbl.frames[frame];
  fr->is_dirty = 0;
  fr->page = -1;
  fr->proc = -1;

	bitmap_unset(&oss->ftbl.bitmap, frame);
}

static void unload_page(struct page * p){
  if(p->frame >= 0){
    unload_frame(p->frame);
  }
  p->frame = -1;
  p->is_referenced = 0;
}

static void clear_proc_pages(struct page *pt){
  int i;
	for(i=0; i < NPAGES; i++)
    unload_page(&pt[i]);
}

static void clear_proc(struct process_table * ptbl, struct user * proc, const unsigned int n){

	bitmap_unset(&ptbl->bitmap, n);

  bzero(proc, sizeof(struct user));
  proc->state = DEAD_STATE;
}

static int start_procs(void){

  if(nstarted >= GPROCS)
    return 0;

  const int proc_index = bitmap_find_unset(&oss->ptbl.bitmap);
  if(proc_index == -1)
    return 0;

  struct user *proc = &oss->ptbl.procs[proc_index];
  proc->id		= 1 + nstarted;
  proc->state = READY_STATE;

	bitmap_set(&oss->ptbl.bitmap, proc_index);
  nstarted++;

  pid_t pid = fork();
  if(pid == -1){
    perror("fork");
    return -1;

  }else if(pid == 0){

    char argv[2][10];
    snprintf(argv[1], 10, "%i", proc_index);
		snprintf(argv[2], 10, "%i", option_m);

    execl("./user", "./user", argv[1], argv[2], NULL);

    perror("execl");
    exit(EXIT_FAILURE);

  }else{
    proc->pid = pid;
    fprintf(flog, "[%i:%i] Master: Generating process with PID %u\n", oss->clock.seconds, oss->clock.nanoseconds, proc->id);
    nlines++;
  }

  return pid;
}

static void end_procs(){

  int i;
  for(i=0; i < NPROCS; i++){

    struct user * proc = &oss->ptbl.procs[i];
    if(proc->pid <= 0){
      continue;
    }

    oss_lock( i);
    proc->state = TERMINATION_STATE;
    oss->refs[i].res = REF_ILLEGAL;
    oss_unlock( i);
  }

  for(i=0; i < NPROCS; i++){
    struct user * proc = &oss->ptbl.procs[i];
    if(proc->pid <= 0){
      waitpid(proc->pid, NULL, 0);
    }
  }
}

static void sim_stats(){
  //show results
  flog = freopen("master.log", "a", flog);

  fprintf(flog, "Simulation time: %u:%u\n", oss->clock.seconds, oss->clock.nanoseconds);

  fprintf(flog, "Processes started: %u\n", nstarted);
  fprintf(flog, "Processes terminated: %u\n", nterminated);

  fprintf(flog, "1.Memory refs (total, read, write): %u, %u, %u\n", nreafs, rd_refs, wr_refs);
  fprintf(flog, "1.Memory faults: %u\n", fault_refs);
  fprintf(flog, "2.Memory speed: %.2f refs/s\n", (float) nreafs / oss->clock.seconds);
  fprintf(flog, "3.Memory fault %%: %.2f\n", ((float)fault_refs / nreafs)* 100);
}

static void simulation_deinitialize(){

	bitmap_deinit(&oss->ptbl.bitmap, NPROCS);
	bitmap_deinit(&oss->ftbl.bitmap, NFRAMES);

  master_deinitialize(oss);
}

static void frame_listing(){

	fprintf(flog, "[%i:%i] Master: Current frame layout:\n", oss->clock.seconds, oss->clock.nanoseconds);
  fprintf(flog, "\t\t\t\tOccupied\tRefByte\tDirtyBit\n");
  nlines += 2;

	int i;
	for(i=0; i < NFRAMES; i++){

    struct frame * fr = &oss->ftbl.frames[i];

    const int ref_bit = (fr->page >= 0) ? oss->ptbl.procs[fr->proc].pages[fr->page].is_referenced : 0;
		const char * used = (fr->page >= 0) ? "Yes" : "No";

    fprintf(flog, "Frame %2d\t\t%s\t\t%7d\t%8d\n", i, used, ref_bit, oss->ftbl.frames[i].is_dirty);
    nlines++;
	}
}

static int page_replacement(struct frame * frame_list){
  static int clock_hand = -1;
  int replaced = 0;

  while(!replaced){

    clock_hand = (clock_hand + 1) % NFRAMES;

		if(frame_list[clock_hand].page < 0){
			continue;
		}

	struct frame * frame = &frame_list[clock_hand];
    struct page * page = &oss->ptbl.procs[frame->proc].pages[frame->page];

    if(page->is_referenced == 0){
      replaced = clock_hand;

      fprintf(flog, "[%i:%i] Master: Evicted page %d of P%d\n",
      oss->clock.seconds, oss->clock.nanoseconds, frame->page, frame->proc);
      nlines++;
		}else{
			page->is_referenced = 0;
    }
  }

  return replaced;
}

static int page_fault(struct reference * ref, const int id){

  fault_refs++;
  struct user * proc = &oss->ptbl.procs[id];
  const int page_index = PIDX(ref->addr);


  int frame_index = bitmap_find_unset(&oss->ftbl.bitmap);
  if(frame_index < 0){

	  frame_index = page_replacement(oss->ftbl.frames);

	  struct frame * frame = &oss->ftbl.frames[frame_index];
	  struct page * page = &oss->ptbl.procs[frame->proc].pages[frame->page];

	  struct user * proc = &oss->ptbl.procs[frame->proc];


    if(frame->is_dirty){
      nlines++;
      fprintf(flog, "[%i:%i] Master: Dirty bit of frame %d set, adding additional time to the clock\n",
        oss->clock.seconds, oss->clock.nanoseconds, page->frame);

      //saving a dirty frame 
      struct time d_time;
      d_time.seconds = 0;
      d_time.nanoseconds = 14* 1000;
      incr_clock(&oss->clock, &d_time);
    }

	  fprintf(flog, "[%i:%i] Master: Swapped P%d page %d for P%d page %d in frame %d\n",
	    oss->clock.seconds, oss->clock.nanoseconds, proc->id, frame->page, proc->id, page_index, page->frame);

	  frame_index = page->frame;
    unload_page(page);

	}else{
    fprintf(flog, "[%i:%i] Master: Using free frame %d for P%d page %d\n",
      oss->clock.seconds, oss->clock.nanoseconds, frame_index, proc->id, page_index);
  }
  nlines++;

  return frame_index;
}

static int ref_loader(struct reference * ref, const int id){

  int res = REF_ILLEGAL;

	struct user * proc = &oss->ptbl.procs[id];

  const int page_index = PIDX(ref->addr);
  if(page_index >= NPAGES){

		fprintf(flog, "[%i:%i] Master: P%d trying to load invalid addr %d\n",
			oss->clock.seconds, oss->clock.nanoseconds, proc->id, ref->addr);
    nlines++;
		return REF_ILLEGAL;
	}

  struct page * page = &proc->pages[page_index];
  if(page->frame < 0){
  	nlines++;
    fprintf(flog, "[%i:%i] Master: Page fault for address %d\n",
      oss->clock.seconds, oss->clock.nanoseconds, ref->addr);

  	page->frame = page_fault(ref, id); //get frame for page, so we can load it into memory
		if(page->frame < 0){
			return REF_ILLEGAL;
		}
  	page->is_referenced = 1;  //after page is loaded into memory, raise the ref bit

    load_frame(page_index, id, page->frame);

    //load takes 14 ms
    ref->swap_time.seconds = 0; ref->swap_time.nanoseconds = 14 * 1000;
    incr_clock(&ref->swap_time, &oss->clock);

		res = REF_SWAP;

  }else{

		//access takes 10ns
		struct time a_time;
		a_time.seconds = 0;
    a_time.nanoseconds = 10;
    incr_clock(&oss->clock, &a_time);

    res = REF_OK;
  }

  return res;
}

static int mem_reference(struct reference * ref, const int id){
  int res = REF_ILLEGAL;

  nreafs++;

  struct user * proc = &oss->ptbl.procs[id];

  if(ref->type == ADDR_RD){

    rd_refs++;
    fprintf(flog, "[%i:%i] Master: P%d wants to read from address %d\n",
      oss->clock.seconds, oss->clock.nanoseconds, proc->id, ref->addr);

  }else{

    wr_refs++;
    fprintf(flog, "[%i:%i] Master: P%d wants to write to address %d\n",
      oss->clock.seconds, oss->clock.nanoseconds, proc->id, ref->addr);
  }
  nlines++;

	res = ref_loader(ref, id);
  if(res == REF_OK){
    const int page_index = PIDX(ref->addr);
    struct page * page = &proc->pages[page_index];

    if(ref->type == ADDR_RD){
      nlines++;
  		fprintf(flog, "[%i:%i] Master: Address %d in frame %d, P%d reading\n", oss->clock.seconds, oss->clock.nanoseconds,
  			ref->addr, page->frame, proc->id);

    }else{

      nlines++;
  		fprintf(flog, "[%i:%i] Master: Address %d in frame %d, P%d writing\n",
  			oss->clock.seconds, oss->clock.nanoseconds, ref->addr, page->frame, proc->id);
    }
  }

  return res;
}


static void process_reference(enum ref_result type){
	int i;
  struct time t;

  t.seconds = 0;

	for(i=0; i < NPROCS; i++){	//for each proc

		struct user * proc = &oss->ptbl.procs[i];

    //if process is not running
		if(proc->pid <= 0)
			continue;  //move to next

		oss_lock(i);

		if(proc->state == TERMINATION_STATE){
      nterminated++;

    	fprintf(flog,"Master received P%d termination request at time %i:%i\n",
            proc->id, oss->clock.seconds, oss->clock.nanoseconds);
      nlines++;

      //clear pages and process
			clear_proc_pages(proc->pages);
			clear_proc(&oss->ptbl, proc, i);
      //
			//clear_proc_pages(proc->pages);

		}else if(oss->refs[i].res == type){	//if we have a request from our "queue"

    	struct reference * ref = &oss->refs[i];
    	ref->res = mem_reference(ref, i);
		}
		oss_unlock(i);

    //add request processing to clock
    t.nanoseconds = rand() % 100;
    incr_clock(&oss->clock, &t);
	}
}

static int simulation_initialize(){

	oss = master_initialize();
  if(oss == NULL){
    return -1;
  }

  flog = fopen("master.log", "w");
  if(flog == NULL){
    perror("fopen");
    return EXIT_FAILURE;
  }

  srand(getpid());

  bzero(oss,   sizeof(struct shared));

	if(	(bitmap_init(&oss->ptbl.bitmap, NPROCS) < 0) ||
			(bitmap_init(&oss->ftbl.bitmap, NFRAMES) < 0) ){
		return -1;
	}

  //initialize frames
  int i;
	for(i=0; i < NFRAMES; i++){
		unload_frame(i);
	}

	//initialize page table of each process
	for(i=0; i < NPROCS; i++){	//for each proc
		struct user * proc = &oss->ptbl.procs[i];
		clear_proc_pages(proc->pages);
	}

  return 0;
}

static void signal_handler(const int sig){
  nlines++;
  fprintf(flog, "[%i:%i] Master: Signaled with %d\n", oss->clock.seconds, oss->clock.nanoseconds, sig);
  interrupt_signal = 1;
}

int main(const int argc, char * argv[]){

	if(argc == 3){
    if(strcmp(argv[1], "-m") == 0){
      option_m = atoi(argv[2]);
    }
  }

  if( (signal(SIGTERM, signal_handler) == SIG_ERR) ||
      (signal(SIGALRM, signal_handler) == SIG_ERR) ||

      (signal(SIGCHLD, SIG_IGN)     == SIG_ERR)){

      perror("signal");
      return EXIT_FAILURE;
  }

  if(simulation_initialize() < 0){
    return EXIT_FAILURE;
  }

  alarm(2);

  struct time next_listing = oss->clock;
  struct time next_start = oss->clock;

  while((nterminated < GPROCS) && !interrupt_signal){

    //advance time with random nanoseconds
    struct time t;
		t.seconds = 1;
		t.nanoseconds = rand() % 1000;
		incr_clock(&oss->clock, &t);

		/* check if we need to fork */
	  if(check_clock(&oss->clock, &next_start)){

	    /* calculate next time we start process */
	    next_start.seconds = 1;
	    next_start.nanoseconds = rand() % 500;
	    incr_clock(&next_start, &oss->clock);

      if(start_procs() < 0){
        fprintf(stderr, "start_procs failed\n");
        break;
      }
    }

    process_reference(REF_SWAP);
    process_reference(REF_TODO);

    if(check_clock(&oss->clock, &next_listing)){
			frame_listing();
      next_listing.seconds = oss->clock.seconds + 10;
    }

  	if(nlines >= 100000){
  		fprintf(flog,"Master: Log exceeding 100000 lines ...\n");
  		flog = freopen("/dev/null", "w", flog);
  	}
  }

  end_procs();
  sim_stats();
  simulation_deinitialize();
  fclose(flog);
  return 0;
}
