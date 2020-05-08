#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "bitmap.h"

int bitmap_init(struct bitmap * b, const int size){

  b->size = size; //size is the number of bits available, not bits[] size
  b->bits = (char*) malloc((size / 8) + 1);
  if(b->bits == NULL){
    perror("malloc");
    return -1;
  }
  bzero(b->bits, (size / 8) + 1);
  return 0;
}

void bitmap_deinit(struct bitmap * b, const int size){
  free(b->bits);
}

void bitmap_set(struct bitmap * b, const int n){
  int byte = n / 8;
  int bit = n % 8;

  b->bits[byte] |= 1 << bit;
}

int bitmap_check(struct bitmap * b, const int n){
  int byte = n / 8;
  int bit = n % 8;

  return ((b->bits[byte] & (1 << bit)) >> bit);
}

void bitmap_unset(struct bitmap * b, const int n){
  int byte = n / 8;
  int bit = n % 8;
  b->bits[byte] &= ~(1 << bit);
}

int bitmap_find_unset(struct bitmap * b){
  int i;
  for(i=0; i < b->size; i++){
		if(	bitmap_check(b, i) == 0)
      return i;
  }
  return -1;
}
