#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

// std.io helper functions
int64_t* read_int() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    int64_t* result = malloc(sizeof(int64_t));
    *result = atoll(buffer);
    return result;
}

char** read_str() {
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    //remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
    char** result = malloc(sizeof(char*));
#ifdef _WIN32
    *result = _strdup(buffer);
#else
    *result = strdup(buffer);
#endif
    return result;
}

bool* read_bool() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    bool* result = malloc(sizeof(bool));
    if (strncmp(buffer, "true", 4) == 0 || strncmp(buffer, "1", 1) == 0) {
        *result = true;
    } else if (strncmp(buffer, "false", 5) == 0 || strncmp(buffer, "0", 1) == 0) {
        *result = false;
    } else {
        free(result);
        return NULL;
    }
    return result;
}

char* read_char() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    if (buffer[0] == '\0' || buffer[0] == '\n') return NULL;
    char* result = malloc(sizeof(char));
    *result = buffer[0];
    return result;
}

char* read_key() {
    char* result = malloc(sizeof(char));
#ifdef _WIN32
    *result = _getch();
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    *result = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    return result;
}

int main()
{
  int64_t *i;
  i = read_int();
  if (i != NULL)   {
    printf("%d\n", *i);
  }

  read_key();
  return 0;
}
