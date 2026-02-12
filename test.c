#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

int64_t unreachable_int_();
int main()
{
  int64_t a;
  a = 20;
  bool b;
  b = true;
  {
    printf("%s\n", "PASS: DCE works on else");
  }
  for (int i = 0; i <= 5; i++)   {
    printf("%s\n", "Looping...");
  }
  int64_t val;
  if (10 == 10) {
    val = 1;
  }
  else {
    val = 0;
  }
  int64_t *p = malloc(sizeof(int64_t));
  *p = 100;
  printf("%s\n", "Value exists");
  free(p);
  return 0;
}
int64_t unreachable_int_()
{
  return 5;
  printf("%s\n", "FAIL: Code after return was not eliminated");
}
