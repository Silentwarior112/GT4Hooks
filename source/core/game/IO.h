#pragma once

#include "core/Target.h"
#include "core/pdistd/SceStructs.h"

extern void (*_sprintf)(char* dest, const char* format, ...);
extern void (*_print)(const char* format, ...);

extern int (*sceOpen)(const char* name, int flags);
extern int (*sceClose)(int fd);
extern int (*sceRead)(int fd, void* buf, int count);
extern int (*sceLSeek)(int fd, int offset, int whence);
extern int (*sceGetStat)(const char* name, struct sce_stat* buf);
extern int (*sceStdioConvertError)(int func, int ioerror);

extern int (*sceMcGetDir)(int port, int slot, const char* name, unsigned int mode, int maxent, sceMcTblGetDir* table);
extern int (*sceMcSync)(int mode, int* cmd, int* result);

extern char* (*PlaystationX_LockFileName)(unsigned int nameHandle);
extern void (*PlayStationX_UnlockFileName)(unsigned int nameHandle);
