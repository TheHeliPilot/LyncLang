#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int add_int_int_int(int a, int b);
int mul_int_int_int(int a, int b);
int apply_int_fn_int_int(int (*f)(int, int), int x, int y);
int add_int_int_int(int a, int b)
{
  return (a + b);
}
int mul_int_int_int(int a, int b)
{
  return (a * b);
}
int apply_int_fn_int_int(int (*f)(int, int), int x, int y)
{
  return f(x, y);
}
int main()
{
  printf("%s\n", "=== fn-ptr demo ===");
  int (*op)(int, int) = add_int_int_int;
  printf("%s %d\n", "apply(add, 3, 4) =", apply_int_fn_int_int(add_int_int_int, 3, 4));
  printf("%s %d\n", "apply(mul, 3, 4) =", apply_int_fn_int_int(mul_int_int_int, 3, 4));
  return 0;
}
