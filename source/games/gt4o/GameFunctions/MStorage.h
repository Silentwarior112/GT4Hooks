#pragma once

#include "core/Target.h"
#include "core/pdistd/SceStructs.h"

extern int (*mStorageMC_getFileSize)();

/*
    The game's own blocking sceMcGetDir - see ADDR_mcGetDir. Locks the libmc
    monitor, retries the queue, waits for completion, unlocks. Returns the
    number of entries written into table, or a negative libmc error.
*/
extern int (*mcGetDir)(int port, int slot, const char* name, unsigned int mode,
                       int maxent, sceMcTblGetDir* table);

/*
    The library read - see ADDR_mcRead. Takes the manager monitor itself.
    Returns 0 on success, a non-zero libmc error otherwise.
*/
extern int (*mcRead)(int index, const char* name, void* buf, int len, int offset);

/*
    The library write - see ADDR_mcWrite. Takes the manager monitor itself.
    Returns 0 on success, a non-zero libmc error otherwise.
*/
extern int (*mcWrite)(int index, const char* name, const void* buf, int len, int offset);
