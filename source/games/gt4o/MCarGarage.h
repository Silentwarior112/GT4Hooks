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