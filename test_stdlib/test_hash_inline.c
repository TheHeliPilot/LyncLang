#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int hash_int_int_int(int k);
int hash_str_int_str(char* s);
int hash_int_int_int(int k)
{
  int h;
  h = k;
  if ((h < 0))   {
    h = (0 - h);
  }

  h = (h * -1640531535);
  if ((h < 0))   {
    h = (0 - h);
  }

  return h;
}
int hash_str_int_str(char* s)
{
  int h;
  h = 5381;
  int i;
  i = 0;
  while ((s[i] != 0))   {
    h = ((h * 33) + s[i]);
    i = (i + 1);
  }
  if ((h < 0))   {
    h = (0 - h);
  }

  return h;
}
int main()
{
  (printf("%s %d\n", "hash_int(0):", hash_int_int_int(0)), fflush(stdout));
  (printf("%s %d\n", "hash_int(1):", hash_int_int_int(1)), fflush(stdout));
  (printf("%s %d\n", "hash_int(42):", hash_int_int_int(42)), fflush(stdout));
  (printf("%s %d\n", "hash_str(hello):", hash_str_int_str("hello")), fflush(stdout));
  (printf("%s %d\n", "hash_str(world):", hash_str_int_str("world")), fflush(stdout));
  int h1;
  h1 = hash_str_int_str("foo");
  int h2;
  h2 = hash_str_int_str("bar");
  (printf("%s %s\n", "foo!=bar:", ((h1 != h2) ? "true" : "false")), fflush(stdout));
  return 0;
}
