#include <stdio.h>

struct ShellState;
ShellState* shell_state_init(const char* db_fn, FILE* out);

void shell_state_free(ShellState* state);

int do_meta_command_r(char *zLine, struct ShellState *p);