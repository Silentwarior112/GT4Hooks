#pragma once

/*
    Gran Turismo 4 Online (USA)
    Boot ELF : SCUS_974.36
    Build ID : SCUS-97436   (GTImageLoader RSA exponent 81001)

    Every Gran Turismo 4 address literal lives here. The rest of
    source/gt4 is build-independent, so a second GT4 region needs a new
    header like this one and nothing else.

    These were lifted out of the GameFunctions headers by
    tools/extract_gt4_targets.py; they were originally reverse
    engineered for GT4Hooks against this exact executable.
*/

/* --- libc, sce file I/O and the PlayStationX name-handle helpers */
/*     was GameFunctions/IO.h */
#define ADDR_sprintf                     0x50F1D8
#define ADDR_print                       0x50F120
#define ADDR_sceOpen                     0x5DF338
#define ADDR_sceClose                    0x5DF5C8
#define ADDR_sceRead                     0x5DF980
#define ADDR_sceGetStat                  0x5E0F20
#define ADDR_sceStdioConvertError        0x5E5AF0
#define ADDR_sceLSeek                    0x5DF740
#define ADDR_sceMcGetDir                 0x5B9620
#define ADDR_sceMcSync                   0x5B9310
#define ADDR_PlaystationX_LockFileName   0x467060
#define ADDR_PlayStationX_UnlockFileName 0x467080

/* --- the game's own string routines */
/*     was GameFunctions/String.h */
#define ADDR_strlen 0x511040
#define ADDR_strcmp 0x510F40
#define ADDR_strstr 0x5D80C0
#define ADDR_strcpy 0x5D7394

/* --- PDISTD / PlayStation2 file device - vtable slots and functions */
/*     was GameFunctions/FileDevice.h */
#define ADDR_PlayStation2_FileDeviceRo_openStream       0x6D9694
#define ADDR_PlayStation2_FileDeviceRo2_GetFileEntry    0x6D96E4
#define ADDR_PlayStation2_FileDeviceRo2_readStat        0x6D96CC
#define ADDR_PDISTD_FileDevice_readStream               0x6D96A4
#define ADDR_PlayStation2_FileDeviceRo_rawReadStream    0x466288
#define ADDR_PlayStation2_FileDeviceRo_closeStream      0x6D968C
#define ADDR_PlayStation2_FileDeviceRo2_IOControlStream 0x4665C0
#define ADDR_PlayStation2_FileDeviceRo_pipe             0x466CB0
#define ADDR_PDISTD_FileInternalStream_ioctl            0x464CD0
#define ADDR_PDISTD_FileInternalStream_getDevice        0x464D28
#define ADDR_UnitArenaBase_allocate                     0x528580
#define ADDR_UnitArenaBase_free                         0x5285B0
#define ADDR_PageManager_GetEntryForFile                0x4613C0
#define ADDR_PDISTD_FileStatus_FileStatus               0x464968
#define ADDR_PDISTD_FileDevice_setExpander              0x462FD0
#define ADDR_PDISTD_FileDevice_removeExpander           0x463020

/* --- PDISTD::Monitor thread locks */
/*     was GameFunctions/Monitor.h */
#define ADDR_PDISTD_Monitor_lock   0x522E78
#define ADDR_PDISTD_Monitor_unlock 0x522EE8

/* --- mStorageMC - memory card storage */
/*     was GameFunctions/MStorage.h */
#define ADDR_mStorageMC_getFileSize 0x3DF4D0

/* --- MCarGarage / CarGarage */
/*     was GameFunctions/MCarGarage.h */
#define ADDR_QUICK_MENU_GetGTPerformanceIndex 0x10D598
#define ADDR_MCarGarage_MCarGarage            0x12A008
#define ADDR_MCarGarage_dtor                  0x129FB0
#define ADDR_mCarGarage_getCarGarage          0x133F28

/* --- CameraSys */
/*     was GameFunctions/CameraSys.h */
#define ADDR_CameraSys_CameraOnBoard_GetCameraMountIndex 0x23F438

/* --- HOutput - the debug output class stripped in release builds */
/*     was GameFunctions/HOutput.h */
#define ADDR_HOutput_Handler 0x67A784
#define ADDR_ADHOC_debug     0x626088
#define ADDR_ADHOC_printf    0x6259D8

/* --- ADHOC scripting runtime */
/*     was GameFunctions/Adhoc.h */
#define ADDR_HClassInit_HClassInit   0x4E2940
#define ADDR_HClassInit_CreateClass  0x4E28C8
#define ADDR_HInt_HInt               0x4E9B00
#define ADDR_HInt_dtor               0x4E7FD8
#define ADDR_HFloat_HFloat           0x4E6468
#define ADDR_HFloat_dtor             0x4E4B98
#define ADDR_HValue_dtor             0x5071C8
#define ADDR_ADHOC_Deallocate        0x508B58
#define ADDR_hObject_GetClassID      0x4F33E8
#define ADDR_hClass_setSuperClassID  0x4E27B8
#define ADDR_RefCounter_ref          0x50C0D0
#define ADDR_RefCounter_unref        0x50C138
#define ADDR_hModule_defineFunction  0x4F0650
#define ADDR_hModule_defineMethod    0x4F0B10
#define ADDR_hModule_defineStatic    0x4F0830
#define ADDR_hModule_defineAttribute 0x4F0C00
#define ADDR_hModule_defineClass     0x4EFC00

/* --- PDISTD::FontManagerClass */
/*     was GameFunctions/FontManagerClass.h */
#define ADDR__PDISTD_FontManagerClass_putstring 0x33B740
#define ADDR__PDISTD_FontManagerClass_setRegion 0x33B418
#define ADDR__PDISTD_FontManagerClass_setFont   0x33AF90
#define ADDR__PDISTD_FontManagerClass_printf    0x33B6B0

/* --- miscellaneous engine entry points */
/*     was GameFunctions/Misc.h */
#define ADDR_GranTurismo4_RenderManager_call     0x100798
#define ADDR_GranTurismo4_GameObjectPS2_render   0x100848
#define ADDR_GranTurismo4_GameObjectManager_call 0x354360

/* --- hook sites patched with MAKE_JAL */
/*     was MCarGarage.c */
#define ADDR_MCarGarage_sortByDisplacement_register 0x132E60

/* --- hook sites patched with MAKE_JAL.
       These are CALL SITES inside a function, not function entry points - do
       not "fix" them into HOOK targets. The two CameraSys ones are 12 bytes
       apart, which is call spacing rather than two separate functions. */
#define ADDR_ADHOC_LastDefineClass_JAL         0x121D58
#define ADDR_CameraSys_GetCameraMountIndex_JAL 0x23F438
#define ADDR_CameraSys_GetCamOffset_JAL        0x23F444

/* --- UnitArenaBase globals backing FileInternalStream::State */
#define ADDR_UnitArena_StreamState  0x885340
#define ADDR_UnitArena_StateField0C 0x885360

/* --- stream helpers called by FileDeviceRo::openStream / ::closeStream */
#define ADDR_FileStream_acquire    0x51B370
#define ADDR_FileStream_flushWrite 0x51B478
#define ADDR_FileStream_release    0x51B4A8

/* --- mStorageMC. The monitor is a member at +0x34 of the global, not a
       separate object. */
#define ADDR_MStorageMC_Global    0x680F6C
#define MSTORAGEMC_MONITOR_OFFSET 52

/* --- std::basic_string internals used by the ADHOC string boilerplate.
       The first is the shared empty-representation object; the other three are
       fields inside it (+0x08 refcount, +0x0C flag, +0x10 data). */
#define ADDR_std_string_EmptyRep          0x6A4168
#define ADDR_std_string_EmptyRep_Refcount 0x6A4170
#define ADDR_std_string_EmptyRep_Flag     0x6A4174
#define ADDR_std_string_EmptyRep_Data     0x6A4178
#define ADDR_std_string_Rep_M_grab        0x5EEB00
#define ADDR_std_string_replace           0x5EEBC0

/* --- the first two instruction words of FileDeviceRo::rawReadStream.
       readStream restores these to briefly un-hook the function so streamed
       files still reach the original implementation. */
#define RAWREADSTREAM_INSN_0 0x27BDFFB0
#define RAWREADSTREAM_INSN_1 0xFFB70038

/* --- injection parameters.

       GT4 Online links its injected code over the static globals of the Omron
       wnn/fep Japanese IME, which this build links in but never calls:
       LoadFep (0x1058D0) has no xrefs at all, so nothing below it ever runs.
       That leaves 0x68A480..0x69A790 reliably dead.

       Before main(), the game hands a pool allocator everything from the start
       of .bss (0x8D0FB0) up to the top of RAM and zero-fills it. The plugin
       sits well below that, so the clear cannot reach it; the memset call is
       NOPed anyway as belt-and-braces, which is what GLOBAL_MEMSET_OFFSET is
       for. Keep PLUGIN_BASE_ADDRESS in step with BASE_US in game.mk. */
#define GLOBAL_MEMSET_OFFSET 0x5225F0
#define MEMORY_SIZE_OFFSET   0x681970
#define PLUGIN_BASE_ADDRESS  0x68A480
#define PLUGIN_RESERVE_BYTES 0x10310
#define POOL_SETUP_FUNC      0x5225B0
#define POOL_BASE_VALUE      0x8D0FB0

/* PolyphonyGL's 174 entry points are bulky enough to keep separate. */
#include "SCUS-97436.pgl.h"
