#pragma once

#include "GameFunctions/MCarGarage.h"
#include "GameStructs/Adhoc.h"
#include "Adhoc/Decl.h"

void MCarGarage_InstallHooks();

void HOOK_ExtendMCarGarage(void* tempHValue, hModule* module, char* name, Adhoc_function_cb func);

/*
    A FUNCTION, not a method - scripts call this as
    main::menu::MCarGarage::getPerformanceIndex(car), which is the module-scope
    form. Note the capital M: MCarGarage is the script-visible module name,
    mCarGarage is the C++ type. See the comment on the registration in
    MCarGarage.c.
*/
void f_getPerformanceIndex(HObject* return_value, int argc, hObject** argv);

/*
    The current car's power modifier, as METHODS - so scripts call them on a car
    the way they call the game's own setBallastSetting:

        main::game.garage.riding_car.setPowerSetting(1250);   // 1.25x
        var p = main::game.garage.riding_car.getPowerSetting();

    The value is per mille: 1000 is 1.000x, and 0 means "no modifier", which is
    what a stock car reads unless the parts database gave it one. setPowerSetting
    returns 1 on success and 0 if it refused.

    Two things a script author has to know, both true of the game's own ballast
    setter as well:

      * FITTING ANY PART ZEROES IT. The record reset at 0x00303BF8 runs on the
        part-install path, so a power setting does not survive a tuning change.
      * THE VALUE MAY NOT REACH A RACE IMMEDIATELY. This writes the garage's
        working record; a race entry is built from a stored copy that only
        catches up when the game commits it (0x00110F70 and two siblings). Stock
        setBallastSetting behaves the same way.
*/
void m_setPowerSetting(HObject* return_value, HObject* this_, int argc, hObject** argv);
void m_getPowerSetting(HObject* return_value, HObject* this_, int argc, hObject** argv);

/*
    Set the car's colour.

        main::game.garage.riding_car.setColorIndex(3);   // 1 on success, 0 if refused

    Only the setter, because the game already ships the rest. MCarGarage registers
    getColorIndex, getColorNum, getColors, getAllColors and getColorName of its
    own, so a script reads the current colour with riding_car.getColorIndex() and
    asks how many the car has with riding_car.getColorNum() - which is what it
    should clamp against. The one thing missing was a way to write it.
    ("setColorIndex" also exists on MCarFace, but that drives a UI widget rather
    than the garage record - a different module, no conflict.)

    Accepts 0..255 and lets the ENGINE decide the real limit: it refuses any index
    the car's palette does not have, so an over-range value returns 0 and changes
    nothing. Negatives are refused here instead, because the engine's own bound is
    a signed compare that a negative slips past - and -1 in particular is the
    "no colour chosen" sentinel, which a script should not be able to re-arm.

    The colour belongs to the CAR, not to a settings sheet, so this writes all
    three of the container's sheets the way the game's own callers do.

    The same two warnings as the power setting apply, for the same reason - the
    colour and the power modifier are fields of one 400-byte record, so whatever
    disturbs one disturbs the other:

      * FITTING A PART CAN RESET IT. 0x00303E5C is a jal to the record reset at
        0x00303BF8, inside the same function the part path runs.
      * THE VALUE MAY NOT REACH A RACE IMMEDIATELY, since a race entry is built
        from a stored copy that only catches up when the game commits.
*/
void m_setColorIndex(HObject* return_value, HObject* this_, int argc, hObject** argv);