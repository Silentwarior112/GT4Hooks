#pragma once

#include <stddef.h>

/*
    Path helpers used only by the Gran Turismo 4 game.

    These lived alongside the generic string helpers, but MStorage is their only
    consumer, so they stay with the game rather than going into shared code.
*/

void* __memrchr(const void* m, int c, size_t n);
char* _dirname(char* path);
char* get_file_name(char* fullpath);
