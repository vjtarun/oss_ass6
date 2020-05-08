struct bitmap{
  char *bits;
  int size;
};

int bitmap_init(struct bitmap * b, const int size);
void bitmap_deinit(struct bitmap * b, const int size);

void bitmap_set(struct bitmap * b, const int n);
int bitmap_check(struct bitmap * b, const int n);
void bitmap_unset(struct bitmap * b, const int n);

int bitmap_find_unset(struct bitmap * b);
