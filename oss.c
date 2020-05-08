#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <stdio.h>
#include "oss.h"

static int shid, semid;

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short  *array;
};

static struct shared* oss_initialize(const int flags){
  const char *key_file = "master.c";
  const int proj_id = 5678;

	const key_t shmkey = ftok(key_file, proj_id);
  const key_t semkey = ftok(key_file, proj_id+1);
	if( (shmkey == -1) || (semkey == -1)){
		perror("ftok");
		return NULL;
	}

	shid = shmget(shmkey, sizeof(struct shared), flags);
	if (shid == -1) {
		perror("shmget");
		return NULL;
	}

	struct shared* oss = (struct shared*)shmat(shid, (void *)0, 0);
	if (oss == (void *)-1) {
		perror("shmat");
		return NULL;
	}

	semid = semget(semkey, NPROCS + 1, flags);
	if(semid == -1){
		perror("semget");
		return NULL;
	}

  if(flags > 0){  //if we are the master
    int i;
  	union semun un;
  	for(i=0; i < NPROCS + 1; i++){
  		un.val = 1;
  		if(semctl(semid, i, SETVAL, un) == -1){
  			perror("semctl");
  			return NULL;
  		}
  	}
  }

  return oss;
}

struct shared* master_initialize(){
  return oss_initialize(IPC_CREAT | IPC_EXCL | S_IRWXU);
}

struct shared* user_initialize(){
  return oss_initialize(0);
}

void master_deinitialize(struct shared * oss){

  shmdt(oss);
  shmctl(shid, IPC_RMID, NULL);
  semctl(semid, 0, IPC_RMID);
}

void user_deinitialize(struct shared* oss){
  shmdt(oss);
}

int oss_lock(const int id){

  struct sembuf opLock;
  opLock.sem_num = id;
  opLock.sem_flg = 0;
	opLock.sem_op  = -1;

  if (semop(semid, &opLock, 1) == -1) {
	   perror("semop");
	   return -1;
	}

  return 0;
}

int oss_unlock(const int id){

  struct sembuf opUnlock;
  opUnlock.sem_num = id;
  opUnlock.sem_flg = 0;
  opUnlock.sem_op  = 1;

  if (semop(semid, &opUnlock, 1) == -1) {
     perror("semop");
     return -1;
  }

  return 0;
}
