#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// std.io helper functions
int64_t* read_int() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    int64_t* result = malloc(sizeof(int64_t));
    *result = atoll(buffer);
    return result;
}

int64_t test_basic_types_int_();
int64_t test_operators_int_();
int64_t test_control_flow_int_();
int64_t add_int_int_int(int64_t a, int64_t b);
int64_t calculate_int_int_int(int64_t x, int64_t y);
int64_t test_functions_int_();
int64_t test_match_values_int_();
int64_t test_ownership_int_();
int64_t consume_int_intown(int64_t* p);
int64_t test_move_semantics_int_();
int64_t test_nullable_types_int_();
int64_t test_io_functions_int_();
int64_t test_scoping_int_();
int64_t test_comments_int_();
int64_t test_basic_types_int_()
{
  printf("%s\n", "=== Testing Basic Types ===");
  int64_t x;
  x = 42;
  printf("%s %d\n", "int:", x);
  bool flag;
  flag = true;
  printf("%s %s\n", "bool:", (flag ? "true" : "false"));
  printf("%s\n", "string: Hello, Lync!");
  printf("%s\n", "Escapes: \n\t\"quoted\" \\ backslash");
  return 0;
}
int64_t test_operators_int_()
{
  printf("%s\n", "=== Testing Operators ===");
  int64_t a;
  a = 10;
  int64_t b;
  b = 3;
  printf("%s %d\n", "Addition:", (a + b));
  printf("%s %d\n", "Subtraction:", (a - b));
  printf("%s %d\n", "Multiplication:", (a * b));
  printf("%s %d\n", "Division:", (a / b));
  printf("%s %s\n", "Greater than:", ((a > b) ? "true" : "false"));
  printf("%s %s\n", "Less than:", ((a < b) ? "true" : "false"));
  printf("%s %s\n", "Greater or equal:", ((a >= b) ? "true" : "false"));
  printf("%s %s\n", "Less or equal:", ((a <= b) ? "true" : "false"));
  printf("%s %s\n", "Equal:", ((a == b) ? "true" : "false"));
  printf("%s %s\n", "Not equal:", ((a != b) ? "true" : "false"));
  bool t;
  t = true;
  bool f;
  f = false;
  printf("%s %s\n", "AND:", ((t && f) ? "true" : "false"));
  printf("%s %s\n", "OR:", ((t || f) ? "true" : "false"));
  printf("%s %d\n", "Negation (int):", (-a));
  printf("%s %s\n", "Negation (bool):", ((!t) ? "true" : "false"));
  return 0;
}
int64_t test_control_flow_int_()
{
  printf("%s\n", "=== Testing Control Flow ===");
  int64_t x;
  x = 15;
  if ((x > 10))   {
    printf("%s\n", "x is greater than 10");
  }
 else   {
    printf("%s\n", "x is not greater than 10");
  }

  int64_t counter;
  counter = 0;
  while ((counter < 3))   {
    printf("%s %d\n", "while loop:", counter);
    counter = (counter + 1);
  }
  int64_t i;
  i = 0;
  do   {
    printf("%s %d\n", "do-while loop:", i);
    i = (i + 1);
  }
  while ((i < 2));
  printf("%s\n", "for loop (0 to 3):");
  for (int j = 0; j <= 3; j++)   {
    printf("%s %d\n", "  j =", j);
  }
  return 0;
}
int64_t add_int_int_int(int64_t a, int64_t b)
{
  return (a + b);
}
int64_t calculate_int_int_int(int64_t x, int64_t y)
{
  int64_t result;
  result = add_int_int_int(x, y);
  return (result * 2);
}
int64_t test_functions_int_()
{
  printf("%s\n", "=== Testing Functions ===");
  int64_t sum;
  sum = add_int_int_int(5, 7);
  printf("%s %d\n", "add(5, 7):", sum);
  int64_t calc;
  calc = calculate_int_int_int(3, 4);
  printf("%s %d\n", "calculate(3, 4):", calc);
  return 0;
}
int64_t test_match_values_int_()
{
  printf("%s\n", "=== Testing Match (Values) ===");
  int64_t x;
  x = 2;
  int64_t result;
  if (x == 1) {
    result = 100;
  }
  else if (x == 2) {
    result = 200;
  }
  else if (x == 3) {
    result = 300;
  }
  else {
    result = 0;
  }
  printf("%s %d\n", "Match expression result:", result);
  if (x == 1) {
    printf("%s\n", "x is 1");
  }
  else if (x == 2) {
    printf("%s\n", "x is 2");
  }
  else {
    printf("%s\n", "x is something else");
  }
  return 0;
}
int64_t test_ownership_int_()
{
  printf("%s\n", "=== Testing Ownership ===");
  int64_t *ptr1 = malloc(sizeof(int64_t));
  *ptr1 = 42;
  printf("%s %d\n", "Allocated ptr1:", *ptr1);
  free(ptr1);
  printf("%s\n", "Freed ptr1");
  int64_t *ptr2 = malloc(sizeof(int64_t));
  *ptr2 = 100;
  int64_t *r;
  r = &*ptr2;
  printf("%s %d\n", "Borrowed ref r:", *r);
  free(ptr2);
  printf("%s\n", "Freed ptr2 (owner)");
  return 0;
}
int64_t consume_int_intown(int64_t* p)
{
  int64_t val;
  val = *p;
  free(p);
  return val;
}
int64_t test_move_semantics_int_()
{
  printf("%s\n", "=== Testing Move Semantics ===");
  int64_t *ptr = malloc(sizeof(int64_t));
  *ptr = 77;
  int64_t result;
  result = consume_int_intown(*ptr);
  printf("%s %d\n", "Value from consumed ptr:", result);
  return 0;
}
int64_t test_nullable_types_int_()
{
  printf("%s\n", "=== Testing Nullable Types ===");
  int64_t *maybe1 = malloc(sizeof(int64_t));
  *maybe1 = 55;
  if (maybe1 != NULL) {
    int64_t* val = maybe1;
    printf("%s %d\n", "maybe1 has value:", *val);
  }
  else if (maybe1 == NULL) {
    printf("%s\n", "maybe1 is null");
  }
  int64_t result;
  if (maybe1 != NULL) {
    int64_t* val = maybe1;
    result = (*val * 2);
  }
  else if (maybe1 == NULL) {
    result = 0;
  }
  printf("%s %d\n", "Match expression result:", result);
  if (maybe1 != NULL)   {
    printf("%s %d\n", "maybe1 proven non-null:", *maybe1);
    free(maybe1);
  }

  int64_t *maybe2 = malloc(sizeof(int64_t));
  *maybe2 = 0;
  free(maybe2);
  return 0;
}
int64_t test_io_functions_int_()
{
  printf("%s\n", "=== Testing I/O Functions (std.io module) ===");
  printf("%s\n", "Enter an integer:");
  int64_t *num;
  num = read_int();
  if (num != NULL) {
    int64_t* n = num;
    printf("%s %d\n", "You entered:", *n);
  }
  else if (num == NULL) {
    printf("%s\n", "Invalid integer input");
  }
  free(num);
  printf("%s\n", "Enter a character:");
  char *ch;
  ch = read_char();
  if (ch != NULL) {
    char* c = ch;
    printf("%s\n", "You entered a character");
  }
  else if (ch == NULL) {
    printf("%s\n", "Invalid character input");
  }
  free(ch);
  return 0;
}
int64_t test_scoping_int_()
{
  printf("%s\n", "=== Testing Scoping ===");
  int64_t outer;
  outer = 1;
  printf("%s %d\n", "Outer variable:", outer);
  printf("%s %d\n", "Variables scoped to function:", outer);
  return 0;
}
int64_t test_comments_int_()
{
  printf("%s\n", "=== Testing Comments ===");
  printf("%s\n", "Single-line comments work");
  printf("%s\n", "Multi-line comments work");
  return 0;
}
int main()
{
  printf("%s\n", "========================================");
  printf("%s\n", "Lync v0.2.0 Comprehensive Feature Test");
  printf("%s\n", "========================================");
  printf("%s\n", "");
  test_basic_types_int();
  printf("%s\n", "");
  test_operators_int();
  printf("%s\n", "");
  test_control_flow_int();
  printf("%s\n", "");
  test_functions_int();
  printf("%s\n", "");
  test_match_values_int();
  printf("%s\n", "");
  test_ownership_int();
  printf("%s\n", "");
  test_move_semantics_int();
  printf("%s\n", "");
  test_nullable_types_int();
  printf("%s\n", "");
  test_scoping_int();
  printf("%s\n", "");
  test_comments_int();
  printf("%s\n", "");
  printf("%s\n", "=== Testing I/O ===");
  printf("%s\n", "Note: The following tests require manual input.");
  printf("%s\n", "Enter values when prompted, or press Ctrl+C to skip.");
  printf("%s\n", "");
  printf("%s\n", "");
  printf("%s\n", "========================================");
  printf("%s\n", "All tests completed successfully!");
  printf("%s\n", "========================================");
  return 0;
}
