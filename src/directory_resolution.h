#ifndef DIRECTORY_RESOLUTION_H
#define DIRECTORY_RESOLUTION_H

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// The root directory of the running server
extern char BASE_DIR[PATH_MAX + 1];

// Resolve the program argument into our global variable
void resolve_dir(char *dirpath);

#endif