#pragma once

#include "core/Target.h"

#include "../GameStructs/Adhoc.h"

extern float (*QUICK_MENU_GetGTPerformanceIndex)(CarGarage* car);
extern void (*MCarGarage_MCarGarage)();
extern void (*MCarGarage_dtor)(MCarGarage* mCar, int flag);
extern CarGarage* (*mCarGarage_getCarGarage)(MCarGarage* mCar);

/* The settings record of the car in the garage's selected slot. Returns a
   pointer already past the container's 8-byte head - see the target header. */
extern void* (*CarGarage_getCurrentCar)(CarGarage* garage);

/* Sets the paint index on all three of the container's setting sheets, and
   refuses an index the car's palette does not have. See the target header. */
extern int (*CarGarage_setColorIndexAll)(CarGarage* garage, int index);

/* The garage list entry of the currently selected car - see the target header.
   Clamps a negative index to zero rather than failing, so what it returns is
   only trustworthy once its car code has been checked. */
extern void* (*GarageList_getCurrentEntry)(void* list);
