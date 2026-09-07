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

/* --- STRP RPC wrappers used to stream a loose file straight into the IOP.
       See OpenStreamedFromHost in core/hooks/HostFs.c. Both sit next to the
       helpers above: command 3 (open) is acquire - 0x30, command 1 (stat) is
       release + 0x58. Verified by disassembly, not by that arithmetic. */
#define ADDR_FileStream_open 0x51B340
#define ADDR_FileStream_stat 0x51B500

/* --- the FileDeviceRo2 vtable word holding FileDeviceRo::pipe, hooked to
       redirect .ads and .pss. A DATA address (the function-pointer word), like
       the openStream slot - the function itself stays at ADDR_..._pipe. */
#define ADDR_PlayStation2_FileDeviceRo_pipe_VTSlot 0x6D96DC

/* --- the memory-card manager.

       ADDR_MC_Manager_Ptr is a POINTER to the manager, not the manager itself,
       and it is null until the game constructs it. The monitor is a member at
       +0x34 of what it POINTS AT. Every memory-card entry point in the game
       does the same two steps - see mcWrite 0x515990:

           0x5159CC  lw    $a0, ($s0)     ; s0 = 0x680F6C
           0x5159D0  jal   0x522E78       ; Monitor_lock
           0x5159D4  addiu $a0, $a0, 0x34

       Locking 0x680F6C + 0x34 instead - the address OF the pointer - waits on
       an unrelated global and hangs the game. */
#define ADDR_MC_Manager_Ptr    0x680F6C
#define MC_MANAGER_MONITOR_OFFSET 0x34

/* An mStorageMC holds its storage index at +0x10. mStorageMC::read 0x3DF4D8
   does `lw $a0,0x10($a0)` / `addiu $a0,$a0,-1` and passes the result as ONE
   combined index; the manager splits it at 0x5149D0:

       0x00514A40  sra  $s0, $v0, 2     ; port = index >> 2
       0x00514A4C  subu $s3, $s6, $v0   ; slot = index - port * 4

   So adhoc's getStorage(1) means index 0 = port 0, slot 0, and getStorage(2)
   means index 1 = port 0, SLOT 1 - not port 1. */
#define MSTORAGEMC_INDEX_OFFSET 0x10

/* The game's own complete sceMcGetDir wrapper, and the only caller of
   sceMcGetDir in the executable. It takes the libmc monitor at 0x689458 (NOT
   the manager's), retries the queue until it succeeds (0x538A74 bnez -> yield
   0x5255B0 -> retry), runs the completion loop, unlocks, and returns the entry
   count or a negative libmc error. Use this rather than driving the async
   protocol by hand: sceMcSync in mode 0 blocks and returns 1 or -1, never 0,
   so a hand-rolled "poll until 1" loop spins forever whenever the queue failed. */
#define ADDR_mcGetDir 0x538A00

/* mStorageMC::read, vtable 0x6CA058 slot +0x1E4. A four-instruction thunk that
   turns the storage index into a card index and tail-jumps to the library. */
#define ADDR_mStorageMC_read 0x3DF4D8

/* The library read behind it: mcRead(index, name, buf, len, offset). It swaps
   its last two arguments into 0x5153D8(index, name, buf, offset, len) and takes
   the manager monitor itself, so callers need no lock.

   It returns the BYTE COUNT on success and a negative error otherwise - the
   worker converts internally at 0x5155C4/0x5155D0 (negu the status, then movz
   the count over it when the status is zero). This is NOT write's convention:
   write's worker 0x515990 returns a raw status and mStorageMC::write does the
   conversion itself at 0x3DF518/0x3DF520. */
#define ADDR_mcRead 0x516488

/* mStorageMC::write, vtable 0x6CA058 slot +0x1EC. Same thunk shape as read.
   Returns the requested length on success and -error otherwise, converting
   the library status itself at 0x3DF518 (negu) / 0x3DF520 (movn). */
#define ADDR_mStorageMC_write 0x3DF4F8

/* The library write: mcWrite(index, name, buf, len, offset), swapping its last
   two into 0x515990(index, name, buf, offset, len) exactly as mcRead does.
   Takes the manager monitor itself. Returns 0 on success, non-zero libmc
   error otherwise - NOT a byte count, unlike mcRead. */
#define ADDR_mcWrite 0x5164A8

/* The LAST hModule_defineMethod in mStorage's InitClass - the one registering
   "mkdir" (name string 0x7006C0, callback 0x3B7140). Redirecting this call is
   how the plugin adds its own members to the module: the hook registers mkdir
   itself and then whatever else it wants, and the game's own HValue_dtor at
   0x3B7500 cleans up the last one. Being last means the additions land after
   every stock member. */
#define ADDR_MStorage_mkdir_register 0x3B74F4

/* --- adhoc value types.

       An adhoc value carries its vtable pointer at object+0x04, and that word
       IS the type - nothing derives from these three, so comparing it is an
       exact test. There is no hBool in this build; adhoc booleans are hInt. */
#define ADDR_hInt_VTable    0x6DAF70
#define ADDR_hFloat_VTable  0x6DAB08
#define ADDR_hString_VTable 0x6DC4B0

/* hNil's handle constructor - the sibling of HInt_HInt, but with no value to
   take. Used to hand script a real "nothing" rather than a null handle.

   That distinction is not cosmetic. adhoc's own `nil` literal IS a null handle
   (mNilConst::execute 0x501F98 pushes a zeroed slot), and a null is safe only
   in a comparison: mBinaryOperator substitutes a fresh hNil for a null operand
   at 0x4FE5CC, which is what makes `x == nil` work. A bare truth test does NOT
   - mJumpZero::execute loads the object's vtable at 0x5006E8 with no null check
   and calls through it, so `if (x)` on a null handle jumps through garbage.
   Returning a real hNil instead makes both forms safe: its toInt (0x4F4428)
   answers 0 and its toFloat (0x4F4430) answers 0.0. */
#define ADDR_HNil_HNil      0x4F1F88

/* hArray's vtable, and the two offsets that reach its elements.

   Same exact-type-test discipline as the three value types above, and the
   uniqueness was measured rather than assumed: the word 0x006DA040 appears in
   no data word anywhere in either loadable segment at any alignment, and only
   four lui/addiu pairs in the whole image materialise it - hArray's three
   constructors and its destructor. Nothing derives from hArray.

     0x004DFD5C  lui   $v1, 0x6e
     0x004DFD64  addiu $v1, $v1, -0x5fc0     -> 0x006DA040
     0x004DFD6C  sw    $v1, 4($s0)

   The elements are an inline std::vector of 4-byte value handles, with the
   start and finish pointers at +0x14 and +0x18. Three functions fix those:
   hArray::size (0x4E0028) is literally lw 4($a0) / lw 8($a0) / subu / sra 2
   with $a0 = this + 0x10; hArray::getElement (0x4E014C) does
   lw $v1,0x14($s1) / sll $v0,i,2 / addu; and hArray::hArray(n) (0x4DFF68)
   writes all three through $s0 = this + 0x10.

   So the ADDRESS of element i is an hObject** - the same shape as argv + n,
   which is what every Adhoc_ helper in MStorage.c already takes. */
#define ADDR_hArray_VTable 0x6DA040
#define ADHOC_ARRAY_START  0x14
#define ADHOC_ARRAY_FINISH 0x18

/* mStorageMC's vtable. An adhoc storage object carries its vtable pointer at
   +0x04, so comparing that word is an exact type test, exactly as for the
   three value types above.

   Wanted because getStorage() can hand back a HARD DISK storage instead, which
   is a different class with no card index - and the stock adhoc read callback
   dereferences its downcast result at 0x3B6BA8 with no null check at all. The
   constructor materialises this value with lui 0x6D / addiu -0x5FA8 at
   0x3DF404/0x3DF408 and stores it to obj+4 at 0x3DF410. */
#define ADDR_mStorageMC_VTable 0x6CA058

/* HString's handle constructor, the sibling of HInt_HInt and HFloat_HFloat.
   Unlike those it takes a POINTER TO A std::string rather than a raw value -
   build one with the STD_STRING macro in Adhoc.h. */
#define ADDR_HString_HString 0x4FB2E0

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
