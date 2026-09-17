#ifndef MEMCHECK_H
#define MEMCHECK_H
#include <stdlib.h>
void *my_malloc(size_t size);
void my_free(void *ptr);
#endif
