#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char BASE_DIR[PATH_MAX + 1];

void resolve_dir(char *dirpath)
{
    char resolved_path[PATH_MAX + 1];

    // Resolve the directory path
    if (realpath(dirpath, resolved_path) == NULL) {
        perror("resolve_dir - error resolving file path");
        exit(EXIT_FAILURE);
    }

    // Validate the directory and check if it's not just a file
    struct stat path_stat;
    if (stat(resolved_path, &path_stat)) {
        perror("resolve_dir - error filling stat object");
        exit(EXIT_FAILURE);
    }

    if (!S_ISDIR(path_stat.st_mode)) {
        fprintf(stderr, "%s is not a directory\n", resolved_path);
        exit(EXIT_FAILURE);
    }

    // Paste the path into our global variable
    strcpy(BASE_DIR, resolved_path);
}