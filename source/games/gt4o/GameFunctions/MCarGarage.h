#pragma once

#include "core/Target.h"

#include "../GameStructs/Adhoc.h"

extern float (*QUICK_MENU_GetGTPerformanceIndex)(CarGarage* car);
extern void (*MCarGarage_MCarGarage)();
extern void (*MCarGarage_dtor)(MCarGarage* mCar, int flag);
extern CarGarage* (*mCarGarage_getCarGarage)(MCarGarage* mCar);
