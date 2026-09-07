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

/*
    Serve streamed files (engine sounds and the .ins/.es sample banks) from the
    host filesystem too, by having the IOP open them itself rather than reading
    them off the disc. Set to 0 to keep those on the disc, which is the older
    and more conservative behaviour. See OpenStreamedFromHost in HostFs.c.
*/
#ifndef HOSTFS_STREAMED
#define HOSTFS_STREAMED 1
#endif

/*
    The STRP RPC send window is 0x80 bytes with the path at packet+0x14, and
    the copy into it is an unbounded strcpy into a 0xC0-byte client global, so
    a longer path corrupts memory rather than failing.
*/
#define STRP_PATH_MAX 107

/* Ring buffer the IOP allocates per stream. What the game itself passes. */
#define STRP_RING_BYTES 0x8000

/*
    Serve .ads and .pss from the host too. These never reach openStream - they
    open with fileMode 3 - so they go through a second hook on the device's
    mode-0 ioctl instead. Set to 0 to leave them on the disc.
*/
#ifndef HOSTFS_IOPSTREAM
#define HOSTFS_IOPSTREAM 1
#endif

/*
    Longest path that fits in the mode-0 answer, and therefore in whichever RPC
    packet consumes it. Both name fields sit inside an 0x80 send window and both
    copies are unbounded: PBGM's name is at packet+0x10 (0x70 bytes), MPG's at
    packet+0x0C (0x74). The mode-0 handler cannot know which server will read
    its answer, so the smaller of the two governs. Overrunning MPG's would run
    into the MPG2 client's own RPC data at 0x776C80.
*/
#define STREAMED_IOP_PATH_MAX 111

void HostFs_InstallHooks(void);

bool IsStreamedFile(char* fileName);

int HandleHostFsOpen(FileDeviceRo* device, FileInternalStream* stream, char* fileName, FileObject* fileObj);

int OpenStreamedFromHost(char* fileName, FileObject* fileObj);

void* HOOK_HostFs__PlayStation2_FileDeviceRo_pipe(FileDeviceRo2* device, FileInternalStream* stream, IOControlStreamCommand0* cmd);

bool HOOK_HostFs__PlayStation2_FileDeviceRo2_GetFileEntry(FileDeviceRo2* device, FileStatus* status, char* fileName);
int  HOOK_HostFs__PlayStation2_FileDeviceRo_openStream(FileDeviceRo* device, FileInternalStream* stream);
int  HOOK_HostFs__PlayStation2_FileDeviceRo_rawReadStream(FileDeviceRo* device, FileInternalStream* stream, MemorySpace* memSpace);
int  HOOK_HostFs__PlayStation2_FileDeviceRo_closeStream(FileDeviceRo* device, FileInternalStream* stream);
int  HOOK_HostFs__PDISTD_FileDevice_readStream(FileDevice* device, FileInternalStream* stream, MemorySpace* memSpace, int expand);
