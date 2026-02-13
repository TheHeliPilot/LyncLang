#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int main()
{
  int64_t* arr = malloc(sizeof(int64_t) * 2);
  arr[0] = 10;
  arr[1] = 20;
  printf("%s %d %d\n", "first alloc:", arr[0], arr[1]);
  free(arr);
  arr = malloc(sizeof(int64_t) * 2);
  arr[0] = 100;
  arr[1] = 200;
  printf("%s %d %d\n", "after realloc:", arr[0], arr[1]);
  free(arr);
  return 0;
}
