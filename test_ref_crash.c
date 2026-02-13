#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int64_t add_int_();
int main()
{
  bool *x = malloc(sizeof(bool));
  *x = true;
  bool *r;
  r = &*x;
  printf("%s %s\n", "Bool je: ", (*r ? "true" : "false"));
  free(x);
  printf("%d\n", add_int());
  return 0;
}
int64_t add_int_()
{
  for (int i = 0; i <= 25; i++)   {
    if ((i > 0))     {
      return i;
    }

    return 420;
  }
}
