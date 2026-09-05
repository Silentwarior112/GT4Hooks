#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
    Small self-contained string helpers.

    Deliberately NOT calls into the game: these are used on the file-open path
    before anything else is known to be safe, and keeping them local means the
    HostFS hook depends on four fewer resolved addresses per build.
*/

char* _strrchr(char* s, int c);
bool starts_with(const char* string, const char* prefix);
char* get_filename_ext(char* filename);
