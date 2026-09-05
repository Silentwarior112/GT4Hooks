#include "String.h"

char* _strrchr(char* s, int c)
{
    char* found = NULL;

    do
    {
        if (*s == (char)c)
            found = s;
    } while (*s++);

    return found;
}

bool starts_with(const char* string, const char* prefix)
{
    while (*prefix)
    {
        if (*prefix++ != *string++)
            return false;
    }

    return true;
}

char* get_filename_ext(char* filename)
{
    char* dot = _strrchr(filename, '.');
    if (!dot || dot == filename)
        return NULL;

    return dot + 1;
}
