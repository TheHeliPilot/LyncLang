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
// @lync_attr echo
typedef struct {
    float vx;
    float vy;
} Velocity;

int my_system_int();
// @lync_attr echo
// @lync_attr priority(10)
int my_system_int()
{
  return 0;
}
int main()
{
  printf("%s\n", "plugin demo");
  return 0;
}

/* ---- plugin top emissions ---- */
/* component reflection for Position */
typedef struct { const char* name; const char* type; } Position_FieldInfo;
static const Position_FieldInfo Position_fields[] = {
    { "x", "float" },
    { "y", "float" },
};

/* component reflection for Velocity */
typedef struct { const char* name; const char* type; } Velocity_FieldInfo;
static const Velocity_FieldInfo Velocity_fields[] = {
    { "vx", "float" },
    { "vy", "float" },
};

/* echo plugin saw: struct 'Velocity' */
/* echo plugin saw: func 'my_system' */

/* ---- plugin init function (auto-generated) ---- */
static void __lync_plugin_init(void) {
    /* echo init for Velocity */ (void)0;
    /* echo init for my_system */ (void)0;
}
