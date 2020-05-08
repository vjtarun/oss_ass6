#ifndef OSS_H
#define OSS_H

#include "bitmap.h"

struct time {
  int seconds;
  int nanoseconds;
};

#define PAGE_SIZE	1024
#define NPAGES	32
#define NFRAMES 256

enum ref_type	{ ADDR_RD=0, ADDR_WR,	ADDR_NOP };
enum ref_result { REF_OK=0, REF_SWAP, REF_TODO, REF_ILLEGAL};

struct reference{
	int addr;
	enum ref_type 	type;
  struct time swap_time;	//time when request will be loaded, if blocked
	enum ref_result res;
};

struct page {
	int frame;
	unsigned int is_referenced : 1;
};

struct frame {
	int page;  //page index
	int proc;  //process owning the table

  unsigned int is_dirty : 1;
};

// processes running at the same time
#define NPROCS 20
// total proccesses started
#define GPROCS 100

enum state 			{ DEAD_STATE=0, READY_STATE, IO_STATE, TERMINATION_STATE};

struct user {
	int	pid;
	int id;
	enum state state;
	struct page	  pages[NPAGES];
};

struct process_table {
	struct user procs[NPROCS];
	struct bitmap bitmap;  //shows which processes are free
};

struct frame_table{
	struct frame  frames[NFRAMES];
	struct bitmap bitmap;  //shows which frames are free
};

struct shared {
  //our page table
	struct process_table ptbl;
  //our frame table
	struct frame_table ftbl;

  //references processess make
	struct reference refs[NPROCS];

  //virtual clock
  struct time 	clock;
};

struct shared* master_initialize();
struct shared* user_initialize();

void master_deinitialize(struct shared *);
void user_deinitialize(struct shared*);

int oss_lock(const int id);
int oss_unlock(const int id);

#endif
