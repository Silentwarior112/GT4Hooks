#pragma once

#include "core/pdistd/FileError.h"

/*
    PDISTD / PlayStation2 file device layout as used by Tourist Trophy.

    Tourist Trophy links the same PDISTD build as Gran Turismo 4 Online: the
    file device functions are instruction-identical between the two executables
    (they differ only in placement), so the field offsets below are shared with
    the GT4 game. They are kept as a separate copy rather than included from
    source/games/gt4o so that a Tourist Trophy specific difference can be expressed
    here without disturbing the GT4 build.
*/

typedef struct UnitArena
{
    int field_0x00;
    int Offset;
    int field_0x08;
    void* field_0x0C;
} UnitArena;

typedef struct FileStatus
{
    FileError Status;
    int Compressed;
    int RealSize;
    int CompressedSize;
    char* Buffer;

    /* Real disc/data offset of the file. The HostFS hook reuses this field to
       carry the host file descriptor for files served off the host filesystem. */
    int DataOffset;
} FileStatus;

typedef struct FileDeviceRo FileDeviceRo;

typedef struct FileObject
{
    int field_0;
    int field_4;
    int field_8;
    int field_C;
    int field_10;
    int field_14;
    int field_18;
    int field_1C;
    int field_20;
    int field_24;
    int field_28;
    int field_2C;
    int field_30;
    int field_34;
    FileDeviceRo* Device;
    int ListElement;
    int field_40;
    int field_44;
    /* ID of the name as a handle */
    int FileNameHandle;
    int Stream;
    int field_50;
    int field_54;
    int field_58;
    int field_5C;
    int field_60;
    int field_64;
    int field_68;
    int field_6C;
    int field_70;
    int FileMode;
    int field_78;
    int field_7C;
    int field_80;
    FileStatus Status;
    int field_9C;
    int field_A0;
    int DataOffset;
    int VTable;
} FileObject;

typedef struct MemorySpace
{
    void* BufferPtr;
    int BufferSize;
} MemorySpace;

////////////////////////////////////////////////
// Streams
///////////////////////////////////////////////
typedef struct FileInternalStream
{
    int Status;
    FileObject* FileObject;
    int unk3;
    UnitArena* State;
    int field_0x10;
    int field_0x14;
    int DefaultCallback;
    int field_0x1C;
    int vtable;
} FileInternalStream;

typedef struct FileInternalStreamDefault
{
    FileInternalStream Base;
    int unk;
    unsigned long long CurrentOffset;
    MemorySpace MemSpace;
    int field_0x38;
} FileInternalStreamDefault;

////////////////////////////////////////////////
// FileDeviceRo specifics
///////////////////////////////////////////////
typedef struct VolumeEntryTypeInfo
{
    int Status;
    int NodeID;
    int PageOffset;
    int field_C;
    long long ModifiedDate;
    int CompressedSize;
    int RealSize;
    char EntryType;
} VolumeEntryTypeInfo;

typedef struct PageManagerUnk
{
    int a;
    int b;
    int c;
    int d;
    int e;
} PageManagerUnk;

typedef struct PageManager
{
    void* vtable;
    char* HeaderBuffer;
    int field_8;
    int RoInflator;
    PageManagerUnk field_0x10;
} PageManager;

////////////////////////////////////////
// File Devices
////////////////////////////////////////
typedef struct FileDevice FileDevice;

typedef void* (*IOControlStream_cb)(void*, FileInternalStream*, void*);
typedef void* (*readStat_cb)(FileStatus*, FileDevice*, FileObject*);
typedef int (*getCdOffsetFromDataOffset_cb)(FileDeviceRo*, char*, int, int);
typedef int (*rawReadStream_cb)(FileDevice*, FileInternalStream*, MemorySpace*);

typedef struct FileDeviceVT
{
    char fields[0x9C];
    rawReadStream_cb rawReadStream;
    void* field_A0;
    void* readStream;
    void* field_A8;
    void* seekStream;
    void* field_B0;
    void* tellStream;
    void* field_B8;
    void* writeStream;
    void* field_C0;
    IOControlStream_cb IOControlStream;
    void* field_C8;
    readStat_cb readStat;
    void* field_D0;
    getCdOffsetFromDataOffset_cb getCdOffsetFromDataOffset;
} FileDeviceVT;

typedef struct FileDevice
{
    char fields[0xA4];
    FileDeviceVT* VTable;
} FileDevice;

typedef struct FileDevicePipe
{
    FileDevice Base;
    int field_0xA8;
    int MountPath;
} FileDevicePipe;

typedef struct FileDeviceRo
{
    FileDevicePipe Pipe;
    int UnkBool;
    char* VolPath;
    int TocOffset;
    int TocPageOffset;
} FileDeviceRo;

typedef struct FileDeviceRo2
{
    struct FileDeviceRo Ro;
    int field_0xC0;
    PageManager PageManager;
    int CustomPageOffset;
    int TopFileDevice;
    void* PtrToSelf;
} FileDeviceRo2;

/////////////////////////////////
// IOCTL
/////////////////////////////////
/*
    Mode 0 asks the device where a file lives, for the benefit of an IOP RPC
    server that will open it itself. FileDeviceRo::pipe fills it in from a
    readStat, and it is the only thing .ads and .pss ever ask the device - both
    open with fileMode 3, take this answer, close the stream, and hand the three
    fields straight to PBGM or MPG.

    Name is the interesting field. It is set to "" when the device is reading a
    real disc, and only filled with a path when it is not - which is the same
    "empty means read the disc by sector" convention the IOP uses everywhere.
*/
typedef struct IOControlStreamCommand0
{
    int  Mode;       /* in : 0 */
    int  Sector;     /* out: FileStatus.DataOffset, an LBA in 2048-byte sectors */
    int  Size;       /* out: FileStatus.RealSize, in bytes */
    char Name[0x70]; /* out: "" for the disc, else a path for the IOP to open */
} IOControlStreamCommand0;

typedef struct IOControlStreamCommand1
{
    int Mode;
    void* Buffer;
    int Size;
    int Unused;
} IOControlStreamCommand1;

typedef struct IOControlStreamCommand2
{
    int Mode;
    void* UnkPtr;
    int Size;
    int Unused;
} IOControlStreamCommand2;
