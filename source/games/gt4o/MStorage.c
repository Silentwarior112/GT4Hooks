#include <stdbool.h>
#include <stdio.h>

#include "MStorage.h"
#include "core/ps2/Memory.h"
#include "core/Log.h"
#include "core/util/String.h"
#include "util/Path.h"

#include "core/game/String.h"
#include "GameFunctions/MStorage.h"
#include "core/game/IO.h"
#include "GameFunctions/Monitor.h"

/* Hook: MStorage
   Purpose: Allows reading files from memory card from adhoc.
   How: Fixes MStorageMC::getFileSize being unimplemented so that MStorage::read works (which uses it)
   Adhoc Usage:
     #define STORAGE_HD 0
     #define STORAGE_MC 1

     var storage = main::menu::MStorage::getStorage(STORAGE_MC);
     var str = storage.read("/BASCUS-97436GAMEDATA/file.txt");
     print "%{str}\n";
*/

void MStorage_InstallHooks()
{
    HOOK(ADDR_mStorageMC_getFileSize, &HOOK__mStorageMC_getFileSize);
}

unsigned int HOOK__mStorageMC_getFileSize(void* this, char* name)
{
    // Note: game immediately reuses return value into a malloc, so do not return something like -1
    PDISTD_Monitor_lock((void*)(ADDR_MStorageMC_Global + MSTORAGEMC_MONITOR_OFFSET)); // thread lock

    int cmd, result;

    char copy[128] = {0};
    __strcpy(copy, name);
    char* dir = _dirname(copy);

    char mcDir[128] = {0};
    _sprintf(mcDir, "%s/*", dir);

    sceMcTblGetDir table[25];
    sceMcGetDir(0, 0, mcDir, 0, 25, table);
    int res = sceMcSync(0, &cmd, &result);

    /* Single exit. The monitor taken above used to be released only on the
       not-found fall-through, so both the res < 0 guard and the success path
       returned still holding it - the first successful lookup leaked the lock
       for the rest of the session. */
    unsigned int size = 0;
    bool found = false;

    if (res >= 0)
    {
        char* fileName = get_file_name(name);
        for (int i = 0; i < result; i++)
        {
            sceMcTblGetDir* entry = &table[i];
            //LOG("name=%s, size=%x\n", entry->EntryName, entry->FileSizeByte);

            if (!__strcmp(entry->EntryName, fileName))
            {
                size = entry->FileSizeByte;
                found = true;
                break;
            }
        }
    }

    PDISTD_Monitor_unlock((void*)(ADDR_MStorageMC_Global + MSTORAGEMC_MONITOR_OFFSET)); // thread unlock

    if (res >= 0 && !found)
        LOG("mStorageMC::getFileSize: err not found name=%s, res=%d\n", name, result);

    return size;
}
