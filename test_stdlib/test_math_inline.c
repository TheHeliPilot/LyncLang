#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

float pi_float();
int min_int_int_int_int(int a, int b);
int max_int_int_int_int(int a, int b);
int clamp_int_int_int_int_int(int v, int lo, int hi);
int abs_int_int_int(int v);
float pi_float()
{
  return 3.14159;
}
int min_int_int_int_int(int a, int b)
{
  if ((a < b))   {
    return a;
  }

  return b;
}
int max_int_int_int_int(int a, int b)
{
  if ((a > b))   {
    return a;
  }

  return b;
}
int clamp_int_int_int_int_int(int v, int lo, int hi)
{
  if ((v < lo))   {
    return lo;
  }

  if ((v > hi))   {
    return hi;
  }

  return v;
}
int abs_int_int_int(int v)
{
  if ((v < 0))   {
    return (0 - v);
  }

  return v;
}
int main()
{
  float p;
  p = pi_float();
  (printf("%s %g\n", "pi:", p), fflush(stdout));
  (printf("%s %d\n", "min(3,7):", min_int_int_int_int(3, 7)), fflush(stdout));
  (printf("%s %d\n", "max(3,7):", max_int_int_int_int(3, 7)), fflush(stdout));
  (printf("%s %d\n", "clamp(10,0,5):", clamp_int_int_int_int_int(10, 0, 5)), fflush(stdout));
  (printf("%s %d\n", "abs(-4):", abs_int_int_int((0 - 4))), fflush(stdout));
  return 0;
}
