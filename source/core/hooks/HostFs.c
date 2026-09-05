#include <stdbool.h>
#include <stdio.h>

#include "core/ps2/Memory.h"

#include "core/Target.h"
#include "core/game/IO.h"
#include "core/game/String.h"
#include "core/game/FileDevice.h"
#include "core/pdistd/FileDevice.h"
#include "core/pdistd/SceStructs.h"
#include "core/pdistd/SceEnums.h"
#include "core/util/String.h"
#include "core/Log.h"

#include "GameConfig.h"
#include "HostFs.h"

static const char HOST_DIR[] = HOSTFS_DIR;

/* Hook: HostFS
   Purpose: Hijack FileDeviceRo (the read-only volume reader) so the game reads
            loose files from the PCSX2 host filesystem instead of from its VOL.
   How:     Overrides the relevant file device virtual functions. The overrides
            mirror the original code and add the host path.
   Note:    Sound and movie files are deliberately NOT redirected. Those are
            DMA'd straight into IOP RAM over IRX RPC, which cannot be done from
            a host file descriptor - see IsStreamedFile below.
   Config:  HOSTFS_DIR for the folder, and GAME_HOSTFS_PREFIXES in the game's
            GameConfig.h for the paths that must stay on the disc.

   This is shared by every game: the two games' copies were identical apart
   from that prefix list, which is now a parameter.
*/

void HostFs_InstallHooks(void)
{
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo2_GetFileEntry, &HOOK_HostFs__PlayStation2_FileDeviceRo2_GetFileEntry);
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo_openStream, &HOOK_HostFs__PlayStation2_FileDeviceRo_openStream);
    HOOK(ADDR_PlayStation2_FileDeviceRo_rawReadStream, HOOK_HostFs__PlayStation2_FileDeviceRo_rawReadStream);
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo_closeStream, &HOOK_HostFs__PlayStation2_FileDeviceRo_closeStream);
    HOOK_FUNC_ADDR((void*)ADDR_PDISTD_FileDevice_readStream, &HOOK_HostFs__PDISTD_FileDevice_readStream);
}

int HOOK_HostFs__PDISTD_FileDevice_readStream(FileDevice* device, FileInternalStream* stream, MemorySpace* memSpace, int expand)
{
    char* fileName = PlaystationX_LockFileName(stream->FileObject->FileNameHandle);
    bool isStreamedFile = IsStreamedFile(fileName);

#if HOSTFS_PRINT && PRINT_HOSTFS_READS
    LOG("FileDeviceRo::readStream: %s\n", fileName);
#endif

    PlayStationX_UnlockFileName(stream->FileObject->FileNameHandle);

    int ret = 0;

    /* Streamed files must go through the real rawReadStream, so briefly restore
       the two instructions our jump hook overwrote. RAWREADSTREAM_INSN_0/1
       are the original prologue words, taken from the target's own code. */
    if (isStreamedFile)
    {
        *(int*)ADDR_PlayStation2_FileDeviceRo_rawReadStream = RAWREADSTREAM_INSN_0;
        *((int*)ADDR_PlayStation2_FileDeviceRo_rawReadStream + 1) = RAWREADSTREAM_INSN_1;
    }

    ret = device->VTable->rawReadStream(device, stream, memSpace);

    HOOK(ADDR_PlayStation2_FileDeviceRo_rawReadStream, HOOK_HostFs__PlayStation2_FileDeviceRo_rawReadStream);

    return ret;
}

/* Guessed function name - resolves a file's entry either from the VOL page
   manager (streamed files) or from the host filesystem (everything else). */
bool HOOK_HostFs__PlayStation2_FileDeviceRo2_GetFileEntry(FileDeviceRo2* device, FileStatus* status, char* fileName)
{
    if (IsStreamedFile(fileName))
    {
        VolumeEntryTypeInfo volFile = {0};

        PageManager* pageManager = &device->PageManager;
        PageManager_GetEntryForFile(&volFile, pageManager, fileName);

        if (volFile.Status >= 0)
        {
            status->Status = FileError_OK;
            status->Compressed = (volFile.EntryType >> 1) & 1;
            status->RealSize = volFile.RealSize;
            status->CompressedSize = volFile.CompressedSize;
            status->DataOffset = device->Ro.TocOffset
                   + *(int*)(pageManager->HeaderBuffer + 0x0C)
                   + volFile.PageOffset;

#if HOSTFS_PRINT
            LOG("GetVolumeFileInfo: pageManager compressed=%d, real_size=%x, comp_size=%x, data_offset=%x\n",
                status->Compressed, status->RealSize, status->CompressedSize, status->DataOffset);
#endif

            return true;
        }
    }
    else
    {
        char path[128];
        _sprintf(path, "host:%s/%s", HOST_DIR, fileName);

        struct sce_stat stat = {0};
        int res = sceGetStat(path, &stat);

        int err = sceStdioConvertError(0, res);

#if HOSTFS_PRINT
        LOG("FileDeviceRo::GetFileEntry: sceGetStat name=%s, err=%x, st_size=%x\n", path, err, stat.st_size);
#endif

        if (res >= 0)
        {
            status->Status = FileError_OK;
            status->Compressed = false;
            status->CompressedSize = stat.st_size;
            status->RealSize = stat.st_size;
            return true;
        }
    }

    return false;
}

int HOOK_HostFs__PlayStation2_FileDeviceRo_openStream(FileDeviceRo* device, FileInternalStream* stream)
{
    int res = -1;

    FileObject* fileObj = stream->FileObject;
    int fileMode = fileObj->FileMode;

    /* Lock name handle */
    char* fileName = PlaystationX_LockFileName(fileObj->FileNameHandle);

#if HOSTFS_PRINT
    LOG("FileDeviceRo::openStream: name=%s, mode=%d\n", fileName, fileMode);
#endif

    if (fileMode != 3)
    {
        if (fileMode <= 0)
        {
            FileStatus status = {0};

            /* Do NOT load these from host: they need to land in IOP memory via
               IRX RPC, which a host fd cannot do. */
            if (IsStreamedFile(fileName))
            {
                /* FileDeviceRo begins with FileDevicePipe, which begins with
                   FileDevice, so the downcast is just a reinterpretation. */
                device->Pipe.Base.VTable->readStat(&status, (FileDevice*)device, fileObj);
                fileObj->Status = status;

                if (fileObj->Status.Status)
                    goto err;

                res = device->Pipe.Base.VTable->getCdOffsetFromDataOffset(device, fileName, status.DataOffset, status.CompressedSize);
            }
            else
            {
                res = HandleHostFsOpen(device, stream, fileName, fileObj);
            }
        }

        if (res)
        {
            /* Expanders post-process a file once read (i.e. the inflater).
               FileDeviceRo's expander checks the stream's file object for the
               compressed flag before doing anything, so this is safe for the
               uncompressed loose files we serve. */
            PDISTD_FileDevice_setExpander(device, stream);

            /* Required for files we force-read from CDVD. */
            UnitArena* unit = UnitArenaBase_allocate((UnitArena*)ADDR_UnitArena_StreamState);
            unit->field_0x00 = res;
            unit->Offset = 0;
            unit->field_0x08 = 0;
            unit->field_0x0C = UnitArenaBase_allocate((UnitArena*)ADDR_UnitArena_StateField0C);
            stream->State = unit;
        }
        else
        {
            stream->Status = FileError_NOTFOUND;
        }
    }
    else
    {
        res = 0;
    }

err:
    /* Release name handle */
    PlayStationX_UnlockFileName(fileObj->FileNameHandle);
    return res;
}

int HandleHostFsOpen(FileDeviceRo* device, FileInternalStream* stream, char* fileName, FileObject* fileObj)
{
    FileStatus status = {0};

    device->Pipe.Base.VTable->readStat(&status, (FileDevice*)device, fileObj);
    fileObj->Status = status;

    if (fileObj->Status.Status == FileError_OK)
    {
        char pathToFile[128];
        _sprintf(pathToFile, "host:%s/%s", HOST_DIR, fileName);

        int fd = sceOpen(pathToFile, SCE_RDONLY);

#if HOSTFS_PRINT
        LOG("HandleHostFsOpen: name=%s, fd=%d\n", fileName, fd);
#endif
        if (fd >= 0)
        {
            /* Reuse the data offset field as a file descriptor. */
            fileObj->Status.DataOffset = fd;
            return 1; /* Good */
        }
    }
    else
    {
#if HOSTFS_PRINT
        LOG("HandleHostFsOpen: Failed, status isn't OK\n");
#endif
    }

    return 0;
}

int HOOK_HostFs__PlayStation2_FileDeviceRo_rawReadStream(FileDeviceRo* device, FileInternalStream* stream, MemorySpace* memSpace)
{
    /* Files read through hFileIO::read arrive in buffered 0x800 chunks. */
    int toRead = memSpace->BufferSize;
    void* outputPtr = memSpace->BufferPtr;

    int currentOffset = stream->State->Offset;
    int fileLength = stream->FileObject->Status.RealSize;
    int rem = fileLength - currentOffset;

    if (toRead > rem)
        toRead = rem;

    int fd = stream->FileObject->Status.DataOffset;

    int actuallyRead = sceRead(fd, outputPtr, toRead);

#if HOSTFS_PRINT && PRINT_HOSTFS_READS
    LOG("FileDeviceRo::rawReadStream: fd=%d, req=%x, read=%x, offset=%x, data=%x\n",
        fd, toRead, actuallyRead, currentOffset, *(int*)outputPtr);
#endif

    stream->State->Offset += actuallyRead;

    /* Return the number of bytes read - several callers use the return value. */
    return actuallyRead;
}

int HOOK_HostFs__PlayStation2_FileDeviceRo_closeStream(FileDeviceRo* device, FileInternalStream* stream)
{
    /* Work out whether this stream was served from the host BEFORE the state
       block is torn down, since that is what decides if there is an fd to close. */
    bool hostBacked = false;
    if (stream->FileObject)
    {
        char* fileName = PlaystationX_LockFileName(stream->FileObject->FileNameHandle);
        hostBacked = !IsStreamedFile(fileName);
        PlayStationX_UnlockFileName(stream->FileObject->FileNameHandle);
    }

    PDISTD_FileDevice_removeExpander(device, stream);

    if (stream->State)
    {
        if (stream->FileObject->FileMode == 1 && stream->State->field_0x08)
            FileStream_flushWrite(stream->State->field_0x00, stream->State->field_0x0C, 0x40 - stream->State->field_0x08);

        FileStream_release(stream->State->field_0x00);
        UnitArenaBase_free((void*)ADDR_UnitArena_StateField0C, stream->State->field_0x0C);
        UnitArenaBase_free((void*)ADDR_UnitArena_StreamState, stream->State);
        stream->State = NULL;
    }

#if HOSTFS_PRINT
    LOG("FileDeviceRo::closeStream (host=%d)\n", hostBacked);
#endif

    /* Close the host descriptor we stashed in Status.DataOffset.

       Note: the GT4 game tests `DataOffset == 0` here, which only ever closes
       fd 0 and leaks every real descriptor. The host IOP module hands out a
       small fixed number of descriptors, so leaking them eventually makes every
       further open fail. Close it when we know the file came from the host. */
    if (hostBacked && stream->FileObject && stream->FileObject->Status.DataOffset > 0)
    {
        sceClose(stream->FileObject->Status.DataOffset);
        stream->FileObject->Status.DataOffset = 0;
    }

    return 0;
}

/*
    Files that must keep streaming off the disc rather than the host.

    These are handed to PDISTR.IRX over RPC and read straight into IOP RAM, so
    an EE-side host file descriptor is no use to them.

    The extensions are the same five Gran Turismo 4 uses: Tourist Trophy links
    the same PDISTD sound and movie code, and the RPC ids (MPG1, MPG2, PBGM,
    STRP) are present in both.

    The engine-sound PREFIX is where the two games differ, and it matters.
    Both build the folder from the same two-entry table { "motosound",
    "carsound" }, indexed by a vehicle-class predicate. In GT4 that predicate is
    a stub that always returns zero, so GT4 only ever reaches "carsound/" - and
    the GT4 game only tests for that. In Tourist Trophy the predicate is real:
    it reads the vehicle's class and returns true for the two-wheel class, so a
    bike - which is very nearly all of Tourist Trophy - loads its engine sounds
    from "motosound/". Missing that prefix sends every bike engine sound to the
    host filesystem, where it cannot be DMA'd into IOP RAM, and engine audio
    fails. Both prefixes are reachable in Tourist Trophy, so both are excluded.

    Note that the sound ROOT also differs ("/sound_gt" became "/sound_tt"), but
    that must NOT be added here: everything under it that actually streams is
    already covered by the extension tests, and a "sound_tt/" prefix would
    wrongly pin non-streamed files there to the disc.
*/
bool IsStreamedFile(char* fileName)
{
    /* The prefix set is the one genuine per-game difference in this file, so it
       comes from the game - see GAME_HOSTFS_PREFIXES in GameConfig.h. The
       extensions below are the same across the whole family. */
    static const char* const prefixes[] = GAME_HOSTFS_PREFIXES;

    for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
        if (starts_with(fileName, prefixes[i]))
            return true;
    }

    char* ext = get_filename_ext(fileName);
    if (ext == NULL)
        return false;

    return !__strcmp(ext, "ins")
        || !__strcmp(ext, "ads")
        || !__strcmp(ext, "sqt")
        || !__strcmp(ext, "es")
        || !__strcmp(ext, "pss");
}
