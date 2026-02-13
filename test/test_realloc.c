#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int main()
{
  int64_t *i = malloc(sizeof(int64_t));
  *i = 2;
  free(i);
  i = malloc(sizeof(int64_t));
  *i = 5;
  printf("%s %d\n", "i after realloc:", *i);
  free(i);
  return 0;
}
