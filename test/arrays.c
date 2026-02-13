#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int main()
{
  int64_t arr1[3] = {10, 20, 30};
  printf("%s %d\n", "arr1[0]:", arr1[0]);
  printf("%s %d\n", "arr1[1]:", arr1[1]);
  printf("%s %d\n", "arr1[2]:", arr1[2]);
  arr1[1] = 99;
  printf("%s %d\n", "arr1[1] after:", arr1[1]);
  int64_t* arr3 = malloc(sizeof(int64_t) * 2);
  arr3[0] = 5;
  arr3[1] = 10;
  printf("%s %d\n", "arr3[0]:", arr3[0]);
  free(arr3);
  int64_t len;
  len = 3;
  printf("%s %d\n", "arr1 length:", len);
  return 0;
}
