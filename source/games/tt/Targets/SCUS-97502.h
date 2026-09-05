#pragma once

/*
    Tourist Trophy (USA)
    Boot ELF : SCUS_975.02
    Build ID : SCUS-97502   (GTImageLoader RSA exponent 90301)
    Executable date: 2006-02-28

    GENERATED FILE - do not edit by hand.
    Produced by tools/gen_headers.py from the resolver output in
    tools/tt_final.json. Addresses were derived from the known
    Gran Turismo 4 Online (SCUS_974.36) addresses used by GT4Hooks by
    matching operand-stripped instruction fingerprints, then confirmed
    against call-graph and constant-pool context. See tools/resolve_tt.py.
*/

#define GAME_REGION_US 1
#define REGION_NAME "US"
#define BUILD_NAME "SCUS-97502"
#define ELF_NAME "SCUS_975.02"

/* Virtual function table slots - patched with HOOK_FUNC_ADDR.
   These are DATA addresses: the function-pointer word inside
   PlayStation2::FileDeviceRo2's vtable, not the function itself. */
#define ADDR_PlayStation2_FileDeviceRo_openStream    0x5DA374
#define ADDR_PlayStation2_FileDeviceRo_closeStream   0x5DA36C
#define ADDR_PlayStation2_FileDeviceRo2_GetFileEntry 0x5DA3C4
#define ADDR_PlayStation2_FileDeviceRo2_readStat     0x5DA3AC
#define ADDR_PDISTD_FileDevice_readStream            0x5DA384

/* Real function addresses - patched in place with HOOK, or called. */
#define ADDR_PlayStation2_FileDeviceRo_rawReadStream    0x4808F8
#define ADDR_PlayStation2_FileDeviceRo2_IOControlStream 0x480C30
#define ADDR_PlayStation2_FileDeviceRo_pipe             0x481320
#define ADDR_PDISTD_FileInternalStream_ioctl            0x47F340
#define ADDR_PDISTD_FileStatus_FileStatus               0x47EFD8
#define ADDR_PageManager_GetEntryForFile                0x47BA30
#define ADDR_PDISTD_FileDevice_setExpander              0x47D640
#define ADDR_PDISTD_FileDevice_removeExpander           0x47D690
#define ADDR_UnitArenaBase_allocate                     0x4AD988
#define ADDR_UnitArenaBase_free                         0x4AD9B8
#define ADDR_PlaystationX_LockFileName                  0x4816D0
#define ADDR_PlayStationX_UnlockFileName                0x4816F0

/* Stream helpers called by FileDeviceRo::openStream / ::closeStream. */
#define ADDR_FileStream_acquire    0x4BEB20
#define ADDR_FileStream_flushWrite 0x4BEC28
#define ADDR_FileStream_release    0x4BEC58

/* UnitArenaBase globals that back FileInternalStream::State. */
#define ADDR_UnitArena_StreamState  0x76FCC0
#define ADDR_UnitArena_StateField0C 0x76FCE0

/* HOutput - the debug output class stubbed out in release builds.
   ADDR_HOutput_Handler is a global function POINTER holding a jr-$ra
   stub, not a function. Resolved by tools/resolve_houtput.py. */
#define ADDR_HOutput_Handler 0x57C42C
#define ADDR_ADHOC_printf    0x5261B0
#define ADDR_ADHOC_debug     0x526860

/* libc. */
#define ADDR_sprintf 0x4A6500
#define ADDR_print   0x4A6448
#define ADDR_strlen  0x4A8368
#define ADDR_strcmp  0x4A8268
#define ADDR_strstr  0x4F88A0
#define ADDR_strcpy  0x4F7B74

/* sce / PS2 kernel. */
#define ADDR_sceOpen              0x4FF970
#define ADDR_sceClose             0x4FFC00
#define ADDR_sceRead              0x4FFFB8
#define ADDR_sceLSeek             0x4FFD78
#define ADDR_sceGetStat           0x501558
#define ADDR_sceStdioConvertError 0x505B78
#define ADDR_sceMcGetDir          0x4DBE08
#define ADDR_sceMcSync            0x4DBAF8

/* The first two instruction words of FileDeviceRo::rawReadStream.
   readStream restores these to briefly un-hook the function so that
   streamed files still reach the original implementation. Read out of
   this build's own code by tools/gen_headers.py. */
#define RAWREADSTREAM_INSN_0         0x27BDFFB0
#define RAWREADSTREAM_INSN_1         0xFFB70038

/*
    Injection parameters.

    Tourist Trophy has no dead wnn/fep region to borrow the way the GT4
    game does, so the plugin takes its memory from the game's generic
    pool instead. The pool's base is a link-time constant sitting in
    initialised data; init() raises it past the plugin before the pool is
    ever built, so the allocator can never hand out the plugin's range.
    See tools/make_injection.py for the checks behind these values.
*/
/* where the plugin is linked - keep in sync with makefile.tt */
#define PLUGIN_BASE_ADDRESS          0x7A6A00
/* bytes reserved for the plugin at PLUGIN_BASE_ADDRESS */
#define PLUGIN_RESERVE_BYTES         0x20000
/* holds the pool's base address; raised by PLUGIN_RESERVE_BYTES in init() */
#define POOL_BASE_GLOBAL             0x5981C0
/* that global's shipped value (PLUGIN_BASE_ADDRESS is this, aligned up) */
#define POOL_BASE_VALUE              0x7A69CC
/* what init() writes to POOL_BASE_GLOBAL: past the reserved block */
#define POOL_BASE_NEW                0x7C6A00
/* newlib sbrk's break pointer, initialised to the same address; sbrk is
   dead code in these builds but is raised too so nothing disagrees */
#define SBRK_BREAK_GLOBAL            0x5A30C0
/* holds the top of RAM; the pool runs from the base up to here */
#define MEMORY_SIZE_OFFSET           0x5981C4
/* jal that zero-fills the pool. NOT patched: raising the pool base
   already moves the clear above the plugin, and suppressing it would only
   rob the game of its zeroed pool. Emitted for reference - see tt/main.c */
#define GLOBAL_MEMSET_OFFSET         0x4A44E0
/* the function containing that call (informational) */
#define POOL_SETUP_FUNC              0x4A44A0
/* first `ei`, where the injector patches in jal INVOKER (informational) */
#define INVOKER_HOOK_SITE            0x1001F8
