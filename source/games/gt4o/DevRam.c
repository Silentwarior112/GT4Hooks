#include <stdbool.h>

#include "core/Target.h"
#include "core/game/IO.h"
#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"

#include "DevRam.h"

/*
    Dev RAM: PCSX2's 128 MB mode, for pcsx2 builds.

    A PS2 has 32 MB. PCSX2 can expose 128 MB, like a development console
    ("Enable 128MB RAM (Dev Console)", [EmuCore/CPU] ExtraMemory), and when it
    does the game's GetMemorySize() says so. Only then does this:

      1. map 0x02000000..0x08000000 in the TLB - cached, uncached and UCAB -
         in the kernel's spare slots 39..47. PCSX2 maps these itself when it
         boots the game, so on a normal boot the entries only repeat that.
         They are what keeps the uncached mirrors, which the game builds pool
         pointers into, after a save state is loaded into a freshly started
         PCSX2.
      2. move the game's pool up into that RAM: base 0x020D0FB0 - the shipped
         base's offset within 1 MB, so blocks keep their stock alignment - and
         top 0x06700000. That is 70 MB against the shipped 23 MB. The 24 MB
         above it, to 0x07F00100, is the course area (DevRam.h); the pool
         never covers it. The main thread's stack stays where crt0 put it,
         0x01FF8000..0x02000000, below the pool instead of inside it.
      3. leave the low RAM the pool used to cover to the plugin. The dev RAM
         reserve is the first 8 MB of it, from just past .bss (DEVRAM_US and
         DEVRAM_RESERVE in game.mk); the ELF loads its code there directly.

    init() calls this before the pool is built, so these are the values the
    pool setup (POOL_SETUP_FUNC) reads. If the answer is 32 MB, or a word is
    not what the shipped executable holds, nothing is touched: the pool comes
    up at its shipped size, over the reserve, and nothing in the reserve may
    be called.
*/

#define DEVRAM_RAM_SIZE   0x08000000

#if DEVRAM_COURSE_AREA + DEVRAM_COURSE_BLOCK + DEVRAM_COURSE_SLACK > DEVRAM_RAM_SIZE - 0x8000
#error "the dev RAM course area runs past the top of 128 MB"
#endif

/* 16 MB pages, two to an entry: each entry maps 32 MB. */
#define TLB_PAGEMASK_16MB 0x01FFE000

typedef unsigned int (*GetMemorySizeFn)(void);
typedef int (*SetTLBEntryFn)(int index, unsigned int pageMask, unsigned int entryHi,
                             unsigned int entryLo0, unsigned int entryLo1);

/* EntryLo is PFN << 6 | C << 3 | D | V | G, as in the kernel's own table:
   C = 3 cached, 2 uncached, 7 uncached accelerated. */
static const struct { int index; unsigned int hi, lo0, lo1; } s_tlb[] =
{
    { 39, 0x02000000, 0x0008001F, 0x000C001F },   /* cached   */
    { 40, 0x04000000, 0x0010001F, 0x0014001F },
    { 41, 0x06000000, 0x0018001F, 0x001C001F },
    { 42, 0x22000000, 0x00080017, 0x000C0017 },   /* uncached */
    { 43, 0x24000000, 0x00100017, 0x00140017 },
    { 44, 0x26000000, 0x00180017, 0x001C0017 },
    { 45, 0x32000000, 0x0008003F, 0x000C003F },   /* UCAB     */
    { 46, 0x34000000, 0x0010003F, 0x0014003F },
    { 47, 0x36000000, 0x0018003F, 0x001C003F },
};

#define DR_STR2(x) #x
#define DR_STR(x)  DR_STR2(x)

static bool WordIs(unsigned int addr, unsigned int value)
{
    return *(volatile unsigned int*)addr == value;
}

bool DevRam_Enable(void)
{
    unsigned int size;
    unsigned int i;

    if (!WordIs(ADDR_GetMemorySize, INSN_GetMemorySize) ||
        !WordIs(ADDR_SetTLBEntry, INSN_SetTLBEntry))
    {
        Sio_Puts("[gt4hooks] dev RAM: GetMemorySize or SetTLBEntry is not the"
                 " shipped code; staying at 32 MB\n");
        return false;
    }

    size = ((GetMemorySizeFn)ADDR_GetMemorySize)();
    if (size != DEVRAM_RAM_SIZE)
    {
        Sio_Puts("[gt4hooks] dev RAM: 32 MB - PCSX2's 128 MB option is off;"
                 " shipped memory layout\n");
        return false;
    }

    if (!WordIs(POOL_BASE_GLOBAL, POOL_BASE_VALUE) ||
        !WordIs(MEMORY_SIZE_OFFSET, MEMORY_SIZE_VALUE))
    {
        Sio_Puts("[gt4hooks] dev RAM: the pool's globals are not the shipped"
                 " values; staying at 32 MB\n");
        return false;
    }

    for (i = 0; i < sizeof(s_tlb) / sizeof(s_tlb[0]); i++)
        ((SetTLBEntryFn)ADDR_SetTLBEntry)(s_tlb[i].index, TLB_PAGEMASK_16MB,
                                          s_tlb[i].hi, s_tlb[i].lo0, s_tlb[i].lo1);

    PATCH_INT(POOL_BASE_GLOBAL,   DEVRAM_POOL_BASE);
    PATCH_INT(MEMORY_SIZE_OFFSET, DEVRAM_POOL_TOP);

    {
        char line[160];

        _sprintf(line, "[gt4hooks] dev RAM: 128 MB - pool 0x%08X..0x%08X (%d KB),"
                 " course area 0x%08X + %d MB, reserve " DR_STR(DEVRAM_ADDRESS) " + "
                 DR_STR(DEVRAM_RESERVE) "\n",
                 DEVRAM_POOL_BASE, DEVRAM_POOL_TOP, (DEVRAM_POOL_TOP - DEVRAM_POOL_BASE) / 1024,
                 DEVRAM_COURSE_AREA, DEVRAM_COURSE_BLOCK >> 20);
        Sio_Puts(line);
    }
    return true;
}

bool DevRam_ReserveLoaded(void)
{
    if (*(volatile const unsigned int*)&DevRam_ReserveHead == DEVRAM_RESERVE_MAGIC &&
        *(volatile const unsigned int*)&DevRam_ReserveTail == DEVRAM_RESERVE_MAGIC)
        return true;

    Sio_Puts("[gt4hooks] dev RAM: the reserve's code is not in this executable;"
             " its hooks are left off\n");
    return false;
}
