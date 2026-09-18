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

/* CarGarage::getCurrentCar(garage) -> the 400-byte settings record of the car
   in the currently selected slot.

   Worth calling rather than doing the arithmetic, because the arithmetic has a
   trap in it. A CarGarage holds its three records starting at +8, not at 0:

       0x00300B10  lw    $a1, 0x4d8($a0)   ; the selected slot
       0x00300B20  j     0x00300AF0        ; tail call into getCar
       ...
       0x00300B04  addu  $v0, $a0, $v1     ; garage + 400 * slot
       0x00300B0C  addiu $v0, $v0, 8       ; ... plus EIGHT

   So a field at record offset D is at displacement D + 8 from the garage, and
   both numbers occur in the image as real displacements of unrelated fields -
   accessor displacement 0x15E is the TCS byte, record offset 0x166 is a flag.
   Reading the record pointer from the game removes the whole question. */
#define ADDR_CarGarage_getCurrentCar          0x300B10

/* The power modifier inside a settings record: 16-bit little endian, 0 meaning
   "no modifier" and 1000 meaning 1.000x. It sits just past the two ballast
   bytes, with one byte of padding nothing in the image ever touches:

       record + 0x15B  ballast weight   u8, 0..200
       record + 0x15C  ballast balance  s8, -50..+50   (read with lb, SIGNED)
       record + 0x15D  padding
       record + 0x15E  power modifier   u16

   0x15B is odd, so 0x15E is even and the halfword is aligned - every stock
   access is an aligned sh/lhu. Exactly three instructions in the image touch
   it: 0x0030125C lhu (the consumer), 0x00303CA4 sh (the record reset) and
   0x003042EC sh (loaded from the parts database on a car install).

   The scale is proven twice over. The consumer at 0x00211CBC multiplies by
   0x3A83126E = 0.001f and substitutes 1.0f when the field is zero, and
   0x00301720 independently derives the same field's stock value as an engine
   row byte times ten - so 1000 really is unity. */
#define CARGARAGE_RECORD_POWER 0x15E

/* The car's colour index, and the two fields a setter has to check first.

   record + 0x00  car code       s64, all ones when the slot is empty
   record + 0x10  variation key  s64, -1 when the parts database had no row
   record + 0x18  colour index   s8

   The colour is a SIGNED byte - CarRecord::getColorIndex is the two-instruction leaf
   0x00305250 `jr $ra; lb $v0,0x18($a0)` - and -1 is a legal stored value meaning
   "no colour chosen yet". The record reset writes it (0x00303BFC addiu $v1,-1;
   0x00303C1C sb $v1,0x18($a0)) and the spec builder normalises it to 0 in place
   on first use (0x0030114C lb / 0x00301154 bne against -1 / 0x0030115C sb $zero).
   So -1 can never serve as a "no car" sentinel for this field the way it can
   for the power setting.

   The variation key matters because CarRecord::getColorCount reads THAT, not the
   car code - and 0x00303DD8 can leave a record holding a valid car code beside a
   -1 key (0x00303F5C beq against -1 with the store in the delay slot). A slot
   that looks occupied can therefore still index a null database table. */
#define CARGARAGE_RECORD_COLOR     0x18
#define CARGARAGE_RECORD_CARCODE   0x00
#define CARGARAGE_RECORD_VARIATION 0x10

/* CarGarage::setColorIndexAll(container, index) -> 1 if it took, 0 if refused.

   Takes the CONTAINER, not a record, and this is the one place paint differs
   structurally from every other setting: a CarGarage is ONE car with THREE
   setting sheets, and colour is a property of the car rather than of a sheet.
   This walks all three itself - 0x00300BDC addiu $s0,$a0,8 / 0x00300BF8 addiu
   $s0,$s0,400 / 0x00300C0C slti $v0,$s1,3 - and every stock caller uses it
   (0x00133024, 0x001330D8, 0x0012BCA0, 0x003008EC). Writing one record's byte
   would leave the other two sheets on the old colour, and whichever sheet is
   selected when a race commits is the one that reaches the track.

   It validates for us. The per-record setter beneath it (0x00305208) calls
   CarRecord::getColorCount and refuses an index that is not below it
   (0x00305224 slt / 0x00305228 beq), so the plugin needs no palette knowledge.
   The compare is SIGNED, though, so a NEGATIVE index sails past it and gets
   stored - that one hole is the caller's to plug. */
#define ADDR_CarGarage_setColorIndexAll 0x300BD0

/* --- the garage list.

   The persistent record of the cars you own, and the reason painting the
   working car alone does not last. It is the SAME OBJECT as the CarGarage: the
   list sits at the base, the CarGarage at + 0x7D08. 0x0031138C passes
   `list + 32008` straight in as the CarGarage, and riding_car resolves its own
   garage at gameObj + 0x7D08 - so a CarGarage pointer less 0x7D08 is the list.

   Entries are 32 bytes starting at list + 8 (every site does sll 5 then +8):

     entry + 0x00  car code, 64 bit
     entry + 0x08  flags, 64 bit: bit 0 = occupied, BITS 2..8 = colour index
     entry + 0x18  another 7-bit field at bits 10..16

   Both directions of the colour field are in the image and they agree:
     write  0x00310C70  andi 127 / dsll 2 / and -509 / sd 8($entry)
     read   0x00311440  ld 8($entry) / dsll 30 / dsra32 / andi 127
   and the read hands its result straight to CarGarage::setColorIndexAll - which
   is precisely why an unmirrored change is undone the next time the car is
   loaded out of the list.

   GarageList::getCurrentEntry is a leaf with no side effects, returning
   list + 8 + 32 * (*(int*)(list + 0x18210)). Note it CLAMPS a negative index to
   zero (0x00311510 slti / 0x00311514 movn) rather than failing, so with nothing
   selected it hands back entry 0 - which belongs to some other car. Check the
   entry's car code before writing through it. */
#define ADDR_GarageList_getCurrentEntry 0x311500
#define GARAGE_LIST_TO_CARGARAGE   0x7D08
#define GARAGE_ENTRY_CARCODE       0x00
#define GARAGE_ENTRY_FLAGS         0x08
#define GARAGE_ENTRY_OCCUPIED_BIT  1
#define GARAGE_ENTRY_COLOR_SHIFT   2
#define GARAGE_ENTRY_COLOR_MASK    0x1FC

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

       Before main(), POOL_SETUP_FUNC hands a pool allocator everything from
       the end of .bss up to the top of RAM and zero-fills it: the base is read
       from POOL_BASE_GLOBAL and aligned up to 16 (0x8D0FB0), the top from
       MEMORY_SIZE_OFFSET. The IME cave sits well below that, so the clear
       cannot reach it, and it runs as shipped. GLOBAL_MEMSET_OFFSET is that
       call; older builds NOPed it, which left a console's heap holding
       whatever the BIOS or loader had left in RAM. Keep PLUGIN_BASE_ADDRESS in
       step with BASE_US in game.mk. */
#define GLOBAL_MEMSET_OFFSET 0x5225F0
#define MEMORY_SIZE_OFFSET   0x681970
#define PLUGIN_BASE_ADDRESS  0x68A480
#define PLUGIN_RESERVE_BYTES 0x10310
#define POOL_SETUP_FUNC      0x5225B0
/* its one call, from startup (0x4DA4B0), and the word there */
#define ADDR_PoolSetup_Call  0x4DA4B8
#define INSN_JAL_PoolSetup   0x0C14896C

/* --- the pool's own globals, for reference and the boot trace. The mod no
       longer touches them: the heap is exactly retail size. SBRK_BREAK_GLOBAL
       is newlib's sbrk break (sbrk is 0x5DA760), shipping the same address;
       nothing calls sbrk. */
#define POOL_BASE_GLOBAL     0x68196C
#define POOL_BASE_VALUE      0x8D0FAC
#define SBRK_BREAK_GLOBAL    0x6A29D8
/* what MEMORY_SIZE_OFFSET ships holding: the pool's top */
#define MEMORY_SIZE_VALUE    0x01FF8000

/* --- dev RAM (pcsx2 builds; see DevRam.c).

       GetMemorySize is a bare syscall stub (li v1,0x7F; syscall). Every real
       PS2 answers 32 MB. PCSX2 answers 128 MB when its "Enable 128MB RAM (Dev
       Console)" option is on (R5900OpcodeImpl.cpp, syscall 127), and maps
       0x02000000..0x08000000 plus its uncached (0x22000000) and UCAB
       (0x32000000) mirrors when it boots the game (R5900.cpp, eeloadHook). The
       game's own call is in InitTLB, at 0x5E6060.

       SetTLBEntry is the game's range-checked wrapper (indices 13..47) around
       syscall 0x56. The kernel's table covers 32 MB and leaves 39..47 free.

       crt0 hard-codes the main thread's stack at 0x01FF8000..0x02000000
       (lui a1,0x200 at 0x10019C, handed to SetupThread), whatever the RAM
       size, so the pool cannot grow up through it: dev RAM moves it above.
       The reserve is the low RAM the pool leaves behind: past the end of .bss,
       which crt0 clears, and below that stack. */
#define ADDR_GetMemorySize   0x5DA4C0
#define INSN_GetMemorySize   0x2403007F   /* addiu v1,zero,0x7F */
#define ADDR_SetTLBEntry     0x5E5FC8
#define INSN_SetTLBEntry     0x27BDFFF0   /* addiu sp,sp,-0x10 */
#define MAIN_STACK_BASE      0x01FF8000

#if defined(DEVRAM_ADDRESS) && \
    (DEVRAM_ADDRESS < POOL_BASE_VALUE || \
     DEVRAM_ADDRESS + DEVRAM_RESERVE > MAIN_STACK_BASE)
#error "DEVRAM_US / DEVRAM_RESERVE in game.mk must lie between the end of .bss and the main stack"
#endif

/* --- memory diagnostics in dev RAM mode (reserve/MemDiag.c).

       The pool object and its two queries. Both lock and walk the free lists:
       0x527880 returns the largest free block less 16, 0x527910 the sum of
       every free block less 16 each. The game reaches them only through two
       wrappers - GetMaxMemorySize and its total-free twin - which dev RAM mode
       answers with what the stock heap would give. */
#define POOL_OBJECT                  0x89DA20
#define ADDR_Pool_LargestFree        0x527880
#define ADDR_Pool_TotalFree          0x527910
#define ADDR_GetMaxMemorySize        0x522640
#define ADDR_GetFreeMemorySize       0x522660
#define INSN_HeapQuery_0             0x27BDFFF0   /* addiu sp,sp,-0x10 */
#define INSN_HeapQuery_1             0x3C04008A   /* lui a0,0x8A */

/*     The pool object: end +4 and start +8 (region init, 0x527368), 16 free
       lists from +0xC (0x5272F8 picks one by size), and a counting lock at
       +0x4C taken with 0x522A40 and dropped with 0x522A80 (nests; the first
       take disables interrupts). A block is a 16-byte header: previous block
       +0, end +4, and for a free block its list link +0xC; an allocated one
       holds -1 at +8 and +0xC.

       malloc's pool routine (0x527478) drops the lock at 0x5275BC with the new
       block in s4 and the pool in s6; free's (0x527790) takes it at 0x5277C4
       with the block in s1 and the pool in s2. Every allocation and free of
       every pool passes those two calls, realloc's included. */
#define ADDR_Pool_Lock               0x522A40
#define ADDR_Pool_Unlock             0x522A80
#define ADDR_PoolAlloc_UnlockCall    0x5275BC
#define INSN_JAL_Pool_Unlock         0x0C148AA0
#define INSN_PoolAlloc_UnlockSlot    0x8FA40000   /* lw a0,0(sp) */
#define ADDR_PoolFree_LockCall       0x5277C4
#define INSN_JAL_Pool_Lock           0x0C148A90
#define INSN_PoolFree_LockSlot       0xAFA20000   /* sw v0,0(sp) */

/*     Where a block's caller is looked for past, on the stack: the heap's own
       code - the pool routines, the malloc/memalign/calloc wrappers and the
       blocking ones that retry (0x5285D8..0x5287C8) - and operator new and
       new[] (0x5EDA58, 0x5EDB70), which every C++ allocation goes through. */
#define GAME_TEXT_START              0x100000
#define GAME_TEXT_END                0x65FBBC
#define HEAP_CODE_START              0x522600
#define HEAP_CODE_END                0x528800
#define OPERATOR_NEW_START           0x5EDA58
#define OPERATOR_NEW_END             0x5EDBE8

/*     The loading screen's clock. LoadingConfigPS2::getColor(out, config) picks
       config+0x20 (red) while the disc is slow, config+0x30 (blue) while an
       allocation is waiting - update, 0x1039A8, sets config+0xC from
       xallocate_error_count - and config+0x10 (grey) otherwise. The four
       loading classes share it. */
#define ADDR_LoadingClock_GetColor   0x103A48
#define INSN_LoadingClock_GetColor0  0x8CA30008   /* lw v1,8(a1) */
#define INSN_LoadingClock_GetColor1  0x0080102D   /* move v0,a0 */

/*     The course block. AllocateRaceBuffer asks 0x276388 for 0xBC8000 bytes
       (plus 0x100 of slack) and keeps the size in course_buffer_size; after a
       free - replays - 0x276370 asks again with that size. 0x276388 takes the
       block from the heap (0x5286D0) and makes it the only block of a
       fixed-size arena (base +0, total +4, block size +8, blocks handed out
       +0x10) with 0x2DF580 (arena, base, total, block size), which empties
       the arena first with 0x2DF568 and returns the base it had.
       COURSE_BLOCK_LIVE is 1 while it is allocated. The course loader
       (0x276810) takes the block with 0x2DF600 and loads up to the arena's
       block size into it; photo mode borrows it at 0x276440 and uses 11 MB.
       The free, 0x2763F8 (from 0x286364 and 0x2E9550), empties the arena
       with 0x2DF568 at 0x276418 and hands the base that returns to free
       (0x5226E0). The last call AllocateRaceBuffer makes is to 0x2E2760, once
       the course, car and work buffers are all in. */
#define COURSE_BLOCK_STOCK           0xBC8000
#define COURSE_ARENA                 0x662DC0
#define COURSE_BLOCK_LIVE            0x662DDC
#define ADDR_CourseBlock_Alloc       0x276388
#define ADDR_CourseBlock_AllocRace   0x2E94E0   /* jal, in AllocateRaceBuffer */
#define ADDR_CourseBlock_AllocAgain  0x276370   /* jal, after a free */
#define INSN_JAL_CourseBlock_Alloc   0x0C09D8E2
#define ADDR_Arena_Take              0x2DF600
#define ADDR_CourseArena_TakeLoad    0x276864   /* jal, in the course loader */
#define INSN_JAL_Arena_Take          0x0C0B7D80
#define ADDR_CourseArena_TakeBorrow  0x276458   /* j, photo mode's borrow */
#define INSN_J_Arena_Take            0x080B7D80
#define ADDR_Arena_Init              0x2DF580
#define ADDR_Arena_Release           0x2DF568
#define ADDR_CourseBlock_FreeRelease 0x276418   /* jal, in the course block's free */
#define INSN_JAL_Arena_Release       0x0C0B7D5A
#define ADDR_RaceBuffers_LastCall    0x2E950C
#define ADDR_RaceBuffers_LastTarget  0x2E2760
#define INSN_JAL_RaceBuffers_Last    0x0C0B89D8

/*     GT4.VOL's table of contents, which the game keeps in the heap for the
       whole session. The file device's mount (0x45FC40, a virtual) reads the
       table's 0x40-byte header from the volume sector held at 0x6E0E24
       (0x22B7: volume offset 0x115B800), asks 0x45FA08 - blocking
       memalign(0x80) - for the table's length (header +8) rounded up, to 64
       on the disc path and 2 KB on the file path, reads the table into that
       block and hands it to the volume with 0x4618B0: at 0x45FD20 on the disc
       path, 0x45FE8C on the file path. Then it makes the volume's 8-page
       cache (0x461C58, at 0x45FD68 and 0x45FED4; the cache is the volume's
       member at +0x10), after which GetPage (0x461A20: volume, index) returns
       any page inflated and decrypted, its cache slot pinned until
       ReleasePage (0x461A00: volume, index) - the volume's vtable slots +0x20
       and +0x28. Eviction skips pinned slots, so with all eight pinned the
       next GetPage spins forever. Header +0x10 is the page size, +0x12 the
       page count. A page opens with u16 type (0 records, 1 index) and u16
       entries * 2 + 1. Each entry has an 8-byte slot, counted back from the
       page's end, whose u16 at +4 is the offset of the entry's info; the
       info's first byte is 0 for a folder, 1 a file, 2 a compressed file. */
#define ADDR_Vol_SetTable            0x4618B0
#define ADDR_VolMount_SetTableDisc   0x45FD20
#define ADDR_VolMount_SetTableFile   0x45FE8C
#define INSN_JAL_Vol_SetTable        0x0C11862C
#define ADDR_Vol_CacheInit           0x461C58
#define ADDR_VolMount_CacheDisc      0x45FD68
#define ADDR_VolMount_CacheFile      0x45FED4
#define INSN_JAL_Vol_CacheInit       0x0C118716
#define ADDR_Vol_GetPage             0x461A20
#define ADDR_Vol_ReleasePage         0x461A00
#define VOL_PAGE_CACHE               0x10

/*     The retail disc's table, for comparison: 391,021 bytes in a 391,040-byte
       block, 432 pages (426 of them records), 18,916 files (14,915 of them
       compressed) and 236 folders. */
#define TOC_RETAIL_BYTES             391040
#define TOC_RETAIL_PAGES             432
#define TOC_RETAIL_FILES             18916
#define TOC_RETAIL_FOLDERS           236

/*     License demo replays. 0x1A8BE8(out, name) builds a replay's path: the
       demo directory from 0x1A8B50 ("replay", unless a DemoDirectory setting
       says otherwise, and a "/"), then sprintf(end, "license/%s", name) at
       0x1A8C4C, reading a "tw" name as "jp" - so replay/license/usl0a0001.
       Its callers are 0x10CA3C and 0x10CBCC. The format string at 0x6EAD60
       is reached only through this lui/addiu pair, both on a1. */
#define ADDR_LicenseReplay_FormatLui    0x1A8C40
#define INSN_LicenseReplay_FormatLui    0x3C05006F   /* lui   a1, 0x6F        */
#define ADDR_LicenseReplay_FormatAddiu  0x1A8C48
#define INSN_LicenseReplay_FormatAddiu  0x24A5AD60   /* addiu a1, a1, -0x52A0 */

/*     The race display's texture file. 0x1C1278 loads it when the first
       RaceDisplay of a generation is built (the shared resource at 0x6618A8
       is empty, +4 == 0), and 0x1BFF08 releases it when the last one dies:
       so the file is read once per race, and a change between races is safe.
       The path is sprintf(buf, "/race_display/%s/%s/display.gpb", set,
       region) at 0x1C12F8: the set is "gt4" (0x6EC440; "tt" when the
       display's +0x33 is set, which retail never does), or what a
       RaceDisplayVersion setting says (0x4DADD0); the region is 0x588DF0's
       table lookup, "US" here. The format at 0x6EC460 is reached only through
       this lui/addiu pair, both on a1, and no data word holds it. The plugin
       points the pair at a format of its own, whose file name script sets. */
#define ADDR_RaceGpb_FormatLui          0x1C12E8
#define INSN_RaceGpb_FormatLui          0x3C05006F   /* lui   a1, 0x6F        */
#define ADDR_RaceGpb_FormatAddiu        0x1C12F0
#define INSN_RaceGpb_FormatAddiu        0x24A5C460   /* addiu a1, a1, -0x3BA0 */

/* --- the caves in dead code.

       GT4 Online links the Medius SDK's billing client - MediusBilling.c and
       its purchase cache mbill_cache.c - and never runs it, and .text has a
       run of zero padding before the 8 KB-aligned code at 0x5AA000:

         0x48D780..0x491060  14,560  MediusBilling.c (38 functions)  .cave2
         0x494540..0x497C18  14,040  mbill_cache.c (59 functions)    .cave2b
         0x5A8AC0..0x5AA000   5,440  zero padding                    .cave2c

       A whole-image reachability pass reaches none of it (roots: the entry,
       every data word pointing into .text, every lui pair building a text
       address, the .ctors/.dtors tables; jump tables resolved). Every jal into
       the two Medius runs comes from other dead code, a chain that starts at
       0x476D38, which nothing references. An independent check, with a tracker
       that follows register moves and stack spills, found no live code or data
       building an address inside any of the three. The billing module's only
       live pieces, 0x48CD60 and 0x48D4B8..0x48D780, sit outside them.

       The build writes the caves over this code in place, in the text
       segment's file image (tools/inject_cave2.py, checked against the base
       image), so nothing is taken from the heap. game.mk's CAVE2_US and
       CAVE2_RESERVE arrive here on the command line; the check keeps .cave2
       inside its run. */
#define DEAD_MEDIUS_BILLING      0x48D780
#define DEAD_MEDIUS_BILLING_END  0x491060
#define DEAD_MEDIUS_CACHE        0x494540
#define DEAD_MEDIUS_CACHE_END    0x497C18
#define DEAD_TEXT_PADDING        0x5A8AC0
#define DEAD_TEXT_PADDING_END    0x5AA000

#if defined(CAVE2_ADDRESS) && \
    (CAVE2_ADDRESS < DEAD_MEDIUS_BILLING || \
     CAVE2_ADDRESS + CAVE2_RESERVE > DEAD_MEDIUS_BILLING_END)
#error "CAVE2_US / CAVE2_RESERVE in game.mk run outside the dead MediusBilling code"
#endif

/* PolyphonyGL's 174 entry points are bulky enough to keep separate. */
#include "SCUS-97436.pgl.h"

/* --- the manufacturer tables.

       Two arrays of { unsigned int id; const char* name; }, holding 92 and 109
       records. They are BYTE IDENTICAL in all four retail executables - only
       their addresses differ - so source/core/hooks/MakerList.c serves every
       build from these constants alone.

       The eight instruction addresses below are every place in the image that
       materialises a table address or bounds one. That completeness was
       measured, not assumed: a sweep of both loadable segments at every 4-byte
       alignment found zero data pointers into either table, and the image has
       no $gp-relative addressing that could hide a reference.

       The Ctor pairs load table A and table B for registration; the Getter pair
       loads table B for the adhoc tuner list. CountA bounds an ID (the highest
       id plus one) while CountB bounds an INDEX (a record count) - they are not
       the same kind of number. See MakerList.c. */
#define ADDR_MakerTableA        0x6F7A68
#define ADDR_MakerTableB        0x6F7E08
#define ADDR_MakerCtorA_Lui     0x308AE4
#define ADDR_MakerCtorA_Addiu   0x308AF0
#define ADDR_MakerCtorB_Lui     0x308AF8
#define ADDR_MakerCtorB_Addiu   0x308B04
#define ADDR_MakerGetter_Lui    0x308A5C
#define ADDR_MakerGetter_Addiu  0x308A68
#define ADDR_MakerCountB        0x308A58
#define ADDR_MakerCountA        0x19A76C

/* ===========================================================================
   RaceDisplay - the in-race HUD.

   Everything below was decoded from BASE_SCUS-97436, and every instruction
   word quoted was read back out of the image, so a mismatch at init means the
   target header does not describe the executable being patched. RaceHud.h
   carries the layout and HUD-type maps that these addresses describe.
   =========================================================================== */

/* --- the element base class, RaceDisplayObjectBase.

       THE VPTR IS AT +0x14, NOT +0x00. RaceDisplay itself is the opposite -
       its vptr is at +0x00 - and the two must never be confused, because
       vtable slot 7 means setPos on an element and setLayoutMode on the
       display. */
#define RDO_FLAGS   0x00   /* u32; the ctor clears the low byte           */
#define RDO_ALIGNH  0x01   /* u8                                          */
#define RDO_ALIGNV  0x02   /* u8                                          */
#define RDO_X       0x04   /* s16                                         */
#define RDO_Y       0x06   /* s16                                         */
#define RDO_W       0x08   /* s16                                         */
#define RDO_H       0x0A   /* s16                                         */
#define RDO_ALPHA   0x0C   /* f32; the base ctor stores 1.0f              */
#define RDO_TEXTURE 0x10   /* handle; zero means nothing to draw          */
#define RDO_VPTR    0x14

/* Byte offsets into an element vtable, whose entries are 8 bytes:
   { s16 thisAdjust; s16 pad; u32 fn }. thisAdjust is 0 for all three in every
   HUD class, but the walker reads it anyway and so does this plugin.

   Call order matters and is the game own order: align, then size, then
   position. The three panel classes recompute child geometry inside setPos and
   read the align and size they were given while doing it. */
#define RDO_VT_SETPOS   56   /* slot 7  -> 0x1D7330 setPos(obj, x, y)      */
#define RDO_VT_SETSIZE  64   /* slot 8  -> 0x1D7340 setSize(obj, w, h)     */
#define RDO_VT_SETALIGN 72   /* slot 9  -> 0x1D7350 setAlign(obj, ah, av)  */

#define ADDR_RaceDisplayObject_VTable   0x6B1730
#define ADDR_RaceDisplayObject_setPos   0x1D7330
#define ADDR_RaceDisplayObject_setSize  0x1D7340
#define ADDR_RaceDisplayObject_setAlign 0x1D7350

/* Alignment bits, read by 0x1D83E8 (float), 0x1D8448 (int) and 0x1D84A8 (the
   widescreen shift). 0x1D84E8, once listed here, is a colour scaler.
     bits 0-1  anchor: 0 = min edge, 1 = max edge, 2 and 3 = centre
     bit  2    enable the widescreen shift
     bit  3    shift direction: set = +96, clear = -96
   The shift is gated on ADDR_HudWideFlag being non-zero. */
#define RDO_ALIGN_ANCHOR_MASK 0x03
#define RDO_ALIGN_WIDE_SHIFT  0x04
#define RDO_ALIGN_WIDE_PLUS   0x08

/* --- RaceDisplay itself. */
#define ADDR_RaceDisplay_VTable          0x6AFA28
#define ADDR_RaceLicenseDisplay_VTable   0x6BB7B8
#define ADDR_RaceChallengeDisplay_VTable 0x6BE4D8

#define RD_VPTR           0x00
#define RD_PANEL          0x14  /* the selected cluster panel               */
#define RD_LAYOUT_MODE    0x1C
#define RD_SUB_MODE       0x20
#define RD_METER_HINT     0x24
#define RD_DIRTY_BYTE     0x31  /* non-zero asks update() for a relayout    */
#define RD_HUD_TYPE       0x33  /* 0 = gt4 texture set, 1 = tt              */
#define RD_MASK_EFFECTIVE 0x58
#define RD_MASK_BASE      0x5C
#define RD_MASK_INRACE    0x60
#define RD_SIZEOF         21692 /* RaceSplitDisplay::getDisplay 0x603158    */

#define ADDR_RaceDisplay_ctor             0x1BFF28
#define ADDR_RaceDisplay_dtor             0x1C01B0
#define ADDR_RaceDisplay_relayout         0x1C7430
#define ADDR_RaceDisplay_applyLayoutTable 0x1C8970
#define ADDR_RaceDisplay_setMask          0x1C8308

/*
    How many RaceDisplay objects are alive.

    The ctor increments it (the address is built at 0x1C011C) and the dtor
    decrements it at 0x1C01F8, tearing the shared state down at 0x1BFF08 when
    it reaches zero. Those two sites are the ONLY references in the image.

    This plugin uses it as a liveness gate: a RaceDisplay* captured from the
    relayout hook stays valid only while this is non-zero. Between a race
    ending and the next relayout there is no other way to tell a live display
    from freed memory that still looks like one.
*/
#define ADDR_RaceDisplay_LiveCount 0x6618A0

/* --- hook sites. These are CALL SITES, not function entry points, so the word
       at each is a jal and MAKE_JAL retargets it without disturbing the delay
       slot that follows. */

/* Both callers of relayout, and callers_of() returns exactly these two. $a0 is
   the live RaceDisplay* at both; $a1 is the meter hint. */
#define ADDR_RaceDisplay_relayout_JAL_build  0x1C1410
#define ADDR_RaceDisplay_relayout_JAL_update 0x1C47E8
#define INSN_JAL_RaceDisplay_relayout        0x0C071D0C

/*
    The last hModule_defineMethod in the MOption InitClass (0x161F38..0x163888),
    registering "save" (name 0x6E74E8, callback 0x161CD0). The module name
    string "MOption" is at 0x6E69C8 and this function is its only reference.

    Being LAST is what makes it the right site: the game emits one HValue_dtor
    per registration, and the one at 0x16380C - NOT 0x163808, which is the
    `addiu $a1, $zero, 2` carrying that call second argument - accounts for
    whichever registration ran last. Adding members after "save" therefore
    balances, exactly as the MStorage.c hook does.

    Do NOT move this to either of the two registrations that follow (0x16382C
    and 0x163858): they are hModule_defineAttribute, they are not last, and
    displacing one would leave the game dtor balancing the wrong handle. The
    five-argument ABI is not the problem - 0x4F0C00 reads its fifth argument
    from $t0 at 0x4F0C2C, and Adhoc/Decl.h already depends on that working.
*/
#define ADDR_MOption_save_register    0x163800
#define INSN_JAL_hModule_defineMethod 0x0C13C2C4

/* --- vector colours.

       Packed colour in this engine is 0xAABBGGRR - the bytes in memory are
       R, G, B, A - and alpha 0x80 is opaque, not 0xFF. Two anchors settle it:
       0x800086E6 is the A-Spec orange #E68600 and 0x800003FE is red #FE0300.
       Every colour this plugin shows to script is swapped into the 0xAARRGGBB
       a human writes, in one pair of helpers in RaceHud.c and nowhere else.

       The two arrays below are per-vertex colours for the needle mesh at
       0x6EE2A0, returned by vtable slot 13. They are ordinary .data (the RW
       PT_LOAD covers 0x65FC00 upward) so a plain store changes them, and an
       exhaustive scan of both segments found NO reference to either array
       other than the getter that produces it - so nothing else can overwrite
       them and nothing else is perturbed by editing them.

       0x6EE2F0 is SHARED by RaceRoundMeterBase, RaceRoundTachometerBase,
       RaceOnboardTachometer, RaceOnboardSpeedmeter and RaceBoostmeter, so
       editing it changes the needle on all of them at once. Per-instance
       needles need the array copied into plugin memory and the getter lui and
       addiu repointed - BOTH words, because the plugin base 0x68A480 has the
       high bit set in its low half and so is lui 0x69, not lui 0x68. */
#define ADDR_NeedleColours   0x6EE2F0   /* 6 words: #400000 edge, #C80000 core */
#define ADDR_NeedleColoursTT 0x6EEED0   /* 6 words: flat #C80000               */
#define ADDR_NeedleVerts     0x6EE2A0   /* 6 verts x 3 floats                  */

#define ADDR_NeedleColourGetter_Lui     0x1DB398 /* 0x3C02006F */
#define ADDR_NeedleColourGetter_Addiu   0x1DB3A0 /* 0x2442E2F0 */
#define ADDR_NeedleColourGetterTT_Lui   0x1FE1B8 /* 0x3C02006F */
#define ADDR_NeedleColourGetterTT_Addiu 0x1FE1C0 /* 0x2442EED0 */

/*
    Sub-object chains, so a display-relative offset can name a colour field.
    Each was read out of the owning constructor jal and delay-slot pair.

    RaceOnboardPanel  = display + 280   (ctor 0x1CE1B0)
      + 112  RaceOnboardSpeedmeter  jal 0x297568 @0x1CE1F8
      + 204  RaceOnboardTachometer  jal 0x1FE010 @0x1CE200
      + 2384 RaceBoostmeter         jal 0x26FDB0 @0x1CE208
    RaceSimplePanel   = display + 3380  (ctor 0x1CD1F8)
      + 500  RaceABMonitor          jal 0x1CB050 @0x1CD2C0
        + 32 bar A (throttle), + 80 bar B (brake), both RaceSimpleBarMeter
*/
#define ONBOARD_PANEL     280
#define ONBOARD_SPEEDO    112
#define ONBOARD_TACHO     204
#define ONBOARD_BOOST     2384
#define SIMPLE_PANEL      3380
#define SIMPLE_ABMONITOR  500
#define ABMON_BAR_A       32
#define ABMON_BAR_B       80
#define TACHO_REDLINE     0x80  /* u32 packed; ctor writes 0x80000099 #990000 */
#define BARMETER_TROUGH   0x24  /* u32 packed; zeroed for the A/B monitor     */
#define BARMETER_FILL     0x28  /* u32 packed                                 */

/* Colours in instruction immediates that script does NOT get, for reference:
   the Tourist Trophy gap bars (dead in GT4 Online), and two lone luis, which
   hold only alpha and blue. The battle tachometer's redline is in
   HUD_COLOUR_SITES below, as revbar.redline. */
#define ADDR_ColImm_TTGapTrough_Lui   0x1D4DB4 /* 0x3C044000 -> 0x40000000 */
#define ADDR_ColImm_TTGapBar1_Lui     0x1D4E44 /* 0x3C0480A0               */
#define ADDR_ColImm_TTGapBar1_Ori     0x1D4E4C /* 0x3484A088 -> 0x80A0A088 */
#define ADDR_ColImm_TTGapBar23_Lui    0x1D4E7C /* 0x3C048000               */
#define ADDR_ColImm_TTGapBar23_Ori    0x1D4E88 /* 0x348400C8 -> 0x800000C8 */
#define ADDR_ColImm_BattleRedline_Lui 0x1FE6C4 /* 0x3C047000               */
#define ADDR_ColImm_BattleRedline_Ori 0x1FE6C8 /* 0x34840099 -> 0x70000099 */
#define ADDR_ColImm_TooltipBg_Lui     0x1D3EB8 /* 0x3C048000 -> 0x80000000 */

/* --- HUD colours a script sets from a menu (RaceHud.c).

       A colour the HUD draws with is built by instructions - a lui/ori pair
       in a constructor or in a draw - or kept in a HUD object each race
       builds. A colour set while no race runs has to go where the next race
       will read it, and each row says where:

         X(name, kind, a, aw, b, bw)

         PAIR   a the lui and aw its word, b the ori and bw its word: one
                packed colour, 0xAABBGGRR, which the ori completes - in the
                lui's register, or after a trip through a stack slot, as the
                warning lamps' do. Both immediates are rewritten.
         TINT   a a jal pglColor4f and aw its word; b the plugin stub it is
                pointed at instead, bw the grey the game passes, as float bits.
                A texture tinted by pglColor4f(g, g, g, alpha) has one float
                for R, G and B, so a colour needs the call routed through a
                stub that loads three.
         ARG    a a jal to the colour helper ADDR_HudColour_AlphaMul and aw its
                word; b the plugin stub it is pointed at, bw the shipped
                colour. For a colour whose register also colours something
                else: the stub puts the plugin's own colour in $a0 in place of
                the register's, so the two come apart. The call is pointed at
                the stub at install, holding the shipped colour until script
                sets one.
         FIELD  a the colour word's offset in RaceDisplay, aw its shipped
                value. Nothing writes it after the constructor, so it is put
                into each display as the display is built.
         DATA   a an array in .data, aw its length in words.

       Rows with the same name are one slot. Every word was read out of
       BASE_SCUS-97436, and each site's reach - what it colours, and that
       nothing rewrites it later - was traced twice, independently. */
#define HUD_COLOUR_SITES(X)                                                    \
    /* the round meters: tachometer, speedometer and boost meter */            \
    X("needle",            DATA,  0x6EE2F0, 6,          0,        0)          \
    X("redline",           PAIR,  0x1FE2CC, 0x3C078000, 0x1FE2D4, 0x34E70099) \
    X("meter.face",        TINT,  0x1DB538, 0x0C169AE4, 0,        0x3F999999) \
    X("meter.base",        FIELD, 388,      0x80FFFFFF, 0,        0)          \
    /* the G meter */                                                          \
    X("gmeter.base",       PAIR,  0x1D0FD8, 0x3C0480FF, 0x1D0FE0, 0x3484FFFF) \
    X("gmeter.point",      PAIR,  0x1D0F30, 0x3C0280E3, 0x1D0F38, 0x3442B896) \
    /* digital speed - simple, cockpit and Prius clusters - and its units */   \
    X("speed.digits",      PAIR,  0x1C9B30, 0x3C0280E3, 0x1C9B38, 0x3442B896) \
    X("speed.digits",      PAIR,  0x1CE26C, 0x3C0280E3, 0x1CE274, 0x3442B896) \
    X("speed.digits",      PAIR,  0x1CFAF0, 0x3C0380E3, 0x1CFAF4, 0x3463B896) \
    X("speed.unit",        FIELD, 3452,     0x80FFFFFF, 0,        0)          \
    X("speed.unit",        FIELD, 3332,     0x80FFFFFF, 0,        0)          \
    X("odometer",          PAIR,  0x1C9D94, 0x3C0480E3, 0x1C9DA0, 0x3484B896) \
    /* the gear number and its plate, the suggested gear, the shift lamp */    \
    X("gear.digits",       PAIR,  0x1CCBCC, 0x3C0480E3, 0x1CCBD4, 0x3484B896) \
    X("gear.base",         PAIR,  0x1CCB34, 0x3C0480FF, 0x1CCB3C, 0x3484FFFF) \
    X("gear.suggested",    PAIR,  0x1CCE94, 0x3C048000, 0x1CCEA0, 0x348400F0) \
    X("gear.lamp",         PAIR,  0x1CD038, 0x3C048000, 0x1CD044, 0x348400FF) \
    /* the simple cluster: pedal monitor, fuel gauges, rev bar */              \
    X("throttle.fill",     PAIR,  0x1CB0D8, 0x3C0280E3, 0x1CB0E0, 0x3442B896) \
    X("brake.fill",        PAIR,  0x1CB0DC, 0x3C038000, 0x1CB0E4, 0x346300F0) \
    X("throttle.trough",   FIELD, 3948,     0x00000000, 0,        0)          \
    X("brake.trough",      FIELD, 3996,     0x00000000, 0,        0)          \
    X("pedals",            TINT,  0x1CB308, 0x0C169AE4, 1,        0x3F800000) \
    X("fuel.fill",         PAIR,  0x1CB3D8, 0x3C0280E3, 0x1CB3DC, 0x3442B896) \
    X("fuel2.fill",        PAIR,  0x1CD344, 0x3C028000, 0x1CD348, 0x344200F0) \
    X("fuel.bg",           TINT,  0x1CB448, 0x0C169AE4, 2,        0x3F800000) \
    X("revbar.fill",       PAIR,  0x1DB8E8, 0x3C0480FF, 0x1DB8F4, 0x3484FFFF) \
    X("revbar.redline",    PAIR,  0x1FE6C4, 0x3C047000, 0x1FE6C8, 0x34840099) \
    /* warning lamps: the cockpit panel's setPos, then the simple ctor */      \
    X("lamp.asm",          PAIR,  0x1CEFC4, 0x3C0280E3, 0x1CEFCC, 0x3442B896) \
    X("lamp.asm",          PAIR,  0x1CD414, 0x3C0280E3, 0x1CD418, 0x3442B896) \
    X("lamp.warning",      PAIR,  0x1CEDB4, 0x3C028000, 0x1CEDF0, 0x346300C8) \
    X("lamp.warning",      PAIR,  0x1CD304, 0x3C038000, 0x1CD328, 0x344200C8) \
    /* the cockpit cluster's own bars. One register, $s2, built by the pair */ \
    /* in cockpit.fuel, colours both the throttle bar and the fuel bar above */\
    /* low; the throttle bar's call is given its own colour instead */        \
    X("cockpit.brake",     PAIR,  0x1CE6A4, 0x3C048000, 0x1CE6B0, 0x348400F0) \
    X("cockpit.throttle",  ARG,   0x1CE890, 0x0C076166, 0,        0x80E3B896) \
    X("cockpit.fuel",      PAIR,  0x1CE830, 0x3C1280E3, 0x1CE83C, 0x3652B896) \
    X("cockpit.fuel.low",  PAIR,  0x1CE9B0, 0x3C048000, 0x1CE9BC, 0x348400C8) \
    X("cockpit.nos",       PAIR,  0x1CEB28, 0x3C048000, 0x1CEB30, 0x348400F0) \
    X("tyres.base",        PAIR,  0x1CBBE8, 0x3C028087, 0x1CBBF0, 0x34427B80) \
    /* the pit refuelling gauge */                                             \
    X("refuel.bar",        PAIR,  0x1CB6DC, 0x3C04805E, 0x1CB6E8, 0x3484A8F2) \
    X("refuel.text",       PAIR,  0x1CB818, 0x3C04805E, 0x1CB824, 0x3484A8F2) \
    X("refuel.frame",      TINT,  0x1CB698, 0x0C169AE4, 3,        0x3F800000) \
    X("refuel.frame",      TINT,  0x1CB78C, 0x0C169AE4, 3,        0x3F800000) \
    /* clocks: digits in the constructor, at build and every frame */          \
    X("clock.digits",      PAIR,  0x1C9F80, 0x3C0280C8, 0x1C9F84, 0x3442C8C8) \
    X("clock.digits",      PAIR,  0x1C0E24, 0x3C1180C8, 0x1C0E48, 0x3631C8C8) \
    X("clock.digits",      PAIR,  0x1C4898, 0x3C0580C8, 0x1C48A4, 0x34A5C8C8) \
    X("clock.alert",       PAIR,  0x1C48A0, 0x3C038000, 0x1C48A8, 0x346300C8) \
    X("clock.label",       TINT,  0x1CA0E4, 0x0C169AE4, 4,        0x3F800000) \
    /* the label textures: position, lap, and the two time labels */           \
    X("label.position",    FIELD, 5696,     0x80FFFFFF, 0,        0)          \
    X("label.lap",         FIELD, 5724,     0x80FFFFFF, 0,        0)          \
    X("label.time1",       FIELD, 5752,     0x80FFFFFF, 0,        0)          \
    X("label.time2",       FIELD, 5780,     0x80FFFFFF, 0,        0)          \
    /* race position, lap counter, the lap-times list, the music banner */     \
    X("rank",              PAIR,  0x1C976C, 0x3C0480C8, 0x1C9778, 0x3484C8C8) \
    X("lap",               PAIR,  0x1D0EF0, 0x3C0280C8, 0x1D0EF8, 0x3442C8C8) \
    X("laptimes",          PAIR,  0x1CA294, 0x3C0480C8, 0x1CA2A4, 0x3484C8C8) \
    X("laptimes.lap",      PAIR,  0x1CA400, 0x3C0480B3, 0x1CA404, 0x3484B3B3) \
    X("music",             PAIR,  0x1D4090, 0x3C0480C8, 0x1D4098, 0x3484C8C8) \
    /* messages and the minimap */                                             \
    X("wrongway",          PAIR,  0x1C3150, 0x3C0280FF, 0x1C3158, 0x3442FFFF) \
    X("tooltip.text",      PAIR,  0x1C11A4, 0x3C0280A7, 0x1C11A8, 0x3442A5A5) \
    X("minimap",           PAIR,  0x1DBFB4, 0x3C0480FF, 0x1DBFBC, 0x3484FFFF) \
    X("minimap.rival",     PAIR,  0x1DBFE4, 0x3C0480FF, 0x1DBFF0, 0x3484FFFF) \
    X("minimap.player",    PAIR,  0x1DC070, 0x3C0480FF, 0x1DC080, 0x3484FFFF) \
    /* the Prius cluster */                                                    \
    X("prius.base",        TINT,  0x1CFDF8, 0x0C169AE4, 5,        0x3F800000) \
    X("prius.monitor",     TINT,  0x1D02B0, 0x0C169AE4, 6,        0x3F800000) \
    X("prius.mileage",     PAIR,  0x1CFB0C, 0x3C0280B5, 0x1CFB14, 0x34428F43) \
    X("prius.consumption", PAIR,  0x1CFB10, 0x3C038003, 0x1CFB18, 0x346363FD) \
    X("prius.vsc",         PAIR,  0x1CFE60, 0x3C048000, 0x1CFE70, 0x348499FF) \
    X("prius.brake",       PAIR,  0x1CFEC0, 0x3C048000, 0x1CFED4, 0x348400C8) \
    X("prius.battery",     PAIR,  0x1D053C, 0x3C088000, 0x1D0550, 0x3508E3FA) \
    X("prius.battery.off", PAIR,  0x1D0540, 0x3C098020, 0x1D0558, 0x35292020)

/* How many stubs HUD_COLOUR_SITES' TINT rows are numbered into. Every TINT
   jal word above targets ADDR_pglColor4f, which the stubs go on to. */
#define HUD_TINT_STUBS 7

/* The same for the ARG rows, and the colour helper their jal words target:
   u32 f(u32 colour, float k), which keeps the colour's RGB and scales its
   alpha by k. */
#define HUD_ARG_STUBS           1
#define ADDR_HudColour_AlphaMul 0x1D8598

/* --- aspect ratio.

       The live flag. Retail writes 0 or 1, and NOT ONE of its eighteen readers
       ever compares it to a non-zero constant - every use is beq/bne against
       zero, or a movz keyed on it. So it COULD carry a mode index - any
       non-zero value reproduces shipped widescreen behaviour exactly. Nothing
       uses that today: RaceAspect rewrites the constants on either side of
       the test instead and leaves the flag the bool the game expects. It is
       the enabling fact for any future runtime mode switch.

       It is ordinary .data at file offset 0x566AA0, not bss. */
#define ADDR_HudWideFlag 0x6655A0

/*
    The decision function, (block.width / block.height) >= 1.5f, and its four
    callers. Two STORE the flag (the JAL_apply and JAL_raceMode sites below);
    RaceAspect hooks JAL_apply to read the options' wide_mode word first (see
    the wide_mode notes further down) and returns this function's answer
    unchanged. The other two would have to keep boolean semantics even if a
    mode index ever went into the flag:

      0x315444  the change guard in 0x315428, which re-copies the 44-byte TV
                geometry block only when this function's bool differs from the
                value it was given. It is reached only from the setter
                0x313260, never from apply, so a mode index handed to the
                setter would re-copy on every set - which is why RaceAspect
                hands the setter the bool.
      0x3125EC  the text-layer mirror into 0x6651B4/0x6651B8, which drives the
                0.8f glyph advance. A mode index here would squash glyphs for a
                mode that has not defined its own metric.
*/
#define ADDR_ratioIsWide                0x315330
#define ADDR_ratioIsWide_JAL_apply      0x3125B0  /* stores to 0x6655A0     */
#define ADDR_ratioIsWide_JAL_raceMode   0x3126D0  /* stores to 0x6655A0 too */
#define ADDR_ratioIsWide_JAL_textMirror 0x3125EC  /* LEAVE ALONE            */
#define INSN_JAL_ratioIsWide            0x0C0C54CC

/*
    Every projection and anchor constant is an inline immediate; there is no
    float table anywhere to edit. These are the ones worth naming, with the
    shipped word beside each so the guard can check it.

    The +/-96 anchor inset is NOT the +/-106.67 the ortho gains. GT4 insets the
    widescreen HUD by 10.67 units per edge deliberately. Both numbers are
    correct and they mean different things.
*/
#define ADDR_Ortho_WideLeft_Lui  0x1C10FC /* 0x3C01C3D5 } -426.66666f */
#define ADDR_Ortho_WideLeft_Ori  0x1C1100 /* 0x34215555 }             */
#define ADDR_Ortho_WideRight_Lui 0x1C1108 /* 0x3C0143D5 } +426.66666f */
#define ADDR_Ortho_WideRight_Ori 0x1C110C /* 0x34215555 }             */
#define ADDR_Ortho_NarrowLeft    0x1C1120 /* 0x3C01C3A0   -320.0f     */
#define ADDR_Ortho_NarrowRight   0x1C1128 /* 0x3C0143A0   +320.0f     */
#define ADDR_Anchor_ShiftPlus    0x1D8468 /* 0x24E20060   +96         */
#define ADDR_Anchor_ShiftMinus   0x1D846C /* 0x24E4FFA0   -96         */
#define ADDR_Anchor_ShiftFloat   0x1D8400 /* 0x3C0142C0    96.0f      */
#define ADDR_Anchor_EdgeFloat    0x1D84BC /* 0x3C0142C0    96.0f      */
/*  The license and mission result trophy's own perspective (0x2B0730, drawn
    for ResultLicense and ResultChallenge only - nothing else reaches it): the
    aspect 0x2B08E8 hands 0x597B28 on the field path, wide then narrow, the
    narrow pair overwriting it when the flag is clear. Not the race's 3D,
    which runs through 0x26BD10 -> 0x26BDB0 -> 0x3154E0. */
#define ADDR_Aspect3D_Wide_Lui   0x2B09D0 /* 0x3C013FF3 } 1.9047617f  */
#define ADDR_Aspect3D_Wide_Ori   0x2B09D4 /* 0x3421CF3B }             */
#define ADDR_Aspect3D_Narrow_Lui 0x2B09E4 /* 0x3C013FB6 } 1.4285713f  */
#define ADDR_Aspect3D_Narrow_Ori 0x2B09E8 /* 0x3421DB6D }             */
#define ADDR_Split_LayoutId_Narrow 0x1C8D54 /* 0x24050008  8          */
#define ADDR_Split_LayoutId_Wide   0x1C8D5C /* 0x24050009  9          */
#define ADDR_Banner_X            0x1C7F28 /* 0x2405FF95   -107        */
#define ADDR_Banner_W            0x1C7F44 /* 0x24050356    854        */
#define ADDR_Banner_W2           0x1F142C /* 0x24050356    854        */

/*
    The rest of the aspect-dependent constants. Retail's values for these are
    already right for 16:9, so they only matter once a wider mode is chosen -
    but at 21:9 every one of them would be visibly wrong if left alone.

    The x squash sites are all LONE luis, each feeding a scale or a multiply
    that the flag gates: in 4:3 the branch skips it or a delay slot swaps in
    1.0f. A lone lui carries only the top 16 bits of a float, so 4/7 - the
    21:9 squash - lands on 0.5703 rather than 0.5714.

    The glyph advance is gated on the TEXT mirror at 0x6651B8, not on the
    main flag, and it is shared by every piece of text in the game.
*/
#define ADDR_Aspect3D_WideMul_Lui  0x2B0DF8 /* 0x3C013FAA } 1.3333333f: the  */
#define ADDR_Aspect3D_WideMul_Ori  0x2B0DFC /* 0x3421AAAA } trophy's w/h *4/3 */
#define ADDR_Ortho2P_WideLeft_Lui  0x1C8DDC /* 0x3C01C2D5 } -106.66666f     */
#define ADDR_Ortho2P_WideLeft_Ori  0x1C8DE0 /* 0x34215554 }                 */
#define ADDR_OrthoOverlay_WideRight_Lui 0x1F2FF8 /* 0x3C014455 } 853.333f  */
#define ADDR_OrthoOverlay_WideRight_Ori 0x1F2FFC /* 0x34215554 }           */
#define ADDR_XScale_A             0x10248C /* 0x3C013F40  0.75f          */
#define ADDR_XScale_B             0x1028E0 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_C             0x1BB474 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_D             0x1C519C /* 0x3C013F40  0.75f, HUD     */
#define ADDR_XScale_E             0x1F3054 /* 0x3C013F40  0.75f, overlay */

/*
    THE MENUS' 3D MODELS. 0x353600 loads 0.75 here, tests the flag at
    0x665578+0x28 (= 0x6655A0), branches 4:3 around to 1.0, and calls
    pglScale(x, *(float*)0x66558C, 1) on the projection. Each of its four
    callers first builds a frustum whose horizontal extent is the framebuffer
    (or a fixed 640x480) with only a vertical fov, so this is the one
    horizontal correction those views get:

      0x2EBBF4  GranTurismo4::ModelStudio slot 3 - the car viewer widget
      0x337714  mCarModelPS2's model draw (0x17F688, from vtable slot 76)
      0x36F2C0  mRenderContextPS2 slot 71 - the multi-pass mModel render
      0x36F92C  mRenderContextPS2 slot 72 - the mModel / CompatibleModel draw

    An earlier note here took the last two for the race cameras. They are not:
    the race cameras are the CameraSys classes, and none of them calls
    0x353600 - which is why rewriting it changed nothing in a race. The race's
    3D is left to script, through main::game.option.physical_monitor_size.

    Missed by the first sweep because the lui comes BEFORE the flag test and
    4:3 is a branch-around rather than the delay-slot swap the others use.
*/
#define ADDR_XScale_F             0x353604 /* 0x3C013F40  0.75f, menu 3D models */
#define ADDR_GlyphAdvance_A_Lui   0x31E3A0 /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_A_Ori   0x31E3A4 /* 0x3421CCCC }               */
#define ADDR_GlyphAdvance_B_Lui   0x33B360 /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_B_Ori   0x33B364 /* 0x3421CCCC }               */

/* The menu text width: a third copy of the same squash, gated on the same
   text mirror 0x6651B8 (branch at 0x33D7D4 skips it in 4:3) and
   multiplied into the glyph width at 0x33D7E8. */
#define ADDR_GlyphAdvance_C_Lui   0x33D7DC /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_C_Ori   0x33D7E0 /* 0x3421CCCC }               */

/* Three more text paths with the same squash, found by sweeping every read of
   the text mirror 0x6651B8 for the lui 0x3F4C / ori 0xCCCC pair behind it.
   The first sweep only looked beside reads of the main flag 0x6655A0, which
   is how these and C were missed. The bytes 4C 3F a memory view shows are
   the lui half alone (0.796875); with the ori the value is 0.8. */
#define ADDR_GlyphAdvance_D_Lui   0x33BE9C /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_D_Ori   0x33BEA0 /* 0x3421CCCC }               */
#define ADDR_GlyphAdvance_E_Lui   0x33C354 /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_E_Ori   0x33C358 /* 0x3421CCCC }               */
#define ADDR_GlyphAdvance_F_Lui   0x3BA6D8 /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_F_Ori   0x3BA6DC /* 0x3421CCCC }               */

/* Three more text paths on the text mirror, which that sweep also missed:
   0x33D150, 0x33D4B0 and 0x33DCA8 each multiply a glyph width by 0.8 on the
   wide arm. */
#define ADDR_GlyphAdvance_G_Lui   0x33D1AC /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_G_Ori   0x33D1B0 /* 0x3421CCCC }               */
#define ADDR_GlyphAdvance_H_Lui   0x33D50C /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_H_Ori   0x33D510 /* 0x3421CCCC }               */
#define ADDR_GlyphAdvance_I_Lui   0x33DD10 /* 0x3C013F4C } 0.8f          */
#define ADDR_GlyphAdvance_I_Ori   0x33DD14 /* 0x3421CCCC }               */

/*
    Nine more copies of the 2D x squash, and two constants that move with it,
    from a sweep that traced every lone lui of 0.75, 4/3, 16/9 and 9/16 in the
    image back to its gate. All lone luis of 0.75.

    Most of these gate on neither 0x6655A0 nor the text mirror but on a third
    copy of the flag: *(0x664EDC)+0x694. 0x664EDC holds the game object G
    (stored at 0x316748), G+0x680 is main::game.option, and +0x14 of that is
    the raw bool the native setter 0x313260 stores (sw at 0x313270) before it
    sets the main flag. So all three copies change together.

      frame grab   0x1B8320 copies the race picture into the 128x128 capture
                   texture, cropping u about the centre by the squash; its
                   flag is a2, loaded from 0x6655A0 at 0x1EA6CC
      photo fit    0x1B9D98 fits a captured image into a rect: the width is
                   DIVIDED by the squash first, then pglScale(0.75, 1, 1)
      preview      0x1E1008, the photo pause preview. Portrait is
                   pglScale(0.75, 1, 1) re-centred at x = 174 + 154 * squash
                   (289.5). Landscape scales x by photo aspect * 0.75 / (4/3);
                   only the 0.75 changes, the 4/3 is a constant
      image fit    0x1E2D30: the box width is divided by the squash, and the
                   fitted x and width multiplied back by it
      slide show   0x1912A8 (mSlideShowFace) stretches y by 4/3 instead of
                   squashing x, and multiplies its y anchors by 0.75 to match.
                   The stretch is 0.75 * R - the squash's inverse
      photo view   0x19E938 (mPhotoViewFace): the quad's half-width
*/
#define ADDR_XScale_FrameGrab     0x1B8410 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_PhotoFit      0x1B9E14 /* 0x3C013F40  0.75f, divisor */
#define ADDR_XScale_PhotoTile     0x1B9E98 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_PreviewTall   0x1E10D8 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_PreviewWide   0x1E11A4 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_ImageFit      0x1E2DB0 /* 0x3C013F40  0.75f, divisor */
#define ADDR_XScale_ImageFitBack  0x1E2E34 /* 0x3C013F40  0.75f          */
#define ADDR_XScale_SlideShowY    0x191378 /* 0x3C013F40  0.75f, on y    */
#define ADDR_XScale_PhotoView     0x19ECCC /* 0x3C013F40  0.75f          */
#define ADDR_PreviewTall_X_Lui    0x1E10BC /* 0x3C014390 } 289.5f        */
#define ADDR_PreviewTall_X_Ori    0x1E10C0 /* 0x3421C000 }               */
#define ADDR_SlideShow_Y_Lui      0x19135C /* 0x3C013FAA } 1.3333329f    */
#define ADDR_SlideShow_Y_Ori      0x191360 /* 0x3421AAA7 }               */

/*
    The three TV geometry blocks 0x315330 decides from. Script sees the live
    copy as main::game.option.physical_monitor_size, an array of eleven:

      +0x00 flag    +0x04 zoom    +0x08 distance  +0x0C width  +0x10 height
      +0x14 aspect  +0x18 scale   +0x1C..+0x28 b[0..3]

    Its attribute callback is 0x161510. The setter reads element 0 as an int
    and the rest as floats, stores all eleven into the options object at
    +0x24..+0x4F, and returns - it applies nothing itself. 0x312EB0 and
    0x315368 fill the same place from these blocks when wide_mode changes.

      0x6F9780  4:3    275 x 205 mm at 238.157 - exactly a 60 degree
                       horizontal field of view, so the numbers are designed
      0x6F97B0  16:9   425 x 240 mm at 276.046
      0x6F97E0  "subaru", chosen by NAME rather than by the wide_mode
                       attribute: 337.9 x 270.3 mm at 1045, with four trailing
                       fields the other two leave zero. A special display
                       configuration. Its ratio is 1.25, so it never reads as
                       wide.

    Nothing here patches them. The one consumer confirmed is 0x315330, which
    reads width and height for the wide test. The menu model views do not
    read them either: the vertical scale 0x353600 hands pglScale is +0x14 of the DISPLAY
    object at 0x665578 - set to 1.0 by its ctor 0x3525A8 - a different object
    from the options copy despite sharing the offset of "aspect". Nothing in
    the image touches 0x665540..0x665578 directly, so the two cannot be the
    same memory. The static copies at 0x664E20/50/80 are only ever addressed,
    never read directly.
*/
#define ADDR_TvGeometry_43     0x6F9780
#define ADDR_TvGeometry_169    0x6F97B0
#define ADDR_TvGeometry_Subaru 0x6F97E0

/*
    The nine layout tables the walker consumes, and the lui/addiu PAIR that
    materialises each. Both words of a pair must be repointed together.

    THESE PAIRINGS WERE ESTABLISHED BY DECODING THE BRANCH STRUCTURE, NOT BY
    PROXIMITY, and two of them defeat a backward scan for the nearest lui:

      0x1C8698 is the delay slot of a beql at 0x1C8694, so it is ANNULLED on
      fall-through and reaches only the addiu at 0x1C86C8 (table H). The lui at
      0x1C869C, which a proximity scan picks instead, belongs to the table I
      addiu at 0x1C86A4.

      0x1C85B4 is a plain beq delay slot whose addiu is at the branch TARGET,
      0x1C85D4.

    Every table address has the high bit set in its low half, so every one of
    them is lui 0x6F with a NEGATIVE addiu. See MIPS_HI in MakerList.c.
*/
#define ADDR_LayoutTable_A 0x6EC818 /* 4 recs  */
#define ADDR_LayoutTable_B 0x6EC8D0 /* 4 recs  */
#define ADDR_LayoutTable_C 0x6EC988 /* 14 recs */
#define ADDR_LayoutTable_D 0x6ECBA8 /* 11 recs */
#define ADDR_LayoutTable_E 0x6ECD58 /* 3 recs  */
#define ADDR_LayoutTable_F 0x6ECDE8 /* 13 recs */
#define ADDR_LayoutTable_G 0x6ECFE0 /* 14 recs */
#define ADDR_LayoutTable_H 0x6ED200 /* 9 recs  */
#define ADDR_LayoutTable_I 0x6ED368 /* 13 recs */
#define LAYOUT_RECORD_SIZE 36

/* The lettering is NOT address order: table H (0x6ED200) sits below table I
   (0x6ED368). The letters follow the order the selection code tests them. */
#define ADDR_LayoutRef_F_Lui    0x1C85B4  /* beq delay slot, target 0x1C85D0 */
#define ADDR_LayoutRef_F_Addiu  0x1C85D4
#define ADDR_LayoutRef_G_Lui    0x1C8630
#define ADDR_LayoutRef_G_Addiu  0x1C8638
#define ADDR_LayoutRef_H_Lui    0x1C8698  /* beql delay slot - ANNULLED      */
#define ADDR_LayoutRef_H_Addiu  0x1C86C8
#define ADDR_LayoutRef_I_Lui    0x1C869C
#define ADDR_LayoutRef_I_Addiu  0x1C86A4
#define ADDR_LayoutRef_E_Lui    0x1C8788
#define ADDR_LayoutRef_E_Addiu  0x1C8794
#define ADDR_LayoutRef_B_Lui    0x1C87E0
#define ADDR_LayoutRef_B_Addiu  0x1C87E8
#define ADDR_LayoutRef_A_Lui    0x1C8810
#define ADDR_LayoutRef_A_Addiu  0x1C8814
#define ADDR_LayoutRef_C_Lui    0x1C8840
#define ADDR_LayoutRef_C_Addiu  0x1C884C
#define ADDR_LayoutRef_C2_Lui   0x1C8888
#define ADDR_LayoutRef_C2_Addiu 0x1C8894
#define ADDR_LayoutRef_D_Lui    0x1C88D8
#define ADDR_LayoutRef_D_Addiu  0x1C88E4
#define ADDR_LayoutRef_E2_Lui   0x1C8928
#define ADDR_LayoutRef_E2_Addiu 0x1C8934

/* --- wide_mode, the script attribute, and the extra aspect modes.

       main::game.option.wide_mode goes through one callback, 0x157AB0, for
       both get and set. On a set it converts the value to an int and calls the
       native setter 0x313260 - but computes the argument as (value != 0) with
       `sltu $a1, $zero, $v0` in the delay slot, so natively the attribute is
       a bool whatever script writes. The setter stores it as the full word at
       options+0x14 and re-copies the TV geometry block at options+0x24. On a
       get the callback wraps that word in an int through 0x4E9B00, unmasked.

       The word can carry a mode index. The options block, options+0x08 to
       +0x122F, is saved and loaded verbatim - 0x3135F8 and 0x313758, slots
       +0x28 and +0x30 of the options vtable 0x6C1C20, from the whole-game
       save (G = *(0x664EDC), options at G+0x680) and MOption.save/load alike
       - with no clamp, and the whole-game CRC is taken over the finished
       buffer. Every native reader only tests it against zero: 0x312EE0,
       0x315368 (from the ctor's 0x3124F8), 0x1DFACC, 0x26BDE8 and the photo
       code through *(0x664EDC)+0x694 (0x1912E8, 0x19ECC0, 0x1B9E08,
       0x1E1058, 0x1E2DA4). The ctor body 0x312310 stores 0. RaceAspect
       rewrites the delay slot to pass the raw value (daddu $a1, $v0, $zero),
       retargets the call, hands the setter the bool, then stores the index in
       the word itself; the game's own getter reads it back.

       Nothing native turns a loaded index back into constants. A load copies
       the word in, and apply (0x312578) - after a whole-game load (0x316D08),
       at boot (0x316744), from MOption.apply (0x161ACC) and after script
       copies options in (0x14608C) - never reads +0x14. RaceAspect hooks
       apply's ratioIsWide call (ADDR_ratioIsWide_JAL_apply; a0 there is
       options+0x24) to re-apply the saved mode. MOption.load does not apply
       by itself.

       0x313260 has one other caller, 0x5F9B50, inside a native accessor pair
       (0x5F9B38 get, 0x5F9B48 set) that nothing in the image calls or keeps a
       pointer to - dead, as far as a scan of both segments can tell. So the
       attribute is the only live way to set wide_mode. */
#define OPTIONS_WIDE_MODE          0x14
#define OPTIONS_TV_GEOMETRY        0x24
#define ADDR_WideMode_Attribute    0x157AB0
#define ADDR_WideMode_SetCall      0x157B00  /* jal 0x313260               */
#define INSN_JAL_WideModeNativeSet 0x0C0C4C98
#define ADDR_WideMode_SetValue     0x157B04  /* its delay slot             */
#define INSN_SLTU_A1_ZERO_V0       0x0002282B /* sltu  $a1, $zero, $v0     */
#define INSN_DADDU_A1_V0           0x0040282D /* daddu $a1, $v0, $zero     */
#define ADDR_WideMode_NativeSet    0x313260
#define ADDR_WideMode_GetCall      0x157B38  /* jal 0x4E9B00               */
#define INSN_JAL_WideModeGetCtor   0x0C13A6C0
#define ADDR_WideMode_GetCtor      0x4E9B00

/* The kernel's FlushCache syscall (100): 0 writes the data cache back, 2
   invalidates the instruction cache. The game's own stub, called from 58
   places - addiu $v1, $zero, 100 / syscall / jr $ra. */
#define ADDR_FlushCache            0x5DA2E0
#define INSN_FLUSHCACHE_FIRST      0x24030064

/* --- the menu pointer's matrix.

   Every frame the cursor update (0x3A8888 draws the pointer on its own, at
   0x3A9024) rebuilds the pointer's matrix from nothing: identity (0x3C3090 at
   0x3A8FDC), rotate by the aim angle (0x3C2F28 at 0x3A8FE8, with ctx+0x6F4
   loaded into $f12 in the delay slot), then translate by (-w/2, 0) (0x3C2EE0 at
   0x3A8FFC) so the image's top-centre is the hotspot, and installs it with
   0x3D6B40. All in the 640-wide menu space, so on a stretched display a tilted
   pointer comes out sheared - and nothing a script sets survives the rebuild.

   The helpers work on the 3x3 at matrix+0x10 and PRE-multiply (M = Op * M,
   row vectors), so the first one applied acts last on the pointer. The image
   has two other callers of 0x3C2F28; only the one at 0x3A8FE8 is the cursor's. */
#define ADDR_CursorMatrix_RotateCall 0x3A8FE8
#define INSN_JAL_MatrixRotate        0x0C0F0BCA
#define ADDR_Matrix_Scale            0x3C2EA8  /* (m, sx, sy): row0 *= sx, row1 *= sy */
#define INSN_MATRIX_SCALE_FIRST      0x24840010
#define ADDR_Matrix_Rotate           0x3C2F28  /* (m, degrees) */
#define INSN_MATRIX_ROTATE_FIRST     0x3C013C8E
