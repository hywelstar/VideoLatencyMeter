#pragma once

#include <stdbool.h>

/* Returns true only when `line` was an AT command (including malformed AT). */
bool at_command_execute(const char *line);
bool at_command_echo_enabled(void);
