#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int main()
{
  int64_t arr1[3] = {10, 20, 30};
  arr1[1] = 99;
  printf("%s %d\n", "modified:", arr1[1]);
  return 0;
}
