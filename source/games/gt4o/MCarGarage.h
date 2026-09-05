#pragma once

#include "GameFunctions/MCarGarage.h"
#include "GameStructs/Adhoc.h"
#include "Adhoc/Decl.h"

void MCarGarage_InstallHooks();

void HOOK_ExtendMCarGarage(void* tempHValue, hModule* module, char* name, Adhoc_function_cb func);
void m_getPerformanceIndex(HObject* return_value, HObject* this_, int argc, hObject** argv);