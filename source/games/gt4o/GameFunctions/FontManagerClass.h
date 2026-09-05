#pragma once

#include "core/Target.h"

#include "../GameStructs/FontManagerClass.h"

extern void (*PDISTD_FontManagerClass_putstring)(FontManagerClass* manager, char* str);
extern void (*PDISTD_FontManagerClass_printf)(FontManagerClass* manager, char* str, ...);
extern void (*PDISTD_FontManagerClass_setRegion)(FontManagerClass* manager, int x, int y, int w, int h);
extern void (*PDISTD_FontManagerClass_setFont)(FontManagerClass* manager, char* fontName);
