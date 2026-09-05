#pragma once

#include <stdbool.h>

#include "core/pdistd/FileDevice.h"

/*
    Set this to the folder that holds the loose, extracted game files.
    It is resolved relative to the PCSX2 host filesystem root, i.e. the
    directory the ELF was booted from. See HostFs.md.
*/
#ifndef HOSTFS_DIR
#define HOSTFS_DIR "VOL_extract"
#endif

void HostFs_InstallHooks(void);

bool IsStreamedFile(char* fileName);

int HandleHostFsOpen(FileDeviceRo* device, FileInternalStream* stream, char* fileName, FileObject* fileObj);

bool HOOK_HostFs__PlayStation2_FileDeviceRo2_GetFileEntry(FileDeviceRo2* device, FileStatus* status, char* fileName);
int  HOOK_HostFs__PlayStation2_FileDeviceRo_openStream(FileDeviceRo* device, FileInternalStream* stream);
int  HOOK_HostFs__PlayStation2_FileDeviceRo_rawReadStream(FileDeviceRo* device, FileInternalStream* stream, MemorySpace* memSpace);
int  HOOK_HostFs__PlayStation2_FileDeviceRo_closeStream(FileDeviceRo* device, FileInternalStream* stream);
int  HOOK_HostFs__PDISTD_FileDevice_readStream(FileDevice* device, FileInternalStream* stream, MemorySpace* memSpace, int expand);
