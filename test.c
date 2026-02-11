#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

int64_t foo();
int main()
{
  int64_t *ptr = malloc(sizeof(int64_t));
  *ptr = 42;
  free(ptr);
  return 0;
;
}
int64_t foo()
{
  int64_t *x = malloc(sizeof(int64_t));
  *x = 100;
  free(x);
  return 0;
;
}
