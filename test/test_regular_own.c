#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int main()
{
  int64_t *x = malloc(sizeof(int64_t));
  *x = 5;
  free(x);
  return 0;
}
