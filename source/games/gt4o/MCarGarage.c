#include <stddef.h>

#include "core/ps2/Memory.h"

#include "Adhoc.h"
#include "MCarGarage.h"

/* Hook: MCarGarage
   Purpose: Extends the MCarGarage built-in module.
            Adds getPerformanceIndex, a function returning a car's performance index.
   How: Redirect hModule::defineFunction(..., ..., "sortByDisplacement", ...) to our hook,
        which will register sortByDisplacement on its own along with our new stuff.
   Extra Note: This is a useful sample to view how to extend an existing built-in module.

   CASING, because it has cost time already: a module has TWO names. The C++
   type is `mCarGarage` (lower m, string at 0x6E4340, mangled 0x6E4AF0 as
   "10mCarGarage"), but the name scripts see is `MCarGarage` (capital M, string
   at 0x6E43A0) - that is what hModule::setName registers. Decl.h's
   ADHOC_START_MODULE(type_name, access_name) is the same split, and every
   sibling module follows it: MGarage, MCarModel, MQuickWork, MOption.
   So scripts must write

       main::menu::MCarGarage::getPerformanceIndex(car)

   with a capital M. The C symbols here keep the lowercase spelling only where
   they name the game's own C++ functions.
*/

void MCarGarage_InstallHooks()
{
    MAKE_JAL(ADDR_MCarGarage_sortByDisplacement_register, &HOOK_ExtendMCarGarage);
}

// NOTE: This function matches the signature for **function** registering
// If you are hooking into an attribute register function, make sure to adjust this function signature 
void HOOK_ExtendMCarGarage(void* tempHValue, hModule* module, char* name, Adhoc_function_cb func)
{
    // NOTE: This should be changed if you are hooking into a method (or something else)
    hModule_defineFunction(tempHValue, module, name, func);
    HValue_dtor(tempHValue, 2);

    /* tempHValue is the caller's out-slot POINTER, not a handle to take the
       address of: the game does `move $a0, $sp` at 0x132E54 and destroys that
       same slot at 0x132E6C. defineFunction (0x4F0650) and defineMethod
       (0x4F0B10) treat a0 identically - both only save it to $s4 and return it
       - so it is forwarded unchanged. Passing its address instead would leave
       the game's own slot holding the already-released sortByDisplacement
       value for the caller's dtor to release a second time.

       Registered as a FUNCTION, not a method. The VM binds a method's receiver
       at member-LOOKUP time - hMethodValue::call takes `this` from
       wrapper+0x10 (0x4ED2FC) and never from argv - so a method is only
       reachable as `car.getPerformanceIndex()`. Scripts call this as
       `MCarGarage::getPerformanceIndex(car)`, which is the function form, and
       that is how the module's own sortByX members are registered. */
    hModule_defineFunction(tempHValue, module, "getPerformanceIndex", &f_getPerformanceIndex);
    // No dtor call here, it'll be handled by parent function
}

void f_getPerformanceIndex(HObject* return_value, int argc, hObject** argv)
{
    MCarGarage* garage;
    CarGarage* car;
    float perfIndex;
    HFloat* result;

    /* Callback shape taken from the module's own sortByDisplacement callback at
       0x131CD0: a1 = argc (`blez $s0`), a2 = argv. */
    if (argc < 1)
        return;

    /* argv is the address of the first argument's handle slot, and
       MCarGarage_MCarGarage reads its second parameter with `lw $v1, ($a1)`
       (0x12A068) - so it wants that slot address, which is argv itself. The
       game's own mCarGarage functions do exactly this at 0x12BD40. */
    MCarGarage_MCarGarage(&garage, (HObject*)argv);

    /* The conversion inside MCarGarage_MCarGarage is a dynamic_cast (0x12A094)
       and on failure it returns WITHOUT storing (0x12A0A0), leaving the handle
       null. mCarGarage_getCarGarage is a bare `lw $v0, 0x14($a0)` with no check
       of its own, so this guard is the difference between "the script passed
       the wrong thing" and a crash. */
    if (garage == NULL)
    {
        MCarGarage_dtor((MCarGarage*)&garage, 2);
        return;
    }

    car = mCarGarage_getCarGarage(garage);

    if (car == NULL)
    {
        MCarGarage_dtor((MCarGarage*)&garage, 2);
        return;
    }

    perfIndex = QUICK_MENU_GetGTPerformanceIndex(car);

    /* As in MMyModule.c: these constructors fill a handle slot, so they take
       the ADDRESS of the handle variable. */
    ADHOC_MakeFloat(&result, perfIndex);
    ADHOC_MakeRef(return_value, (HObject*)result);

    HFloat_dtor((HFloat*)&result, 2);
    MCarGarage_dtor((MCarGarage*)&garage, 2);
}
