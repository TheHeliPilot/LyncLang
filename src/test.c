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

//std.io helper functions
int* read_int() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    int* result = malloc(sizeof(int));
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

float* read_float() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    float* result = malloc(sizeof(float));
    *result = strtof(buffer, NULL);
    return result;
}

double* read_double() {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;
    double* result = malloc(sizeof(double));
    *result = strtod(buffer, NULL);
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
  printf("%s\n", "Enter a float: ");
  float *f_opt;
  f_opt = read_float();
  if (f_opt != NULL) {
    float* val = f_opt;
    printf("%s %g\n", "You entered float: ", *val);
  }
  else if (f_opt == NULL) {
    printf("%s\n", "Failed to read float");
  }
  free(f_opt);
  printf("%s\n", "Enter a double: ");
  double *d_opt;
  d_opt = read_double();
  if (d_opt != NULL) {
    double* val = d_opt;
    printf("%s %g\n", "You entered double: ", *val);
  }
  else if (d_opt == NULL) {
    printf("%s\n", "Failed to read double");
  }
  free(d_opt);
  return 0;
}
