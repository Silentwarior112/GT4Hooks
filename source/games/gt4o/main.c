#include <stdbool.h>
#include <stdio.h>

#include "core/hooks/HOutput.h"
#include "core/hooks/MakerList.h"
#if PLATFORM_PCSX2
#include "core/hooks/HostFs.h"
#include "DevRam.h"
#include "LicenseReplays.h"
#endif
#include "CameraSys.h"
#include "Adhoc.h"
#include "MStorage.h"
#include "MCarGarage.h"
#include "RaceHud.h"
#include "RaceAspect.h"
#include "RaceGpb.h"
#include "HeapWatch.h"

#include "GameFunctions/PolyphonyGL.h"
#include "GameStructs/PolyphonyGL.h"

#include "GameFunctions/FontManagerClass.h"
#include "GameStructs/FontManagerClass.h"

#include "GameFunctions/Misc.h"

#include "core/game/IO.h"
#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"

/* GLOBAL_MEMSET_OFFSET and MEMORY_SIZE_OFFSET now live in the target header
   alongside every other address, so a second GT4 region only needs a new one
   of those. See gt4o/Targets/SCUS-97436.h. */

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480


#ifndef CAVE2_ADDRESS
#error "CAVE2_US / CAVE2_RESERVE are missing from game.mk - the linkfile needs them"
#endif

#ifndef STARTUP_CALL
#error "STARTUP_SLOT_US / STARTUP_CALL_US are missing from game.mk - INVOKER makes crt0's call with them"
#endif

#if !defined(PLATFORM_PS2) || !defined(PLATFORM_PCSX2)
#error "PLATFORM_PS2 / PLATFORM_PCSX2 come from the makefile - build with build.bat ... for:ps2 or for:pcsx2"
#endif

/*
    Boot tracing over SIO: a line per step of init(), and one either side of the
    game's own pool setup. Formatted by hand rather than with the game's sprintf,
    because this runs before main(). Set BOOT_TRACE to 0 once boot is proven.
*/
#define BOOT_TRACE 0

#if BOOT_TRACE
/* "[gt4hooks] <what>" and then n (0..2) words in hex. */
static void Trace(const char* what, int n, unsigned int a, unsigned int b)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned int v[2];
    char hex[10];
    int i, k;

    v[0] = a;
    v[1] = b;
    Sio_Puts("[gt4hooks] ");
    Sio_Puts(what);
    for (k = 0; k < n; k++)
    {
        hex[0] = ' ';
        for (i = 0; i < 8; i++)
            hex[1 + i] = digits[(v[k] >> (28 - 4 * i)) & 15];
        hex[9] = 0;
        Sio_Puts(hex);
    }
    Sio_Puts("\n");
}
#define TRACE(what, n, a, b) Trace(what, n, a, b)

/* Stands in for the game's one call to POOL_SETUP_FUNC, so the log shows
   whether startup got past init() and what the pool was built from. */
typedef int (*PoolSetupFn)(int, int, int, int);
static int HOOK_PoolSetup(int a0, int a1, int a2, int a3)
{
    int r;

    TRACE("pool setup: base, top", 2, *(volatile unsigned int*)POOL_BASE_GLOBAL,
          *(volatile unsigned int*)MEMORY_SIZE_OFFSET);
    r = ((PoolSetupFn)POOL_SETUP_FUNC)(a0, a1, a2, a3);
    TRACE("pool setup: done", 0, 0, 0);
    return r;
}
#else
#define TRACE(what, n, a, b) do { } while (0)
#endif

/*
    The words linkfile opens and closes .cave2 and .cave2b with. If the loader
    did not put a cave where it was linked - or put only part of it there -
    these will not both be there, and nothing in that cave may be called.
*/
#define CAVE2_MAGIC 0x32564143 /* "CAV2" */
extern const unsigned int _cave2_start[], _cave2_tail[];
extern const unsigned int _cave2b_start[], _cave2b_tail[];

static bool CaveLoaded(const unsigned int* start, const unsigned int* tail,
                       const char* missing)
{
    unsigned int head = start[0];
    unsigned int end  = tail[0];

    TRACE("cave: head, tail", 2, head, end);
    if (head == CAVE2_MAGIC && end == CAVE2_MAGIC)
        return true;

    /* Most likely an ELF built without the cave step: the dead Medius code
       the cave is written over is still there. */
    Sio_Puts(missing);
    return false;
}

/*
    The hooks come in two groups:

      every build     console and PCSX2 alike
      pcsx2 builds    what is PCSX2's alone: HostFS reads through PCSX2's
                      host: device, dev RAM needs its 128 MB option, and
                      license replays come from a folder of PCSX2's own

    game.mk lists the second group's sources in GAME_SRCS_pcsx2, so a ps2 build
    does not even link them. Everything here runs before the game builds its
    pool (see the target header), which dev RAM depends on.
*/
void init()
{
    bool cave2, cave2b;
#if PLATFORM_PCSX2
    bool devram;
#endif

    TRACE("init: start", 0, 0, 0);
    cave2  = CaveLoaded(_cave2_start, _cave2_tail,
                        "[gt4hooks] cave2 is not in this executable;"
                        " RaceHud, RaceAspect and RaceGpb left off\n");
    cave2b = CaveLoaded(_cave2b_start, _cave2b_tail,
                        "[gt4hooks] cave2b is not in this executable;"
                        " RaceGpb and HostFS left off\n");

    /* The game's own zero-fill of its heap (GLOBAL_MEMSET_OFFSET) is left to
       run: none of the plugin lies in the heap, and without it a console hands
       the allocator whatever the BIOS or loader left in RAM. */

    /* ---- every build ------------------------------------------------- */
    TRACE("init: HOutput", 0, 0, 0);
    HOutput_InstallHooks();
    TRACE("init: CameraSys", 0, 0, 0);
    CameraSys_InstallHooks();
    TRACE("init: Adhoc modules", 0, 0, 0);
    ADHOC_InjectNewModules();
    TRACE("init: MStorage", 0, 0, 0);
    MStorage_InstallHooks();
    TRACE("init: MCarGarage", 0, 0, 0);
    MCarGarage_InstallHooks();
    TRACE("init: MakerList", 0, 0, 0);
    MakerList_InstallHooks();

    /* RaceHud and RaceAspect live in the second cave and RaceGpb in the third
       (see linkfile), so each goes in only once its cave's words check out.
       RaceHud must come first only in the sense that it owns the MOption
       registration the others add to - and RaceGpb's functions register
       there, so RaceGpb needs both caves. The three patch disjoint sets of
       words and none reads another's state at init. */
    if (cave2)
    {
        TRACE("init: RaceHud", 0, 0, 0);
        RaceHud_InstallHooks();
        TRACE("init: RaceAspect", 0, 0, 0);
        RaceAspect_InstallHooks();
    }
    if (cave2 && cave2b)
    {
        TRACE("init: RaceGpb", 0, 0, 0);
        RaceGpb_InstallHooks();
    }

    /* ---- pcsx2 builds only ------------------------------------------- */
#if PLATFORM_PCSX2
    /* HostFS lives in the third cave too (see linkfile). */
    if (cave2b)
    {
        TRACE("init: HostFs", 0, 0, 0);
        HostFs_InstallHooks();
    }
    TRACE("init: license replays", 0, 0, 0);
    LicenseReplays_InstallHooks();

    /* The 128 MB mode moves the pool, so it has to happen here, before the
       pool is built. The reserve's hooks go in only if the pool really moved
       out from under them. */
    TRACE("init: DevRam", 0, 0, 0);
    devram = DevRam_Enable();
    if (devram && DevRam_ReserveLoaded())
        DevRam_InstallReserveHooks();
#endif

    /* Every build, last: dev RAM mode's diagnostics take the allocator site
       first when they are on. */
    TRACE("init: HeapWatch", 0, 0, 0);
    HeapWatch_Install();

#if BOOT_TRACE
    if (*(volatile unsigned int*)ADDR_PoolSetup_Call == INSN_JAL_PoolSetup)
        MAKE_JAL(ADDR_PoolSetup_Call, &HOOK_PoolSetup);
#endif

    /* Every patch above went in as stores through the data cache, and a PS2
       fetches instructions from memory, not from that cache: until a dirty line
       is written back the CPU can still run the old instruction, and an
       instruction-cache line that already holds it keeps running it until the
       line is replaced. PCSX2 models neither, which is how a hook can work there
       and not on a console - and INVOKER runs the game's setup, patched code
       and all, the moment init() returns. So, once, after the last patch:
       write the data cache back, then drop the instruction cache. */
    if (*(volatile unsigned int*)ADDR_FlushCache == INSN_FLUSHCACHE_FIRST)
    {
        ((void (*)(int))ADDR_FlushCache)(0);
        ((void (*)(int))ADDR_FlushCache)(2);
    }
    TRACE("init: done", 0, 0, 0);

    //HOOK(ADDR_GranTurismo4_RenderManager_call, HOOK__GranTurismo4_RenderManager_call);
}

/*
    The plugin's entry. The injector turns crt0's `ei` into a jal to here, so
    the `ei` is done here first. The word after it in crt0 is its call into the
    game's early setup (STARTUP_CALL in game.mk), which was left in the hook's
    delay slot - a jump in a delay slot, which PCSX2 skips but a PS2 takes, so
    on a console init() never ran and no hook went in. build.bat makes that
    slot a nop (tools/startup_slot.py), and the call is made here instead,
    after init(): the setup builds the game's pool, and dev RAM mode has to move
    the pool before it does.
*/
void INVOKER()
{
    asm volatile("ei");
    init();
    ((void (*)(void))STARTUP_CALL)();
}

int main()
{
    return 0;
}

// This was just an attempt at an overlay
void HOOK__GranTurismo4_RenderManager_call(void* a1)
{
    //_print("Render tick\n");

    ((void(*)(void*, void*))ADDR_GranTurismo4_GameObjectManager_call)(a1, (void*)ADDR_GranTurismo4_GameObjectPS2_render); // Called in the original

    /*
    pglMatrixMode(0);
    pglPushMatrix();
    pglLoadIdentity();
    pglColor4f(1, 0, 0, 1);
    static MathVector3 vertices[] = (MathVector3[]){
            {-320,  224, 0 },
            { 320,  224, 0 },
            {-320, -224, 0 },
            { 320, -224, 0 }
    };

    static MathVector3 texCoords[] = (MathVector3[]){
            { -32,  32, 0 },
            {  32,  32, 0 },
            { -32, -32, 0 },
            {  32, -32, 0 },
    };

    pgluTriangleStrip3fv(sizeof(vertices), vertices, texCoords, NULL, NULL);
    pglPopMatrix();
    */

    /* This kinda worked, rectangle glitches out though
    static int counter = 0;

    // Create a transparent rectangle
    pglColor(0x40808080);
    pglDisable(PGL_TEST_DepthTest);
    pglEnable(PGL_PrimABE_AlphaBlend);
    pglBlendFunc(1 << 6 | 0 << 4 | 1 << 2 | 0 << 0, 1.0); // In order: D = 1, C = 0, B = 1, A = 0
    pglWcRect2i(4, 4, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 8);

    if (*((int*)0x6651BC) != 0)
    {
        FontManagerClass* class_ = *(FontManagerClass**)0x6651BC;
        class_->Color = 0x80A0A0A0;
        class_->IsAligned = 1;
        class_->AlignMode = 0;
        PDISTD_FontManagerClass_setFont(class_, "hn_14");
        PDISTD_FontManagerClass_setRegion(class_, 4, 4, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 8);
        PDISTD_FontManagerClass_printf(class_, "Counter: %d", counter++);
    }
    */
    
}
