#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

int _str_len_internal_int_str(char* s);
int str_len_int_str(char* s);
bool str_eq_bool_str_str(char* a, char* b);
bool str_starts_with_bool_str_str(char* s, char* prefix);
int _str_len_internal_int_str(char* s)
{
  int n;
  n = 0;
  while ((s[n] != 0))   {
    n = (n + 1);
  }
  return n;
}
int str_len_int_str(char* s)
{
  return _str_len_internal_int_str(s);
}
bool str_eq_bool_str_str(char* a, char* b)
{
  int la;
  la = _str_len_internal_int_str(a);
  int lb;
  lb = _str_len_internal_int_str(b);
  if ((la != lb))   {
    return false;
  }

  int i;
  i = 0;
  while ((i < la))   {
    if ((a[i] != b[i]))     {
      return false;
    }

    i = (i + 1);
  }
  return true;
}
bool str_starts_with_bool_str_str(char* s, char* prefix)
{
  int ls;
  ls = _str_len_internal_int_str(s);
  int lp;
  lp = _str_len_internal_int_str(prefix);
  if ((lp > ls))   {
    return false;
  }

  int i;
  i = 0;
  while ((i < lp))   {
    if ((s[i] != prefix[i]))     {
      return false;
    }

    i = (i + 1);
  }
  return true;
}
int main()
{
  (printf("%s %d\n", "str_len(hello):", str_len_int_str("hello")), fflush(stdout));
  (printf("%s %s\n", "str_eq(hello,hello):", (str_eq_bool_str_str("hello", "hello") ? "true" : "false")), fflush(stdout));
  (printf("%s %s\n", "str_eq(hello,world):", (str_eq_bool_str_str("hello", "world") ? "true" : "false")), fflush(stdout));
  (printf("%s %s\n", "str_eq(hi,hello):", (str_eq_bool_str_str("hi", "hello") ? "true" : "false")), fflush(stdout));
  (printf("%s %s\n", "starts_with(hello world,hello):", (str_starts_with_bool_str_str("hello world", "hello") ? "true" : "false")), fflush(stdout));
  (printf("%s %s\n", "starts_with(hello world,world):", (str_starts_with_bool_str_str("hello world", "world") ? "true" : "false")), fflush(stdout));
  return 0;
}
