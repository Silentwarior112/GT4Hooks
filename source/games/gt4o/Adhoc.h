#pragma once

#include "GameStructs/Adhoc.h"
#include "core/game/String.h"

void ADHOC_InjectNewModules();
void  HOOK_defineMoreClasses(void* value, hClass* previous, hClass* classId);

// std::basic_string shit
#define STD_STRING_BOILERPLATE(name) \
    void* unk; \
    int* v2 = (void*)ADDR_std_string_EmptyRep_Data; \
    if (*(int*)ADDR_std_string_EmptyRep_Flag) \
        v2 = ((int*(*)(void*))(ADDR_std_string_Rep_M_grab))((int*)ADDR_std_string_EmptyRep); \
    else \
        ++(*((int*)ADDR_std_string_EmptyRep_Refcount)); \
    unk = (void*)v2;

#define STD_STRING(name) \
    STD_STRING_BOILERPLATE(); \
    int len = __strlen(name); \
    ((void(*)(void*, int, long long, char*, int))ADDR_std_string_replace)(&unk, 0, -1, name, len);