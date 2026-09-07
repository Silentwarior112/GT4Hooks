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

    These are all thin wrappers over the same STRP RPC client: each one loads
    the client global, sets a command number and tail-jumps to a shared packet
    builder. FileStream_acquire is command 6, the write-side open, and takes
    the same four arguments as FileStream_open below rather than the single
    pointer it was once declared with.
*/
extern int (*FileStream_acquire)(unsigned int byteOffset, int byteLength, int ringBytes, const char* iopPath);
extern void (*FileStream_flushWrite)(int handle, void* buffer, int size);
extern void (*FileStream_release)(int handle);

/*
    Reply to STRP command 1, which is a plain ioman::getStat on the IOP with
    the file's first eight bytes checked for the PDI compression header.

    The RPC wrapper copies a fixed 0x40 bytes into whatever it is handed, so
    this must stay 0x40 bytes long however few fields are actually named.
*/
typedef struct
{
    int Status;      /* 0 on success, 1 if the IOP could not stat the path */
    int Compressed;  /* 1 when the file starts with the PDI magic 0xFFF7EEC5 */
    int Size;        /* st_size, in bytes */
    int RealSize;    /* also st_size, or -(uncompressed size) when Compressed */
    int reserved[12];
} StrpStat;

/*
    STRP command 3: open a stream on the IOP and return its handle.

    The IOP picks its backend from the first byte of iopPath - an empty string
    means "read the disc by sector", anything else is opened through ioman,
    which PCSX2 serves from the host filesystem. byteOffset is a byte offset
    within whatever was opened, seeked to with SEEK_SET before every read.
*/
extern int (*FileStream_open)(unsigned int byteOffset, int byteLength, int ringBytes, const char* iopPath);

/*
    STRP command 1: stat a path on the IOP. Unlike command 3 this has no retry
    wrapper, so it is the only safe way to ask whether a file exists - see
    OpenStreamedFromHost in core/hooks/HostFs.c.
*/
extern void (*FileStream_stat)(StrpStat* out, const char* iopPath);

/*
    FileDeviceRo::pipe - the mode-0 arm of IOControlStream, and the only thing
    a .ads or .pss ever asks the device. Returns the command block on success,
    or NULL when the file has no volume entry. Called through the original
    address by the hook that replaces its vtable slot.
*/
extern void* (*PlayStation2_FileDeviceRo_pipe)(FileDeviceRo2* device, FileInternalStream* stream, IOControlStreamCommand0* cmd);
