#include <stddef.h>
#include <stdio.h>

#include "core/ps2/Memory.h"
#include "core/Log.h"

#include "Adhoc.h"
#include "MCarGarage.h"
#include "GameFunctions/Adhoc.h"

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

    /* METHODS, unlike the function above, because a script calls them on a car:
       riding_car.setPowerSetting(1250). That is the same shape the module's own
       setBallastSetting has - it is registered with defineMethod at 0x0013AE24 -
       and defineMethod takes a0 exactly as defineFunction does, so tempHValue is
       forwarded unchanged here too.

       These land on the same module the ballast members do. The displaced call
       receives the module in $s3 (0x00132E64), and the same $s3 is handed to the
       second half of InitClass at 0x0013A978 (0x00132E78), which is where
       setBallastSetting is registered - and that function has exactly one caller
       in the image.

       NO HValue_dtor between any of these, deliberately. MStorage.c documents a
       boot crash caused by exactly that, bisected three ways, and the reason is
       still not understood - the structural explanation that mCarGarage differs
       from mStorage does not survive checking, since both emit one dtor per
       registration in their own InitClass. So this follows the configuration
       that is known to boot rather than the one that is known to crash. The cost
       is a leaked reference on each handle but the last, on a module that lives
       for the whole session. */
    hModule_defineMethod(tempHValue, module, "setPowerSetting", &m_setPowerSetting);
    hModule_defineMethod(tempHValue, module, "getPowerSetting", &m_getPowerSetting);

    /* No name collision: the module owns getColorIndex, getColorNum, getColors,
       getAllColors and getColorName (registered 0x00131F24 through 0x00131FC4)
       but no setter. MCarFace's own "setColorIndex" is a different module. */
    hModule_defineMethod(tempHValue, module, "setColorIndex", &m_setColorIndex);
    // No dtor call here, it'll be handled by parent function
}

/* ---- the power modifier ---------------------------------------------------

   A car's settings record carries it as a 16-bit value at CARGARAGE_RECORD_POWER,
   per mille: 1000 is 1.000x and 0 means "no modifier". The full layout, the
   proof of the scale, and why the record pointer is asked for rather than
   computed are all in the target header next to those constants.
*/

/* An adhoc value as an int, through the conversion slot every value type
   implements. `slot` is the ADDRESS of the word holding the object, which is
   what argv + n and this_ both are.

   Written here rather than shared with MStorage.c's equivalent: that file has a
   set of eight such helpers and lifting three of them into a common header
   would leave a worse split than either whole one. */
static int AdhocToInt(hObject** slot)
{
    char* obj;
    char* entry;
    int (*fn)(void*);

    if (slot == NULL || *slot == NULL)
        return 0;

    obj   = *(char**)slot;
    entry = (char*)(*(void**)(obj + 4)) + ADHOC_VT_TOINT;
    fn    = *(int (**)(void*))(entry + 4);

    return fn(obj + *(short*)entry);
}

/* Only a number. A string would convert too - hString::toInt even sniffs an 0x
   prefix and reads hex - which is not something a power setting should accept
   silently. */
static int AdhocIsNumber(hObject** slot)
{
    void* vt;

    if (slot == NULL || *slot == NULL)
        return 0;

    vt = *(void**)(*(char**)slot + 4);

    return vt == (void*)ADDR_hInt_VTable || vt == (void*)ADDR_hFloat_VTable;
}

/*
    The selected car's settings record, or NULL.

    The wrapper is handed back because the caller has to release it either way -
    the game's own constructor is a dynamic_cast that returns WITHOUT storing on
    failure, having already zeroed the slot, so a null result is well defined and
    still needs its dtor.
*/
static char* PowerRecord(HObject* receiver, MCarGarage** garageOut)
{
    MCarGarage* garage = NULL;
    CarGarage*  container;

    MCarGarage_MCarGarage(&garage, receiver);
    *garageOut = garage;

    if (garage == NULL)
        return NULL;

    container = mCarGarage_getCarGarage(garage);

    if (container == NULL)
        return NULL;

    /* Asks the game for the record rather than computing garage + 8 + 400*slot.
       The +8 is real and easy to drop, and both the right and the wrong number
       exist in the image as genuine displacements of other fields, so a mistake
       would write plausible-looking garbage instead of faulting. */
    return (char*)CarGarage_getCurrentCar(container);
}

/* riding_car.setPowerSetting(value) -> 1 on success, 0 if it refused. */
void m_setPowerSetting(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    MCarGarage* garage = NULL;
    HInt* result;
    int   status = 0;

    /* Nothing in the VM checks arity, so reading argv[0] with argc < 1 would
       take a live object off the operand stack. */
    if (argc < 1)
    {
        LOG("power: setPowerSetting takes (value)\n");
    }
    else if (!AdhocIsNumber(argv))
    {
        LOG("power: setPowerSetting wants a number\n");
    }
    else
    {
        int value = AdhocToInt(argv);

        /* The field is 16 bits wide. This is not an invented range - anything
           outside it would simply wrap, and a script asking for 70000 would
           quietly get 4464. */
        if (value < 0 || value > 0xFFFF)
        {
            LOG("power: %d is outside 0 to 65535\n", value);
        }
        else
        {
            char* record = PowerRecord(this_, &garage);

            if (record == NULL)
            {
                LOG("power: no car in the selected slot\n");
            }
            else
            {
                *(unsigned short*)(record + CARGARAGE_RECORD_POWER) =
                    (unsigned short)value;
                status = 1;
            }
        }
    }

    MCarGarage_dtor((MCarGarage*)&garage, 2);

    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/*
    riding_car.getPowerSetting() -> the stored value, or -1 if there is no car.

    Worth shipping alongside the setter: 0 is ambiguous without it. A zero means
    "no modifier", which is what a stock car reads unless the parts database gave
    it one on install (0x003042EC), so a script cannot otherwise tell an untouched
    car from one it has already set, nor find out what it is about to overwrite.
    -1 for "no car" is outside the field's range, so it cannot collide.
*/
void m_getPowerSetting(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    MCarGarage* garage = NULL;
    HInt* result;
    int   value = -1;

    char* record = PowerRecord(this_, &garage);

    if (record != NULL)
        value = (int)*(unsigned short*)(record + CARGARAGE_RECORD_POWER);
    else
        LOG("power: no car in the selected slot\n");

    MCarGarage_dtor((MCarGarage*)&garage, 2);

    HInt_HInt((HInt*)&result, value);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/* ---- the colour index -----------------------------------------------------

   A signed byte at CARGARAGE_RECORD_COLOR. Only a setter lives here: the game
   already registers getColorIndex, getColorNum, getColors, getAllColors and
   getColorName on this module, so reading it and asking how many colours a car
   has are both already script-reachable. The layout and the reasoning behind
   going through the game's own setter are in the target header.
*/

/*
    The container to set the colour through, or NULL if the slot is not fit to
    be written.

    Two fields have to be sound, not one. The game's own emptiness test is the
    car code at record+0 reading all ones, but that is not sufficient here:
    CarGarage::setColorIndexAll bottoms out in CarRecord::getColorCount, which
    reads a DIFFERENT field - the variation key at record+0x10 - and the part
    path at 0x00303DD8 can leave a record carrying a valid car code beside a -1
    key (0x00303F5C tests for -1 and stores it anyway from the delay slot).
    Checking only the car code would pass exactly that record into a lookup that
    indexes a null database table.
*/
static CarGarage* ColorTarget(HObject* receiver, MCarGarage** garageOut,
                              char** recordOut)
{
    MCarGarage* garage = NULL;
    CarGarage*  container;
    char*       record;

    *recordOut = NULL;

    MCarGarage_MCarGarage(&garage, receiver);
    *garageOut = garage;

    if (garage == NULL)
        return NULL;

    container = mCarGarage_getCarGarage(garage);

    if (container == NULL)
        return NULL;

    record = (char*)CarGarage_getCurrentCar(container);

    if (record == NULL)
        return NULL;

    *recordOut = record;

    /* Both fields are 64 bit and both read as all ones when unset. */
    if (*(unsigned int*)(record + CARGARAGE_RECORD_CARCODE) == 0xFFFFFFFFu &&
        *(unsigned int*)(record + CARGARAGE_RECORD_CARCODE + 4) == 0xFFFFFFFFu)
        return NULL;

    if (*(unsigned int*)(record + CARGARAGE_RECORD_VARIATION) == 0xFFFFFFFFu &&
        *(unsigned int*)(record + CARGARAGE_RECORD_VARIATION + 4) == 0xFFFFFFFFu)
        return NULL;

    return container;
}

/*
    Mirror the colour into the garage list entry, which is where it persists.

    Painting the working car does not last on its own. The list entry is the copy
    written when the car is bought, and 0x003113E0 reads it back whenever the car
    is loaded out of the list - ld 0x8($entry) / dsll 30 / dsra32 / andi 127,
    handed straight to CarGarage::setColorIndexAll. So an unmirrored change is
    undone by the game itself at the next load.

    Both guards earn their place. GarageList::getCurrentEntry clamps a negative
    index to zero instead of failing, so with nothing selected it returns entry
    0 - a real entry belonging to some other car. Comparing the entry's car code
    against the record's catches that, and it also catches the list-from-garage
    derivation being wrong, which is the one step here that is arithmetic rather
    than a call.
*/
static int MirrorColorToList(CarGarage* container, const char* record, int index)
{
    char*              list  = (char*)container - GARAGE_LIST_TO_CARGARAGE;
    char*              entry = (char*)GarageList_getCurrentEntry(list);
    unsigned long long flags;

    if (entry == NULL)
        return 0;

    /* The list field is seven bits wide, so it simply cannot carry more than
       this - unlike the record byte, which the engine already bounded against
       the car's palette. */
    if (index < 0 || index > 0x7F)
        return 0;

    if ((*(unsigned int*)(entry + GARAGE_ENTRY_FLAGS) & GARAGE_ENTRY_OCCUPIED_BIT) == 0)
        return 0;

    if (*(unsigned int*)(entry + GARAGE_ENTRY_CARCODE) !=
            *(unsigned int*)(record + CARGARAGE_RECORD_CARCODE) ||
        *(unsigned int*)(entry + GARAGE_ENTRY_CARCODE + 4) !=
            *(unsigned int*)(record + CARGARAGE_RECORD_CARCODE + 4))
        return 0;

    flags = *(unsigned long long*)(entry + GARAGE_ENTRY_FLAGS);
    flags &= ~(unsigned long long)GARAGE_ENTRY_COLOR_MASK;
    flags |= ((unsigned long long)index) << GARAGE_ENTRY_COLOR_SHIFT;

    *(unsigned long long*)(entry + GARAGE_ENTRY_FLAGS) = flags;

    return 1;
}

/* riding_car.setColorIndex(index) -> 1 on success, 0 if it refused. */
void m_setColorIndex(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    MCarGarage* garage = NULL;
    HInt* result;
    int   status = 0;

    if (argc < 1)
    {
        LOG("colour: setColorIndex takes (index)\n");
    }
    else if (!AdhocIsNumber(argv))
    {
        LOG("colour: setColorIndex wants a number\n");
    }
    else
    {
        int index = AdhocToInt(argv);

        /* Only the lower bound is ours. The engine refuses any index the car's
           palette does not have, so a script that asks for colour 40 on a
           six-colour car simply gets 0 back - no clamp, no wrap, nothing
           written. But the engine's bound is a SIGNED compare, so a negative
           slips past it and is stored, and -1 in particular would re-arm the
           "no colour chosen" sentinel. That hole is the caller's to plug. */
        if (index < 0 || index > 255)
        {
            LOG("colour: %d is outside 0 to 255\n", index);
        }
        else
        {
            char*      record;
            CarGarage* container = ColorTarget(this_, &garage, &record);

            if (container == NULL)
            {
                LOG("colour: no usable car in the selected slot\n");
            }
            else if (!CarGarage_setColorIndexAll(container, index))
            {
                /* Not a failure of ours - the car has fewer colours than that.
                   getColorNum() is what a script should ask first. */
                LOG("colour: this car has no colour %d\n", index);
            }
            else if (!MirrorColorToList(container, record, index))
            {
                /* The car IS painted, and will look right until it is next
                   loaded out of the garage list - at which point the list's own
                   copy paints it back. Reported as a failure rather than a
                   success because the colour will not stay. */
                LOG("colour: painted, but the garage list entry was not updated\n");
            }
            else
            {
                status = 1;
            }
        }
    }

    MCarGarage_dtor((MCarGarage*)&garage, 2);

    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
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
