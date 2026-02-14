#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int main()
{
  printf("%s\n", "Testing Floats and Doubles...");
  float f;
  f = 3.14f;
  printf("%s %g\n", "f (float) = ", f);
  double d;
  d = 123.456;
  printf("%s %g\n", "d (double) = ", d);
  float res_float;
  res_float = (1 + f);
  printf("%s %g\n", "1 + f = ", res_float);
  double res_double;
  res_double = (f + d);
  printf("%s %g\n", "f + d = ", res_double);
  printf("%s %g\n", "sqrt(16.0) = ", sqrt(16));
  return 0;
}
