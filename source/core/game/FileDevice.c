#include "FileDevice.h"

void (*PageManager_GetEntryForFile)(VolumeEntryTypeInfo* retEntry, PageManager* manager, char* fileName) = (void*)ADDR_PageManager_GetEntryForFile;
void* (*UnitArenaBase_allocate)(UnitArena* ptr) = (void*)ADDR_UnitArenaBase_allocate;
void* (*UnitArenaBase_free)(void*, UnitArena* ptr) = (void*)ADDR_UnitArenaBase_free;

void (*PDISTD_FileStatus_FileStatus)(FileStatus* status) = (void*)ADDR_PDISTD_FileStatus_FileStatus;
void (*PDISTD_FileDevice_setExpander)(void* device, FileInternalStream* stream) = (void*)ADDR_PDISTD_FileDevice_setExpander;
void (*PDISTD_FileDevice_removeExpander)(void* device, FileInternalStream* stream) = (void*)ADDR_PDISTD_FileDevice_removeExpander;

int (*FileStream_acquire)(unsigned int byteOffset, int byteLength, int ringBytes, const char* iopPath) = (void*)ADDR_FileStream_acquire;
void (*FileStream_flushWrite)(int handle, void* buffer, int size) = (void*)ADDR_FileStream_flushWrite;
void (*FileStream_release)(int handle) = (void*)ADDR_FileStream_release;

int (*FileStream_open)(unsigned int byteOffset, int byteLength, int ringBytes, const char* iopPath) = (void*)ADDR_FileStream_open;
void (*FileStream_stat)(StrpStat* out, const char* iopPath) = (void*)ADDR_FileStream_stat;

void* (*PlayStation2_FileDeviceRo_pipe)(FileDeviceRo2* device, FileInternalStream* stream, IOControlStreamCommand0* cmd) = (void*)ADDR_PlayStation2_FileDeviceRo_pipe;
