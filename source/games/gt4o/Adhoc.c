#include "Adhoc/MMyModule.h"
#include "core/game/IO.h"

#include "core/ps2/Memory.h"


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

