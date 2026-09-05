#pragma once

#include "core/Target.h"
#include "core/pdistd/FileDevice.h"

extern void (*PageManager_GetEntryForFile)(VolumeEntryTypeInfo* retEntry, PageManager* manager, char* fileName);
extern void* (*UnitArenaBase_allocate)(UnitArena* ptr);
extern void* (*UnitArenaBase_free)(void*, UnitArena* ptr);

extern void (*PDISTD_FileStatus_FileStatus)(FileStatus* status);
extern void (*PDISTD_FileDevice_setExpander)(void* device, FileInternalStream* stream);
extern void (*PDISTD_FileDevice_removeExpander)(void* device, FileInternalStream* stream);

/*
    Helpers that FileDeviceRo::openStream / ::closeStream call around the
    UnitArena state block. Named for what they do rather than what Polyphony
    called them - the symbols are stripped in retail builds.
*/
extern void (*FileStream_acquire)(void* state);
extern void (*FileStream_flushWrite)(int handle, void* buffer, int size);
extern void (*FileStream_release)(int handle);
