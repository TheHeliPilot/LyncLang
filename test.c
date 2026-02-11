#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

int64_t doubleF(int64_t* n);
int64_t grade(int64_t score);
int main()
{
  int64_t *x = malloc(sizeof(int64_t));
  *x = 5;
  int64_t r;
  if (*x == 1) {
    r = 10;
  }
  else if (*x == 5) {
    r = 50;
  }
  else {
    r = 0;
  }
  int64_t y;
  if (r == 50) {
    if (*x == 5) {
      y = 100;
    }
    else {
      y = 0;
    }
  }
  else {
    y = 1;
  }
  bool b;
  if ((*x > 3) == true) {
    b = true;
  }
  else if ((*x > 3) == false) {
    b = false;
  }
  else {
    b = false;
  }
if (true) {
  *x = (*x + 2);
}
else if (false) {
  *x = (*x - 2);
}
  free(x);
  return y;
;
}
int64_t doubleF(int64_t* n)
{
  {
  int64_t _ret;
  if (*n == 0) {
    _ret = 0;
  }
  else if (*n == 1) {
    _ret = 2;
  }
  else {
    _ret = (*n + *n);
  }
  return _ret;
}
;
  free(n);
}
int64_t grade(int64_t score)
{
  int64_t result;
  if (score == 100) {
    result = 5;
  }
  else if (score == 90) {
    result = 4;
  }
  else if (score == 80) {
    result = 3;
  }
  else {
    result = 0;
  }
  return result;
;
}
