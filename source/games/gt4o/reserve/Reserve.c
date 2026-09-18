#include "core/ps2/Sio.h"

#include "DevRam.h"
#include "MemDiag.h"

/*
    The dev RAM reserve's own code (pcsx2 builds).

    linkfile puts every object built from this folder into .devram, at
    DEVRAM_US, between the two words below; DevRam_ReserveLoaded() checks both.
    That RAM belongs to the game's pool unless DevRam_Enable() has moved the
    pool into PCSX2's extra memory, so nothing here may run - and no hook may
    point here - unless both of those said yes. init() calls
    DevRam_InstallReserveHooks() only then.

    To put a hook in the reserve: add its source to this folder and to
    GAME_SRCS_pcsx2 in game.mk, and call its installer below. Any object from
    this folder counts against DEVRAM_RESERVE (8 MB), and the link fails if the
    reserve overflows.
*/

const unsigned int DevRam_ReserveHead __attribute__((section(".devram.head"), used)) = DEVRAM_RESERVE_MAGIC;
const unsigned int DevRam_ReserveTail __attribute__((section(".devram.tail"), used)) = DEVRAM_RESERVE_MAGIC;

void DevRam_InstallReserveHooks(void)
{
    Sio_Puts("[gt4hooks] dev RAM reserve: live\n");
    MemDiag_Install();
}
