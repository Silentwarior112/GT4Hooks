#include "MStorage.h"

int (*mStorageMC_getFileSize)() = (void*)ADDR_mStorageMC_getFileSize;

int (*mcGetDir)(int port, int slot, const char* name, unsigned int mode,
                int maxent, sceMcTblGetDir* table) = (void*)ADDR_mcGetDir;

int (*mcRead)(int index, const char* name, void* buf, int len, int offset) = (void*)ADDR_mcRead;

int (*mcWrite)(int index, const char* name, const void* buf, int len, int offset) = (void*)ADDR_mcWrite;