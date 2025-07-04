#ifndef MIME_TYPES_H
#define MIME_TYPES_H

#include <stdio.h>
#include <string.h>

// Returns a file's MIME type based on its extensions
const char *get_MIME_type(const char *filename);

#endif