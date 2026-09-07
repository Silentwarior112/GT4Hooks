#include "Adhoc/MMyModule.h"
#include "core/game/IO.h"

#include "core/ps2/Memory.h"
#include "Adhoc.h"
#include "GameFunctions/Adhoc.h"

/* See the comment on the declaration - the float has to reach $f12 by hand. */
void ADHOC_MakeFloat(HFloat** slot, float value)
{
    union { float f; unsigned int u; } bits;
    bits.f = value;

    {
        register void*        _a0 __asm__("$4")  = (void*)slot;
        register unsigned int _v  __asm__("$8")  = bits.u;
        register void*        _fn __asm__("$25") = (void*)HFloat_HFloat;

        __asm__ __volatile__(
            ".set push\n\t"
            ".set noreorder\n\t"
            "mtc1  %2, $f12\n\t"
            "jalr  %1\n\t"
            "nop\n\t"
            ".set pop"
            : "+r"(_a0)
            : "r"(_fn), "r"(_v)
            : "$2", "$3", "$5", "$6", "$7", "$9", "$10", "$11", "$12", "$13",
              "$14", "$15", "$24", "$31", "memory");
    }
}


void (*funcs[])() = {
    // Add modules constructor functions here.
    mMyModule_CreateInit_,
};

hClass* (*classIds[])() = {
    // Add module class ids go here.
    mMyModule_getClassId,
};


/* Hook: Adhoc
   Purpose: Adds new adhoc modules.
   How: Creates brand new class inits, class definitions.
*/

void ADHOC_InjectNewModules()
{
    // Base modules are always added in engine, new modules needs to be added
    // though InitializeAdhocMenu. init_menu and init_gt4app callbacks to hADHOC::addModuleHandle
    // Override the last defineClass to register the original class that was going to be registed, and our additional ones
    MAKE_JAL(ADDR_ADHOC_LastDefineClass_JAL, &HOOK_defineMoreClasses);

    // Call CreateInit_ on every registered module (equivalent to whats in the static ctor)
    int num = sizeof(funcs) / sizeof(funcs[0]);
    for (int i = 0; i < num; i++)
        funcs[i]();
}

void HOOK_defineMoreClasses(void* value, hClass* previous, hClass* classId)
{
    /* hClass begins with its hModule, so these downcasts are offset-0 casts. */
    hModule_defineClass(value, (hModule*)previous, (hModule*)classId);
    HValue_dtor(value, 2);

    int num = sizeof(funcs) / sizeof(funcs[0]);

    for (int i = 0; i < num; i++)
    {
        hClass* id = classIds[i]();
        hModule_defineClass(value, (hModule*)previous, (hModule*)id);

        if (i < num - 1) // Avoid double free from parent
            HValue_dtor(value, 2);
    }
}

