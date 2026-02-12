#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

int main()
{
  int64_t *maybe_ptr = malloc(sizeof(int64_t));
  *maybe_ptr = 42;
  int64_t i;
  i = 5;
  if (maybe_ptr != NULL) {
    int64_t* f = maybe_ptr;
    i = *f;
  }
  else if (maybe_ptr == NULL) {
  }
  if (maybe_ptr != NULL) {
    int64_t* f = maybe_ptr;
    i = *f;
  }
  else if (maybe_ptr == NULL) {
    i = 0;
  }
  free(maybe_ptr);
  return 0;
}
