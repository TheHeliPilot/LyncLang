#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// @lync_attr component
typedef struct {
    float x;
    float y;
} Position;

// @lync_attr component
// @lync_attr serializable
typedef struct {
    float vx;
    float vy;
} Velocity;

int init_int();
int my_input_system_int();
// @lync_attr on_load
int init_int()
{
  return 0;
}
// @lync_attr system("Input", "Game")
// @lync_attr priority(10)
int my_input_system_int()
{
  return 0;
}
int main()
{
  printf("%s\n", "attrs compiled cleanly");
  return 0;
}
