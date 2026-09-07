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
   Note:    Sound and movie files take a different route. Their data is DMA'd
            into IOP RAM and on into SPU2 sound RAM, so an EE file descriptor
            is no use to them - but the IOP can open a host path itself, so
            they are served by handing the IOP the path instead of a sector.
            See IsStreamedFile and OpenStreamedFromHost below.
   Config:  HOSTFS_DIR for the folder, HOSTFS_STREAMED to turn the streamed
            route off and keep those files on the disc, and
            GAME_HOSTFS_PREFIXES in the game's GameConfig.h for the paths that
            take it.

   This is shared by every game: the two games' copies were identical apart
   from that prefix list, which is now a parameter.
*/

/*
    Say so, unmissably, when a streamed file could not be served from the host
    and is being read from the disc image instead.

    This is not an error and the game plays fine, which is exactly the problem:
    a silent fallback makes an edited file simply do nothing, and "nothing
    changed" is indistinguishable from "the hook never ran". That ambiguity is
    worth a loud banner.

    Split across several WARN calls on purpose - each one formats into a 256
    byte local, and a single banner plus a long VOL path would not fit.
*/
static void WarnDiscFallback(const char* fileName, const char* reason)
{
    WARN("\n**************** READING FROM DISC, NOT HOST ****************\n");
    WARN("****  file:   %s\n", fileName);
    WARN("****  reason: %s\n", reason);
    WARN("************************************************************\n");
}

void HostFs_InstallHooks(void)
{
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo2_GetFileEntry, &HOOK_HostFs__PlayStation2_FileDeviceRo2_GetFileEntry);
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo_openStream, &HOOK_HostFs__PlayStation2_FileDeviceRo_openStream);
    HOOK(ADDR_PlayStation2_FileDeviceRo_rawReadStream, HOOK_HostFs__PlayStation2_FileDeviceRo_rawReadStream);
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo_closeStream, &HOOK_HostFs__PlayStation2_FileDeviceRo_closeStream);
    HOOK_FUNC_ADDR((void*)ADDR_PDISTD_FileDevice_readStream, &HOOK_HostFs__PDISTD_FileDevice_readStream);
#if HOSTFS_IOPSTREAM
    HOOK_FUNC_ADDR((void*)ADDR_PlayStation2_FileDeviceRo_pipe_VTSlot, &HOOK_HostFs__PlayStation2_FileDeviceRo_pipe);
#endif
}

#if HOSTFS_IOPSTREAM
/*
    Hook: the device's mode-0 ioctl, which is how .ads and .pss get served.

    Neither of those ever reaches openStream - both open with fileMode 3, which
    that hook declines by design. Instead each asks the device exactly one
    question through this function, closes the stream, and hands the answer
    straight to an IOP RPC server: PBGM for .ads, MPG for .pss.

    The answer is three fields - a sector, a byte size, and a name - and the
    game already knows how to put a real path in the name. It writes "" there
    when the device is backed by a disc and builds a path when it is not, which
    is the same "empty means read the disc by sector" convention pdistr uses on
    every one of its RPC servers. Both servers then pick their backend from the
    first byte of that name, exactly as the STRP open does for engine sounds.

    So there is no new RPC plumbing here. Filling the name in IS the redirect.

    Two fields have to be right, for the same reasons as OpenStreamedFromHost:
      - Sector must be 0. Both servers shift it left by 11 and seek there, and
        a loose file starts at its own byte zero.
      - Size must be the loose file's own size, not the volume entry's, which
        is what the original just wrote.

    And the same pre-flight, for the same reason: both opens set libpdi's retry
    flag, so a path that is not there is retried forever rather than failing.
    For .ads that costs the background-music thread; for .pss the EE is blocked
    in the call and the machine stops. Never hand over a path we have not
    stat'd.

    Note .pss only: the IOP reads a whole number of 2048-byte sectors, so a
    loose file that is not a multiple of 2048 has up to 2047 bytes of stale
    buffer appended. The demuxer stops on the bitstream rather than on a length,
    so this is harmless in practice - but padding the file with zeros makes it
    exact. .ads carries an exact byte count and needs no padding.
*/
void* HOOK_HostFs__PlayStation2_FileDeviceRo_pipe(FileDeviceRo2* device, FileInternalStream* stream, IOControlStreamCommand0* cmd)
{
    /* Let the game answer first. The hook replaces a vtable word rather than
       the code, so the original is still reachable at its own address, and on
       the disc path nothing below changes what it wrote. */
    void* res = PlayStation2_FileDeviceRo_pipe(device, stream, cmd);

    if (stream->FileObject == NULL)
        return res;

    char iopPath[128];
    bool wanted = false;

    char* fileName = PlaystationX_LockFileName(stream->FileObject->FileNameHandle);
    {
        char* ext = get_filename_ext(fileName);

        wanted = ext != NULL && (!__strcmp(ext, "ads") || !__strcmp(ext, "pss"));

        if (wanted)
        {
            /* The stored name is mount-relative, so it should not carry a
               leading separator - every name in the log agrees. Skip one
               anyway rather than build "host:dir//mpeg/...". */
            char* rel = (fileName[0] == '/') ? fileName + 1 : fileName;
            _sprintf(iopPath, "host:%s/%s", HOST_DIR, rel);
        }
    }
    PlayStationX_UnlockFileName(stream->FileObject->FileNameHandle);

    if (!wanted)
        return res;

    if (__strlen(iopPath) > STREAMED_IOP_PATH_MAX)
    {
        WarnDiscFallback(iopPath, "host path is longer than the RPC packet allows");
        return res;
    }

    StrpStat st = {0};
    FileStream_stat(&st, iopPath);

#if HOSTFS_PRINT
    LOG("FileDeviceRo::pipe: stat name=%s, status=%d, compressed=%d, size=%x\n",
        iopPath, st.Status, st.Compressed, st.Size);
#endif

    if (st.Status != 0)
    {
        WarnDiscFallback(iopPath, "no such file under the host directory");
        return res;
    }

    if (st.Size <= 0)
    {
        WarnDiscFallback(iopPath, "host file is empty");
        return res;
    }

    if (st.Compressed)
    {
        WarnDiscFallback(iopPath, "host file is still PDI-compressed - re-extract it decompressed");
        return res;
    }

    cmd->Sector = 0;
    cmd->Size = st.Size;
    __strcpy(cmd->Name, iopPath);

#if HOSTFS_PRINT
    LOG("FileDeviceRo::pipe: serving %s from the host, size=%x\n", iopPath, st.Size);
#endif

    /* If the file had no volume entry the original returned NULL without
       writing anything, and its callers skip the RPC on NULL. We have now
       filled in every field, so report success - which also means a file that
       exists only on the host still plays. */
    return cmd;
}
#endif

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

            /* These cannot be served by an EE-side file descriptor: their data
               is DMA'd into IOP RAM and on into SPU2 sound RAM. But the IOP can
               open them itself - see OpenStreamedFromHost - so try that first
               and fall back to the disc. Host first, because a file that was
               added or replaced need not exist in the VOL at all. */
            if (IsStreamedFile(fileName))
            {
#if HOSTFS_STREAMED
                res = OpenStreamedFromHost(fileName, fileObj);
#else
                res = 0;
                WarnDiscFallback(fileName, "streamed host reads are off (HOSTFS_STREAMED=0)");
#endif

                if (!res)
                {
                    /* FileDeviceRo begins with FileDevicePipe, which begins with
                       FileDevice, so the downcast is just a reinterpretation. */
                    device->Pipe.Base.VTable->readStat(&status, (FileDevice*)device, fileObj);
                    fileObj->Status = status;

                    if (fileObj->Status.Status)
                        goto err;

                    res = device->Pipe.Base.VTable->getCdOffsetFromDataOffset(device, fileName, status.DataOffset, status.CompressedSize);
                }
            }
            else
            {
                res = HandleHostFsOpen(device, stream, fileName, fileObj);
            }
        }
        else
        {
            /* A write stream. FileDeviceRo is the read-only device, neither
               game has been seen opening one, and there is no host-side
               implementation for it - retail would take the FileStream_acquire
               path here. Leave res at its -1 error value so callers that test
               for a negative return still see a failure, and make sure it does
               not reach the state block below. */
            WARN("HostFS: write stream is not served from the host: mode=%d name=%s\n",
                 fileMode, fileName);
        }

        /* Only a POSITIVE result is a stream.

           This test used to be a plain `if (res)`, which was wrong twice over.
           HandleHostFsOpen reports success by returning the literal 1 rather
           than a handle, and an unhandled file mode left res at its -1
           initialiser - and both sailed through, installing 1 or -1 in the
           state block as though it were an IOP stream handle. closeStream then
           handed that to the IOP as a release. */
        if (res > 0)
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
        else if (res == 0)
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

            /* 1 is a sentinel, NOT a handle: the descriptor lives in
               Status.DataOffset above and the reads go through sceRead, so
               there is no IOP stream here for closeStream to release. Callers
               must treat this as "opened", never as something to hand back to
               the IOP - see the res > 0 test in openStream. */
            return 1;
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

/*
    Open a streamed file directly from the host filesystem.

    Engine sounds and the .ins/.es sample banks never pass through the EE at
    all: pdistr.irx reads them and DMAs them onward into SPU2 sound RAM, which
    is why an EE-side descriptor is no use and why these files were left on the
    disc. The descriptor has to live on the IOP - and the STRP open will happily
    put it there. It picks its backend from the first byte of the path it is
    given: an empty string means "read the disc by sector", and anything else is
    opened through ioman, which PCSX2 serves from the host filesystem using the
    same root it resolves "host:" against for the EE. Nothing on the IOP side
    cares that the file used to live inside a VOL.

    Two values have to be right, and neither is the path:

      - The offset must be zero. It is a byte offset that the IOP seeks to with
        SEEK_SET before every read burst; for a VOL-backed file the game passes
        the file's offset within the volume, but a loose file starts at its own
        byte zero.

      - The length must be exact. The read loop never checks for end of file,
        it simply reads until it has consumed that many bytes, so too small
        starves the consumer and too large appends garbage.

    Returns the stream handle, or 0 to fall back to the disc.
*/
int OpenStreamedFromHost(char* fileName, FileObject* fileObj)
{
    char iopPath[128];
    _sprintf(iopPath, "host:%s/%s", HOST_DIR, fileName);

    int pathLen = __strlen(iopPath);

    if (pathLen > STRP_PATH_MAX)
    {
        WarnDiscFallback(fileName, "host path is longer than the RPC packet allows");
#if HOSTFS_PRINT
        LOG("OpenStreamedFromHost: path is %d chars, limit is %d: %s\n", pathLen, STRP_PATH_MAX, iopPath);
#endif
        return 0;
    }

    /* Ask before opening, and never open blind. The STRP open sets libpdi's
       retry flag, so a path that is not there does not fail - the IOP retries
       the open every millisecond forever on its RPC server thread, and the EE
       blocks in the call that is waiting for it. The stat command reaches
       ioman::getStat with no such wrapper, so it is safe on a missing path and
       is the only reason this hook cannot hang the game. */
    StrpStat st = {0};
    FileStream_stat(&st, iopPath);

#if HOSTFS_PRINT
    LOG("OpenStreamedFromHost: stat name=%s, status=%d, compressed=%d, size=%x\n",
        iopPath, st.Status, st.Compressed, st.Size);
#endif

    if (st.Status != 0)
    {
        WarnDiscFallback(fileName, "no such file under the host directory");
        return 0;
    }

    if (st.Size <= 0)
    {
        WarnDiscFallback(fileName, "host file is empty");
        return 0;
    }

    /* A file that still carries the PDI header holds compressed bytes, and
       nothing on this path expands them - the expander the EE would normally
       attach is bypassed, so the IOP would push them into sound RAM as they
       are. Leave it on the disc rather than play noise, and say so: it means
       the extracted copy needs re-extracting decompressed. */
    if (st.Compressed)
    {
        WarnDiscFallback(fileName, "host file is still PDI-compressed - re-extract it decompressed");
        return 0;
    }

    int handle = FileStream_open(0, st.Size, STRP_RING_BYTES, iopPath);

#if HOSTFS_PRINT
    LOG("OpenStreamedFromHost: open name=%s, handle=%x\n", iopPath, handle);
#endif

    if (!handle)
    {
        WarnDiscFallback(fileName, "the IOP refused the stream open");
        return 0;
    }

    /* Stand in for the VOL entry the caller would otherwise have read. Written
       as a whole struct, the way both other paths here do it, so no field is
       left holding a value from a previous open. The file is loose and whole,
       so both sizes are the real size, and there is no descriptor to carry in
       DataOffset - streamed files read through the handle above, not through
       rawReadStream. */
    FileStatus status = {0};
    status.Status = FileError_OK;
    status.Compressed = false;
    status.RealSize = st.Size;
    status.CompressedSize = st.Size;
    status.DataOffset = 0;
    fileObj->Status = status;

    return handle;
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
        /* field_0x00 only holds a real IOP stream handle for the streamed
           files. For everything else HandleHostFsOpen put its sentinel 1 there,
           and releasing that sends the IOP a command 2 for a handle it never
           issued - which pdistr then dereferences, reading from address 0x8D.
           That fired on every ordinary file close, dozens of times per load.
           hostBacked is exactly the right test: it is false only for the
           streamed files, which are the only ones with a handle to release. */
        if (!hostBacked)
        {
            if (stream->FileObject && stream->FileObject->FileMode == 1 && stream->State->field_0x08)
                FileStream_flushWrite(stream->State->field_0x00, stream->State->field_0x0C, 0x40 - stream->State->field_0x08);

            FileStream_release(stream->State->field_0x00);
        }

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
    Files that stream through PDISTR.IRX rather than through an EE descriptor.

    These are handed to PDISTR.IRX over RPC and read straight into IOP RAM, so
    an EE-side host file descriptor is no use to them. That does not mean they
    have to come off the disc - OpenStreamedFromHost gives the IOP a host path
    so it can open them itself - but they must never take the ordinary
    sceOpen/sceRead route the rest of this file uses, which is what this test
    is for. Note that .ads and .pss open with file mode 3 and so never reach
    openStream at all - they are served instead by the mode-0 ioctl hook, which
    still needs them to test true here so that GetFileEntry answers them from
    the volume rather than from an EE descriptor.

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
