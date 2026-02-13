#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int main()
{
  int64_t* arr3 = malloc(sizeof(int64_t) * 2);
  free(arr3);
  return 0;
}
