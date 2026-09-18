#pragma once

#include <stdbool.h>

/*
    Dev RAM: PCSX2's 128 MB mode, in pcsx2 builds only. See DevRam.c.
*/

/* Where the pool goes with 128 MB: above 32 MB, starting where the shipped pool
   starts modulo 1 MB (0x020D0FB0), so every block lands at the same offset, with
   the same alignment padding, as on a PS2 - reserve/MemDiag.c plays the stock
   heap alongside it - and ending where the course area starts. Needs the
   target header first (POOL_BASE_VALUE). */
#define DEVRAM_POOL_BASE  (0x02000000 | (((POOL_BASE_VALUE + 15) & ~15) & 0xFFFFF))
#define DEVRAM_POOL_TOP   DEVRAM_COURSE_AREA

/* The course block's room with 128 MB: 24 MB, and the game's 0x100 bytes of
   slack, above the pool. The heap still gives a race its stock-size course
   block, so it stays laid out exactly as on a PS2; reserve/MemDiag.c points
   the course arena - where the course loader and photo mode take the block
   from - here instead. */
#define DEVRAM_COURSE_AREA   0x06700000
#define DEVRAM_COURSE_BLOCK  0x1800000
#define DEVRAM_COURSE_SLACK  0x100

/* The words reserve/Reserve.c opens and closes the reserve with. */
#define DEVRAM_RESERVE_MAGIC 0x52564544 /* "DEVR" */
extern const unsigned int DevRam_ReserveHead;
extern const unsigned int DevRam_ReserveTail;

/* 128 MB present: map it, move the game's pool up into it and leave the low
   RAM to the plugin. Returns false, having changed nothing, otherwise. Must run
   before the pool is built, as init() does. */
bool DevRam_Enable(void);

/* Both words above are where the linker put them, so the reserve was loaded. */
bool DevRam_ReserveLoaded(void);

/* In the reserve itself (reserve/Reserve.c). Call only after DevRam_Enable()
   and DevRam_ReserveLoaded() both said yes. */
void DevRam_InstallReserveHooks(void);
