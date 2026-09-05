#include <stdbool.h>
#include <stdio.h>

#include "core/ps2/Memory.h"

#include "core/Target.h"
#include "core/game/IO.h"
#include "core/Log.h"
#include "core/hooks/HostFs.h"
#include "core/hooks/HOutput.h"

/*
    Tourist Trophy plugin entry point.

    Tourist Trophy runs the same engine as Gran Turismo 4 Online, so this
    mirrors that game's entry point. The differences are:

      * every address comes from Targets/<build>.h, so one source tree builds
        all three retail regions;
      * the plugin's memory is taken from the game's pool rather than borrowed
        from a dead region - see below;
      * CameraSys, MStorage, MCarGarage and the adhoc modules are not installed.
        Those target classes and adhoc modules that either do not exist in
        Tourist Trophy or whose addresses have not been resolved for it.
*/

void init(void)
{
    /*
        Reserve the plugin's memory from the game's generic pool.

        Shortly after startup the game hands that pool everything from
        POOL_BASE_GLOBAL up to the top of RAM and zero-fills it. The plugin
        is linked at the pool's base (PLUGIN_BASE_ADDRESS == POOL_BASE_VALUE), so
        the base is raised past it here. The pool then begins above the plugin
        and the allocator can never hand the region out.

        This runs early enough to matter: the injector patches its call to
        INVOKER over the first `ei` in the executable, at INVOKER_HOOK_SITE
        (0x1001F8 in every retail build), which is far ahead of the pool setup
        at POOL_SETUP_FUNC.

        The GT4 game instead borrows the unused Omron wnn/fep globals at
        0x68A480. Tourist Trophy does not link that code at all, so there is no
        equivalent dead region to use here.
    */
    PATCH_INT(POOL_BASE_GLOBAL, POOL_BASE_NEW);

    /*
        newlib's sbrk break pointer ships holding the same address. sbrk is dead
        code in these builds - _sbrk_r has no callers and its address appears
        nowhere - so malloc can never reach the plugin either way, but raising it
        too costs one store and leaves nothing pointing into the reserved block.
    */
    PATCH_INT(SBRK_BREAK_GLOBAL, POOL_BASE_NEW);

    /*
        Note there is deliberately no NOP of the pool's memset here.

        The GT4 game has to wipe that call (GLOBAL_MEMSET_OFFSET in its
        makefile) because it links its code *inside* the range the pool clears,
        so leaving the memset alone would erase it. This game raises the pool
        base instead, which means the clear already starts above the plugin and
        never overlaps it.

        Killing the memset anyway would only stop the game zeroing its own pool.
        crt0 clears .bss up to _end, but the pool runs from _end to the top of
        RAM and nothing else clears that, so suppressing it hands the allocator
        a region full of whatever was in RAM. GLOBAL_MEMSET_OFFSET is still
        emitted in the target headers for reference, and if the reserve
        arithmetic is ever changed in a way that puts the plugin back inside the
        cleared range, this is the line that would need to come back.
    */

#ifdef USE_DEV_RAM
    /* MEMORY_SIZE_OFFSET holds the top of RAM, which is what bounds the pool. */
    PATCH_INT(MEMORY_SIZE_OFFSET, (128 * 0x100000) - 0x8000);
#endif

    HostFs_InstallHooks();
    HOutput_InstallHooks();

#if HOSTFS_PRINT
    LOG("TTHooks: HostFS installed (" BUILD_NAME ", " REGION_NAME ")\n");
#endif
}

void __attribute__((optimize("O3"))) INVOKER(void)
{
    asm("ei\n");
    asm("addiu $ra, -4\n");

    init();
}

int main(void)
{
    return 0;
}
