#pragma once

#include "core/Target.h"

/*
    The game's own string routines.

    The HostFS hook only needs strcmp (extension compare) and strlen, but the
    whole set is exposed so other Tourist Trophy hooks can use them without
    pulling in newlib.
*/

extern int (*__strlen)(char* str);
extern int (*__strcmp)(const char* left, const char* right);
extern char* (*__strcpy)(const char* destination, const char* source);
extern char* (*__strstr)(const char* str1, const char* str2);
