#include <stddef.h>
#include <stdio.h>

#include "RaceHud.h"
#include "RaceAspect.h"            /* their script functions register here too */
#include "RaceGpb.h"
#include "core/ps2/Memory.h"
#include "core/Log.h"
#include "core/ps2/Sio.h"       /* constant messages skip _sprintf entirely */
#include "core/game/String.h"      /* __strcmp */
#include "GameFunctions/Adhoc.h"

/* Hook: RaceHud
   Purpose: Let a script set the colours the race HUD draws with, from a menu,
            for every race after it; and give MOption the plugin's functions.
   How: Displaces the last member registration in MOption to add them;
        rewrites the instructions and data that build each colour; and
        displaces both callers of RaceDisplay::relayout to put the colours a
        display keeps in itself into each display as it is built.

   See RaceHud.h for what script sees and for the HUD maps, and
   HUD_COLOUR_SITES in the target header for every colour site.
*/

/* ---- adhoc value helpers --------------------------------------------------

   Written here rather than shared with MStorage.c and MCarGarage.c. Both of
   those made the same call for the same reason: lifting three of eight helpers
   into a common header leaves a worse split than either whole one.

   `slot` is the ADDRESS of the word holding the object, which is exactly what
   argv + n is. */

static void* AdhocVTableOf(hObject** slot)
{
    char* obj;

    if (slot == NULL || *slot == NULL)
        return NULL;

    obj = *(char**)slot;
    return *(void**)(obj + 4);
}

int RaceHud_IsNumber(hObject** slot)
{
    void* vt = AdhocVTableOf(slot);

    return vt == (void*)ADDR_hInt_VTable || vt == (void*)ADDR_hFloat_VTable;
}

#define AdhocIsNumber RaceHud_IsNumber

int RaceHud_IsString(hObject** slot)
{
    return AdhocVTableOf(slot) == (void*)ADDR_hString_VTable;
}

#define AdhocIsString RaceHud_IsString

int RaceHud_ToInt(hObject** slot)
{
    char* obj;
    char* entry;
    int (*fn)(void*);

    if (slot == NULL || *slot == NULL)
        return 0;

    obj   = *(char**)slot;
    entry = (char*)(*(void**)(obj + 4)) + ADHOC_VT_TOINT;
    fn    = *(int (**)(void*))(entry + 4);

    return fn(obj + *(short*)entry);
}

#define AdhocToInt RaceHud_ToInt

char* RaceHud_CStr(hObject** slot)
{
    char* obj;
    char* entry;
    char* (*fn)(void*);

    if (slot == NULL || *slot == NULL)
        return NULL;

    obj   = *(char**)slot;
    entry = (char*)(*(void**)(obj + 4)) + ADHOC_VT_CSTR;
    fn    = *(char* (**)(void*))(entry + 4);

    return fn(obj + *(short*)entry);
}

#define AdhocCStr RaceHud_CStr

/* Return an int, and release our reference the way m_ping does. */
void RaceHud_ReturnInt(HObject* return_value, int value)
{
    HInt* result;

    HInt_HInt((HInt*)&result, value);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

#define ReturnInt RaceHud_ReturnInt

/*
    A REAL hNil, not the null handle the return slot already holds. Both read as
    nil in a comparison, but only this one survives `if (v)`: mJumpZero loads
    the object's vtable at 0x5006E8 with no null check and calls through it.
    MStorage.c found this the hard way.
*/
static void ReturnNil(HObject* return_value)
{
    HObject* nil;

    HNil_HNil((HObject*)&nil);
    ADHOC_MakeRef(return_value, nil);
    HValue_dtor((void*)&nil, 2);
}

/* ---- colour channel order -------------------------------------------------

   THE ONE THING IN THIS FILE THAT CAN GO QUIETLY WRONG, so it lives in exactly
   these two functions and nowhere else.

   The engine word is 0xAABBGGRR - bytes in memory R, G, B, A. Script sees
   0xAARRGGBB, which is what a human writes. Anchors that settle it: the A-Spec
   orange #E68600 is stored 0x800086E6 and red #FE0300 is stored 0x800003FE.

   Alpha is the second asymmetry and it belongs here too rather than as a rule
   somewhere else. The engine's opaque is 0x80, not 0xFF - 0x1D8598 multiplies
   the stored alpha by the element's own +0x0C before handing it to pglColor,
   so a stored 0x80 is a ceiling, not a guarantee. Script therefore works in
   0..255 and these two scale it, with rounding chosen so that a value written
   and read back returns unchanged.
*/
static unsigned int ColourToScript(unsigned int game)
{
    unsigned int a = (game >> 24) & 0xFF;
    unsigned int b = (game >> 16) & 0xFF;
    unsigned int g = (game >> 8)  & 0xFF;
    unsigned int r =  game        & 0xFF;

    a = (a * 255u + 64u) / 128u;
    if (a > 255u)
        a = 255u;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

static unsigned int ColourToGame(unsigned int script)
{
    unsigned int a = (script >> 24) & 0xFF;
    unsigned int r = (script >> 16) & 0xFF;
    unsigned int g = (script >> 8)  & 0xFF;
    unsigned int b =  script        & 0xFF;

    a = (a * 128u + 127u) / 255u;
    if (a > 128u)
        a = 128u;

    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* ---- the colour sites -------------------------------------------------------

   One row per site, from HUD_COLOUR_SITES in the target header, which says
   what a row of each kind holds. Rows with the same name are one slot: a set
   writes every one of them, a get reads the first.

   No HUD exists while a script runs, so nothing here touches a live one. A
   colour goes where the next race reads it - into the instructions that build
   it (PAIR), through a stub its pglColor4f call is pointed at (TINT), through
   a stub that hands a call its own colour where one register serves two
   elements (ARG), into the array the needle is drawn from (DATA) - or, for a
   colour each display keeps in itself and nothing builds from code, into
   every display as it is built (FIELD). Each holds until it is set again or
   the game is switched off; nothing is saved.
*/
#define HC_PAIR  0
#define HC_TINT  1
#define HC_FIELD 2
#define HC_DATA  3
#define HC_ARG   4

typedef struct HudColourSite
{
    const char*  name;
    unsigned int kind;
    unsigned int a, aw;
    unsigned int b, bw;
} HudColourSite;

#define HC_ROW(name, kind, a, aw, b, bw) { name, HC_##kind, a, aw, b, bw },

static const HudColourSite g_sites[] = { HUD_COLOUR_SITES(HC_ROW) };

#define HC_COUNT ((int)(sizeof(g_sites) / sizeof(g_sites[0])))

/*
    What script has set that has no home in the game's own code or data: the
    FIELD rows' colours, the three floats each tint stub loads, and the packed
    colour each ARG stub loads. g_fieldSet says which FIELD rows script has
    set, since a shipped colour can be 0. g_live says every site checked out
    at install.
*/
static unsigned int  g_field[HC_COUNT]    = { 0 };
static unsigned char g_fieldSet[HC_COUNT] = { 0 };
static float         g_tint[HUD_TINT_STUBS][3] __attribute__((used)) = { { 1.0f, 1.0f, 1.0f } };
static unsigned int  g_arg[HUD_ARG_STUBS]      __attribute__((used)) = { 0 };
static int           g_live = 0;

/* The game's own FlushCache syscall stub: 0 writes the data cache back to
   memory, 2 invalidates the instruction cache. */
#define GameFlushCache(mode) ((void (*)(int))ADDR_FlushCache)(mode)

/*
    The tint stubs. A TINT site's call to pglColor4f lands on its stub instead,
    with the game's arguments already in place - the grey in $f12..$f14, alpha
    in $f15, the delay slot run. The stub puts the three floats script set in
    place of the grey and goes on to pglColor4f, which returns to the game.

    The ARG stubs do the same for a packed colour: an ARG site's call to the
    colour helper lands on its stub with the colour in $a0 and the alpha
    factor in $f12, and the stub puts the colour script set in $a0 before it
    goes on to the helper.

    Assembly, because the game's float arguments and this toolchain's do not
    line up (see HOOK_CursorRotate in RaceAspect.c). One per slot, so a stub
    needs no lookup: it knows its own colour.
*/
#define RH_STR2(x) #x
#define RH_STR(x)  RH_STR2(x)

#define TINT_STUB(k, off)                                                     \
    ".globl RaceHud_Tint" #k "\n"                                             \
    "RaceHud_Tint" #k ":\n"                                                   \
    "    lui   $at, %hi(g_tint + " #off ")\n"                                 \
    "    addiu $at, $at, %lo(g_tint + " #off ")\n"                            \
    "    lwc1  $f12, 0($at)\n"                                                \
    "    lwc1  $f13, 4($at)\n"                                                \
    "    li    $t9, " RH_STR(ADDR_pglColor4f) "\n"                            \
    "    jr    $t9\n"                                                         \
    "    lwc1  $f14, 8($at)\n"

#define ARG_STUB(k, off)                                                      \
    ".globl RaceHud_Arg" #k "\n"                                              \
    "RaceHud_Arg" #k ":\n"                                                    \
    "    lui   $a0, %hi(g_arg + " #off ")\n"                                  \
    "    lw    $a0, %lo(g_arg + " #off ")($a0)\n"                             \
    "    li    $t9, " RH_STR(ADDR_HudColour_AlphaMul) "\n"                    \
    "    jr    $t9\n"                                                         \
    "    nop\n"

__asm__(
    ".pushsection .text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".align 2\n"
    TINT_STUB(0, 0)  TINT_STUB(1, 12) TINT_STUB(2, 24) TINT_STUB(3, 36)
    TINT_STUB(4, 48) TINT_STUB(5, 60) TINT_STUB(6, 72)
    ARG_STUB(0, 0)
    ".set pop\n"
    ".popsection\n"
);

typedef char RaceHud_OneStubPerTintSlot[(HUD_TINT_STUBS == 7) ? 1 : -1];

extern char RaceHud_Tint0[], RaceHud_Tint1[], RaceHud_Tint2[], RaceHud_Tint3[],
            RaceHud_Tint4[], RaceHud_Tint5[], RaceHud_Tint6[];

static char* const g_tintStub[HUD_TINT_STUBS] =
{
    RaceHud_Tint0, RaceHud_Tint1, RaceHud_Tint2, RaceHud_Tint3,
    RaceHud_Tint4, RaceHud_Tint5, RaceHud_Tint6,
};

typedef char RaceHud_OneStubPerArgSlot[(HUD_ARG_STUBS == 1) ? 1 : -1];

extern char RaceHud_Arg0[];

static char* const g_argStub[HUD_ARG_STUBS] = { RaceHud_Arg0 };

/* A jal to a stub, as the word that replaces a site's call. */
static unsigned int JalTo(const char* stub)
{
    return 0x0C000000u | (((unsigned int)stub >> 2) & 0x03FFFFFFu);
}

static float FloatOf(unsigned int bits)
{
    union { unsigned int u; float f; } pun;

    pun.u = bits;
    return pun.f;
}

/* An instruction's 16-bit immediate replaced, its opcode and registers kept. */
static void SetImmediate(unsigned int addr, unsigned int imm)
{
    volatile unsigned int* w = (volatile unsigned int*)addr;

    *w = (*w & 0xFFFF0000u) | (imm & 0xFFFFu);
}

/*
    A TINT slot's colour, as the three floats its stub loads.

    Each channel scales the grey the game passes: 255 is the game's own, less
    is darker. So white leaves the texture exactly as the game draws it - the
    meter faces' 1.2 included - and alpha plays no part: the element's own
    fade still arrives in $f15.
*/
static void SetTint(const HudColourSite* s, unsigned int argb)
{
    float  grey = FloatOf(s->bw) / 255.0f;
    float* t    = g_tint[s->b];

    t[0] = grey * (float)((argb >> 16) & 0xFF);
    t[1] = grey * (float)((argb >> 8)  & 0xFF);
    t[2] = grey * (float)( argb        & 0xFF);

    *(volatile unsigned int*)s->a = JalTo(g_tintStub[s->b]);
}

static unsigned int TintColour(const HudColourSite* s)
{
    float        grey = FloatOf(s->bw);
    float*       t    = g_tint[s->b];
    unsigned int out  = 0xFF000000u;
    int          i;

    /* Never set: the call still goes straight to pglColor4f. */
    if (*(volatile unsigned int*)s->a == s->aw)
        return 0xFFFFFFFFu;

    for (i = 0; i < 3; i++)
    {
        int c = (int)(t[i] * 255.0f / grey + 0.5f);

        if (c < 0)
            c = 0;
        if (c > 255)
            c = 255;
        out |= (unsigned int)c << (16 - 8 * i);
    }
    return out;
}

/*
    One colour into every site of a slot. Returns how many took it.

    PAIR and TINT rewrite instructions, and DATA writes an array the needle's
    DMA reads straight from memory, so both caches are dealt with before any
    race can use them: the data cache written back, the instruction cache
    invalidated. This is only ever reached from the script VM, in a menu, so
    none of the code it rewrites is running.
*/
static int SetSlot(const char* name, int vertex, unsigned int argb)
{
    unsigned int packed = ColourToGame(argb);
    int          wrote  = 0;
    int          i, k;

    if (name == NULL)
        return 0;

    for (i = 0; i < HC_COUNT; i++)
    {
        const HudColourSite* s = &g_sites[i];

        if (__strcmp(s->name, name) != 0)
            continue;

        switch (s->kind)
        {
            case HC_PAIR:
                SetImmediate(s->a, packed >> 16);
                SetImmediate(s->b, packed);
                break;

            case HC_TINT:
                SetTint(s, argb);
                break;

            case HC_ARG:
                g_arg[s->b] = packed;
                break;

            case HC_FIELD:
                g_field[i]    = packed;
                g_fieldSet[i] = 1;
                break;

            case HC_DATA:
                if (vertex >= (int)s->aw)
                    return 0;
                for (k = 0; k < (int)s->aw; k++)
                    if (vertex < 0 || vertex == k)
                        *(volatile unsigned int*)(s->a + k * 4) = packed;
                break;
        }
        wrote++;
    }

    if (wrote)
    {
        GameFlushCache(0);
        GameFlushCache(2);
    }
    return wrote;
}

/* The colour a slot's first site holds now: what script set, or the game's
   own. */
static int GetSlot(const char* name, int vertex, unsigned int* argb)
{
    int i;

    if (name == NULL)
        return 0;

    for (i = 0; i < HC_COUNT; i++)
    {
        const HudColourSite* s = &g_sites[i];

        if (__strcmp(s->name, name) != 0)
            continue;

        switch (s->kind)
        {
            case HC_PAIR:
                *argb = ColourToScript(((*(volatile unsigned int*)s->a & 0xFFFFu) << 16) |
                                        (*(volatile unsigned int*)s->b & 0xFFFFu));
                return 1;

            case HC_TINT:
                *argb = TintColour(s);
                return 1;

            case HC_ARG:
                *argb = ColourToScript(g_arg[s->b]);
                return 1;

            case HC_FIELD:
                *argb = ColourToScript(g_fieldSet[i] ? g_field[i] : s->aw);
                return 1;

            case HC_DATA:
                if (vertex < 0 || vertex >= (int)s->aw)
                    return 0;
                *argb = ColourToScript(*(volatile unsigned int*)(s->a + vertex * 4));
                return 1;
        }
        return 0;
    }
    return 0;
}

/* The FIELD colours script has set, into a display. Only a RaceDisplay or one
   of its two subclasses - the offsets are the base class's. */
static void PutFields(char* display)
{
    unsigned int vptr;
    int          i;

    if (display == NULL)
        return;

    vptr = *(unsigned int*)(display + RD_VPTR);
    if (vptr != ADDR_RaceDisplay_VTable &&
        vptr != ADDR_RaceLicenseDisplay_VTable &&
        vptr != ADDR_RaceChallengeDisplay_VTable)
        return;

    for (i = 0; i < HC_COUNT; i++)
        if (g_sites[i].kind == HC_FIELD && g_fieldSet[i])
            *(unsigned int*)(display + g_sites[i].a) = g_field[i];
}

#if RACEHUD_LAYOUT
/* ===========================================================================
   THE SHELVED LAYOUT FUNCTIONS - RaceHudGet and RaceHudSet, as they were.
   See RACEHUD_LAYOUT in RaceHud.h for why they are off.
   =========================================================================== */

/* ---- segment bounds, used only for plausibility checks --------------------

   From the two PT_LOADs of SCUS_974.36:
     RWX  va 0x00100000  filesz 0x0055FBBC   -> code and rodata
     RW   va 0x0065FC00  filesz 0x000D45FC   -> initialised data, vtables
   The RW segment's memsz is larger (0x008D45FC) because .bss follows, but a
   vtable is initialised data, so the FILE size is the right bound here. */
#define SEG_CODE_LO 0x00100000u
#define SEG_CODE_HI 0x0065FBBCu
#define SEG_DATA_LO 0x0065FC00u
#define SEG_DATA_HI 0x007341FCu

/* ---- the addressable elements ---------------------------------------------

   Offsets are from the RaceDisplay base and come from the member map in
   RaceHud.h. Sizes there are ctor spacing, and sizeof(RaceDisplay) is 21692,
   so every offset below is in range by construction.

   Not listed, deliberately:
     * the five non-elements at 200, 204, 232, 4400 and 6008. They are members
       of RaceDisplay that are not RaceDisplayObjectBase and have no vptr at
       +0x14, so the setter dispatch would read a garbage word.
     * cluster sub-elements. 0x1C77C0 positions those relative to the SELECTED
       PANEL at display+0x14, from 16-byte records built on the stack, and the
       panel identity changes with meter kind - so a display-relative offset
       cannot name them at all.
     * the RaceLicenseDisplay and RaceChallengeDisplay members above 21692.
       They exist only on those two subclasses, and gating them would mean
       carrying a per-subclass upper bound for two elements nobody has asked
       for yet. */

#define HE_NOSTICKY 0x01   /* repositioned outside relayout - see below */

typedef struct HudElement
{
    const char*    name;
    unsigned short offset;
    unsigned char  flags;
} HudElement;

static const HudElement g_elements[] =
{
    { "panel.onboard",  280,   0 },
    { "panel.simple",   3380,  0 },
    { "steering",       4260,  0 },
    { "map",            4296,  0 },
    { "message",        4464,  0 },
    { "lap",            4800,  0 },
    { "rank",           4952,  0 },
    { "laptimes",       5040,  0 },
    { "time0",          5112,  0 },
    { "time1",          5224,  0 },
    { "time2",          5336,  0 },
    { "time3",          5448,  0 },
    { "time4",          5560,  0 },
    { "panel0",         5672,  0 },
    { "panel1",         5700,  0 },
    { "panel2",         5728,  0 },
    { "panel3",         5756,  0 },
    { "speed0",         5784,  0 },
    { "speed1",         5896,  0 },
    { "event",          6012,  0 },
    { "message1",       8780,  0 },
    { "message2",       9116,  0 },
    { "message3",       9452,  0 },
    { "message4",       9788,  0 },
    { "message5",       10124, 0 },
    { "message6",       10460, 0 },
    { "message7",       10796, 0 },
    { "message8",       11132, 0 },
    { "replaymode",     11468, 0 },

    /*
        THE CAR ICON IS DRIVEN EVERY TICK, NOT BY THE LAYOUT TABLES, and a
        sticky override on it would silently lose every frame.

        0x1C6B58 recomputes it from RaceEventDisplay's live position:
          addiu $s2,$s3,6012 / addiu $s0,$s3,11812
          lh $a1,4($s2) / addiu $a1,$a1,-82
          lh $a2,6($s2) / addiu $a2,$a2,22
          jal 0x1D7330                       <- the BASE setPos, direct, so
                                                even a subclass override would
                                                not catch it
        reached from the per-tick update 0x1C35D8 -> 0x1C3948 -> 0x1C6098.
        So a plain set still works for one frame, sticky is refused, and the
        supported way to move the car icon is to move "event", which drags it
        by -82 / +22.
    */
    { "caricon",        11812, HE_NOSTICKY },

    { "signboard",      17060, 0 },
    { "refuel",         17152, 0 },
    { "tooltip",        17192, 0 },
    { "music",          20356, 0 },
    { "panel.prius",    20424, 0 },
    { NULL,             0,     0 }
};

/*
    The HUD-type map lives in MAP 2 in RaceHud.h, as a comment and a typedef,
    and NOT as a table here. It was a table until the plugin ran 2.9 KB past
    the 0x10310 bytes of dead space it has to live in - and nothing dispatches
    on it, so it was pure documentation paying for itself in a budget that
    cannot afford it. The typedef is kept for whoever builds the feature that
    needs the data, along with every value it would have held.
*/

/* ---- live displays --------------------------------------------------------

   Up to two, because RaceSplitDisplay builds exactly two (container+8 and
   container+8+21692, per getDisplay at 0x603158). They are captured from the
   relayout hook and held SORTED ASCENDING BY ADDRESS, which is what makes
   index 0 mean player 1 rather than "whichever relayout happened to run
   first". Capture order is not player order and nothing in the game promises
   it would be.
*/
static void* g_display[2] = { 0, 0 };

/* ---- overrides ------------------------------------------------------------

   relayout re-runs the whole nine-table walk plus the hardcoded pass at
   0x1C77C0, so anything a script set is gone the next time the display goes
   dirty. An element marked sticky is re-applied from here, from the tail of
   the relayout hook.

   Sixteen entries is ample, and the number is chosen against the plugin's
   memory budget rather than against the element count: no shipped layout table
   lays out more than 14 elements, and the whole plugin has to fit in the
   0x10310 bytes of dead Omron IME space at 0x68A480. Each entry is 20 bytes.

   Explicitly initialised so it lands in .data. The build passes
   -fno-zero-initialized-in-bss and the injector copies only the plugin's text
   segment, so an uninitialised static would not be in the injected image at
   all - the same trap MakerList.c documents.
*/
#define RACEHUD_MAX_OVERRIDES 8

#define OV_X      0x01
#define OV_Y      0x02
#define OV_W      0x04
#define OV_H      0x08
#define OV_ALIGNH 0x10
#define OV_ALIGNV 0x20
#define OV_ALL    0x3F

typedef struct ElemState
{
    short         x, y, w, h;
    unsigned char alignH, alignV;
} ElemState;

typedef struct HudOverride
{
    unsigned short offset;
    unsigned char  player;
    unsigned char  props;
    ElemState      state;
} HudOverride;

static HudOverride g_over[RACEHUD_MAX_OVERRIDES] = { { 0, 0, 0, { 0, 0, 0, 0, 0, 0 } } };
static int         g_overCount = 0;

/* ---- lookup ---------------------------------------------------------------- */

static const HudElement* FindElement(const char* name)
{
    int i;

    if (name == NULL)
        return NULL;

    for (i = 0; g_elements[i].name != NULL; i++)
        if (__strcmp(g_elements[i].name, name) == 0)
            return &g_elements[i];

    return NULL;
}

/*
    The live RaceDisplay for a player, or NULL.

    THIS IS THE LIFETIME GATE and it is the reason the count exists in the
    header. A captured pointer is script-reachable asynchronously to the game:
    a script can call in after a race has ended and the display has been
    destroyed but before any new relayout has refreshed this table. Freed EE
    memory keeps its old contents, so the vptr alone would still look right and
    the next step would be a vtable call into a destroyed object.

    ADDR_RaceDisplay_LiveCount reaching zero is exactly that window - the dtor
    decrements it at 0x1C01F8 - so the table is dropped when it does. The vptr
    test after it is a second, weaker check against a display that was replaced
    rather than merely freed.
*/
static char* DisplayFor(int player)
{
    char*        d;
    unsigned int vptr;

    if (*(volatile int*)ADDR_RaceDisplay_LiveCount <= 0)
    {
        g_display[0] = NULL;
        g_display[1] = NULL;
        return NULL;
    }

    if (player < 0 || player > 1)
        return NULL;

    d = (char*)g_display[player];

    if (d == NULL)
        return NULL;

    vptr = *(unsigned int*)(d + RD_VPTR);

    if (vptr != ADDR_RaceDisplay_VTable &&
        vptr != ADDR_RaceLicenseDisplay_VTable &&
        vptr != ADDR_RaceChallengeDisplay_VTable)
        return NULL;

    return d;
}

/*
    An element's address, once its vptr looks like one.

    Every offset in g_elements is a checked compile-time constant, so this is a
    sanity check rather than a security boundary - which is precisely why the
    raw-offset form of the API was dropped. Still worth doing: it catches a
    display captured from a class whose members do not line up, and it costs
    two loads.
*/
static int LooksLikeElement(const char* obj)
{
    unsigned int vp = *(unsigned int*)(obj + RDO_VPTR);
    unsigned int fn;

    if (vp < SEG_DATA_LO || vp >= SEG_DATA_HI || (vp & 3) != 0)
        return 0;

    fn = *(unsigned int*)(vp + RDO_VT_SETPOS + 4);

    if (fn < SEG_CODE_LO || fn >= SEG_CODE_HI || (fn & 3) != 0)
        return 0;

    return 1;
}

static char* ElementPtr(const HudElement* e, char* display)
{
    char* obj = display + e->offset;

    return LooksLikeElement(obj) ? obj : NULL;
}

/*
    Call one of the three geometry setters through the element's vtable.

    This is the walker's own idiom, from 0x1C8A04..0x1C8A24: read the 8-byte
    entry, take the s16 thisAdjust from its first half and the function from
    its second, and offset `this` by the adjust. thisAdjust is 0 in every HUD
    class, and it is read anyway because the game reads it.

    ELEMENTS ONLY. Slot 7 on a RaceDisplayObjectBase is setPos; slot 7 on
    RaceDisplay is setLayoutMode, and RaceDisplay keeps its vptr at +0x00
    instead of +0x14. Handing a display to this function would dispatch
    setLayoutMode with an x and a y. The "screen" pseudo-element never comes
    near here - it has its own path in ScreenGet / ScreenSet.
*/
static void CallSetter(char* elem, int slotByteOff, int a, int b)
{
    char* entry = *(char**)(elem + RDO_VPTR) + slotByteOff;
    short adj   = *(short*)entry;
    void (*fn)(void*, int, int) = *(void (**)(void*, int, int))(entry + 4);

    fn(elem + adj, a, b);
}

static void ReadElem(char* obj, ElemState* s)
{
    s->x      = *(short*)(obj + RDO_X);
    s->y      = *(short*)(obj + RDO_Y);
    s->w      = *(short*)(obj + RDO_W);
    s->h      = *(short*)(obj + RDO_H);
    s->alignH = *(unsigned char*)(obj + RDO_ALIGNH);
    s->alignV = *(unsigned char*)(obj + RDO_ALIGNV);
}

/*
    Write whichever properties `props` names, in the game's own order.

    ALIGN, THEN SIZE, THEN POSITION. The three panel classes recompute their
    children inside setPos (RaceOnboardPanel 0x1CECC8, RaceSimplePanel
    0x1CD778, RacePriusPanel 0x1CFC08) and read the align and size they were
    given while doing it, so position has to go last. The walker at 0x1C8970
    calls them in exactly this order for the same reason.

    Alpha is the exception to going through the vtable: +0x0C has no setter
    anywhere in the hierarchy, so it is a direct 32-bit store, applied after
    the three calls.
*/
static void WriteElem(char* obj, const ElemState* want, unsigned int props)
{
    ElemState now;

    ReadElem(obj, &now);

    if (props & (OV_ALIGNH | OV_ALIGNV))
        CallSetter(obj, RDO_VT_SETALIGN,
                   (props & OV_ALIGNH) ? want->alignH : now.alignH,
                   (props & OV_ALIGNV) ? want->alignV : now.alignV);

    if (props & (OV_W | OV_H))
        CallSetter(obj, RDO_VT_SETSIZE,
                   (props & OV_W) ? want->w : now.w,
                   (props & OV_H) ? want->h : now.h);

    if (props & (OV_X | OV_Y))
        CallSetter(obj, RDO_VT_SETPOS,
                   (props & OV_X) ? want->x : now.x,
                   (props & OV_Y) ? want->y : now.y);
}

/* ---- the override list ---------------------------------------------------- */

static HudOverride* FindOverride(unsigned short offset, int player)
{
    int i;

    for (i = 0; i < g_overCount; i++)
        if (g_over[i].offset == offset && g_over[i].player == (unsigned char)player)
            return &g_over[i];

    return NULL;
}

static void DropOverride(HudOverride* o)
{
    int i = (int)(o - g_over);

    if (i < 0 || i >= g_overCount)
        return;

    g_over[i] = g_over[g_overCount - 1];
    g_overCount--;
}

/* ---- display capture ------------------------------------------------------ */

static void SortDisplays(void)
{
    /* Ascending, so index 0 is RaceSplitDisplay's container+8 - player 1. */
    if (g_display[0] != NULL && g_display[1] != NULL &&
        (unsigned int)g_display[0] > (unsigned int)g_display[1])
    {
        void* t      = g_display[0];
        g_display[0] = g_display[1];
        g_display[1] = t;
    }
    else if (g_display[0] == NULL && g_display[1] != NULL)
    {
        g_display[0] = g_display[1];
        g_display[1] = NULL;
    }
}

static void NoteDisplay(void* d)
{
    int live;

    if (d == NULL)
        return;

    live = *(volatile int*)ADDR_RaceDisplay_LiveCount;

    /* One display alive means the split-screen pair is gone, so a second
       remembered pointer is stale by definition. */
    if (live <= 1)
    {
        g_display[0] = d;
        g_display[1] = NULL;
        return;
    }

    if (g_display[0] != d && g_display[1] != d)
    {
        if (g_display[0] == NULL)
            g_display[0] = d;
        else if (g_display[1] == NULL)
            g_display[1] = d;
        else
            g_display[1] = d;
    }

    SortDisplays();
}

static void ReapplyOverrides(void* display)
{
    int player;
    int i;

    if (g_overCount == 0)
        return;

    player = (g_display[0] == display) ? 0 : ((g_display[1] == display) ? 1 : -1);

    if (player < 0)
        return;

    for (i = 0; i < g_overCount; i++)
    {
        char* obj;

        if (g_over[i].player != (unsigned char)player || g_over[i].props == 0)
            continue;

        obj = (char*)display + g_over[i].offset;

        /* The display was just relaid out, so the element is certainly there -
           but this runs on the game's own tick and a bad write here would fault
           inside the render path rather than in a script, so it is checked
           anyway. */
        if (!LooksLikeElement(obj))
            continue;

        WriteElem(obj, &g_over[i].state, g_over[i].props);
    }
}

/* ---- the "screen" pseudo-element -----------------------------------------

   RaceDisplay itself, kept well away from CallSetter for the slot-7 reason
   above. Everything here is a plain field read; the one writable property goes
   through the game's own mask setter.
*/
static int ScreenGet(char* d, int prop, int* out)
{
    switch (prop)
    {
        case RHP_LAYOUT_MODE: *out = *(int*)(d + RD_LAYOUT_MODE);          return 1;
        case RHP_SUB_MODE:    *out = *(int*)(d + RD_SUB_MODE);             return 1;
        case RHP_METER_HINT:  *out = *(int*)(d + RD_METER_HINT);           return 1;
        case RHP_MASK:        *out = *(int*)(d + RD_MASK_BASE);            return 1;
        case RHP_HUD_TYPE:    *out = *(unsigned char*)(d + RD_HUD_TYPE);   return 1;
        default:                                                           return 0;
    }
}

static int ScreenSet(char* d, int prop, int value)
{
    switch (prop)
    {
        /*
            The game's own setter, 0x1C8308: `which` 0 stores to +0x5C and 2 to
            +0x60, and it then re-runs vtable slot 36 itself at 0x1C8344 to
            recompute the effective mask at +0x58. Writing the field directly
            would leave +0x58 stale until something else went dirty.
        */
        case RHP_MASK:
            ((void (*)(void*, int, int))ADDR_RaceDisplay_setMask)(d, 0, value);
            return 1;

        /*
            REFUSED, deliberately, though it is one byte and would "work".

            Writing 1 here switches the texture set to "tt" and turns on a whole
            family of code that has never executed in this build - and it needs
            /race_display/tt/<version>/display.gpb to exist, which retail does
            not ship. Until someone has put those assets on the host filesystem
            and watched it boot, an API that flips it is a crash with a property
            id. The map in RaceHud.h says how it works; enabling it is a
            separate, deliberate piece of work.
        */
        case RHP_HUD_TYPE:
            Sio_Puts("racehud: hud type is read only - see MAP 2 in RaceHud.h\n");
            return 0;

        default:
            return 0;
    }
}

/*
    Everything every entry point has to do before it can act: check the two
    fixed arguments, pick up the optional player, find the live display, and
    resolve the element - unless the name is "screen", which resolves to the
    display itself and leaves `obj` NULL.

    `playerArg` differs between Get and Set only because their argument lists
    do. Returns 0 if anything at all was wrong; the caller then answers nil or
    0 without having touched memory.
*/
typedef struct Target
{
    char*             display;
    char*             obj;      /* NULL for the "screen" pseudo-element */
    const HudElement* e;
    int               prop;
    int               player;
} Target;

static int Resolve(int argc, hObject** argv, int playerArg, Target* t)
{
    const char* name;

    t->display = NULL;
    t->obj     = NULL;
    t->e       = NULL;
    t->prop    = 0;
    t->player  = 0;

    if (argc < playerArg || !AdhocIsString(argv + 0) || !AdhocIsNumber(argv + 1))
        return 0;

    t->prop = AdhocToInt(argv + 1);

    if (argc > playerArg && AdhocIsNumber(argv + playerArg))
        t->player = AdhocToInt(argv + playerArg);

    t->display = DisplayFor(t->player);

    if (t->display == NULL)
        return 0;

    name = AdhocCStr(argv + 0);

    if (__strcmp(name, "screen") == 0)
        return 1;

    t->e = FindElement(name);

    if (t->e == NULL)
        return 0;

    t->obj = ElementPtr(t->e, t->display);

    return t->obj != NULL;
}

/*
    MOption::RaceHudGet(elem, prop [, player]) -> value, or nil

    nil means "could not read", and there are only three reasons: no display is
    live (the normal state in a menu), the element name is unknown, or the
    property does not apply to it.
*/
void f_RaceHudGet(HObject* return_value, int argc, hObject** argv)
{
    Target    t;
    ElemState s;
    int       prop;

    if (!Resolve(argc, argv, 2, &t))
    {
        ReturnNil(return_value);
        return;
    }

    prop = t.prop;

    if (t.obj == NULL)
    {
        int v;

        if (ScreenGet(t.display, prop, &v))
            ReturnInt(return_value, v);
        else
            ReturnNil(return_value);

        return;
    }

    ReadElem(t.obj, &s);

    switch (prop)
    {
        case RHP_X:      ReturnInt(return_value, s.x);      return;
        case RHP_Y:      ReturnInt(return_value, s.y);      return;
        case RHP_W:      ReturnInt(return_value, s.w);      return;
        case RHP_H:      ReturnInt(return_value, s.h);      return;
        case RHP_ALIGNH: ReturnInt(return_value, s.alignH); return;
        case RHP_ALIGNV: ReturnInt(return_value, s.alignV); return;

        case RHP_STICKY:
            ReturnInt(return_value,
                      FindOverride(t.e->offset, t.player) != NULL ? 1 : 0);
            return;

        default:
            ReturnNil(return_value);
            return;
    }
}

/*
    MOption::RaceHudSet(elem, prop, value [, player]) -> 1 or 0

    Zero means nothing was written. It never writes some of what was asked.
*/
void f_RaceHudSet(HObject* return_value, int argc, hObject** argv)
{
    Target       t;
    ElemState    want;
    HudOverride* o;
    int          prop;
    unsigned int bit = 0;

    /* Resolve FIRST: it is what establishes argc >= 3, and until it has,
       argv + 2 is whatever happened to be on the stack. */
    if (!Resolve(argc, argv, 3, &t) || !AdhocIsNumber(argv + 2))
    {
        ReturnInt(return_value, 0);
        return;
    }

    prop = t.prop;

    if (t.obj == NULL)
    {
        ReturnInt(return_value,
                  ScreenSet(t.display, prop, AdhocToInt(argv + 2)));
        return;
    }

    /*
        Sticky first, because turning it on snapshots whatever is on the element
        RIGHT NOW - including anything set earlier in the same script - and that
        snapshot is what comes back after each relayout. Turning it off drops
        the entry and lets the game win again.
    */
    if (prop == RHP_STICKY)
    {
        int on = AdhocToInt(argv + 2);

        o = FindOverride(t.e->offset, t.player);

        if (!on)
        {
            if (o != NULL)
                DropOverride(o);

            ReturnInt(return_value, 1);
            return;
        }

        if (t.e->flags & HE_NOSTICKY)
        {
            Sio_Puts("racehud: that element is repositioned every tick"
                     " - sticky refused\n");
            ReturnInt(return_value, 0);
            return;
        }

        if (o == NULL)
        {
            if (g_overCount >= RACEHUD_MAX_OVERRIDES)
            {
                Sio_Puts("racehud: override list full\n");
                ReturnInt(return_value, 0);
                return;
            }

            o = &g_over[g_overCount++];
            o->offset = t.e->offset;
            o->player = (unsigned char)t.player;
        }

        ReadElem(t.obj, &o->state);
        o->props = OV_ALL;

        ReturnInt(return_value, 1);
        return;
    }

    ReadElem(t.obj, &want);

    switch (prop)
    {
        case RHP_X:      want.x      = (short)AdhocToInt(argv + 2);         bit = OV_X;      break;
        case RHP_Y:      want.y      = (short)AdhocToInt(argv + 2);         bit = OV_Y;      break;
        case RHP_W:      want.w      = (short)AdhocToInt(argv + 2);         bit = OV_W;      break;
        case RHP_H:      want.h      = (short)AdhocToInt(argv + 2);         bit = OV_H;      break;
        case RHP_ALIGNH: want.alignH = (unsigned char)AdhocToInt(argv + 2); bit = OV_ALIGNH; break;
        case RHP_ALIGNV: want.alignV = (unsigned char)AdhocToInt(argv + 2); bit = OV_ALIGNV; break;

        default:
            ReturnInt(return_value, 0);
            return;
    }

    WriteElem(t.obj, &want, bit);

    /* Keep a sticky snapshot in step, so the next relayout restores what the
       script just set rather than what it snapshotted earlier. */
    o = FindOverride(t.e->offset, t.player);

    if (o != NULL)
    {
        ReadElem(t.obj, &o->state);
        o->props = OV_ALL;
    }

    ReturnInt(return_value, 1);
}
#endif /* RACEHUD_LAYOUT */

/*
    Both callers of RaceDisplay::relayout come here, among them the build of
    every new display - which is where the FIELD colours go in, once the game
    has laid the display out. Nothing rewrites them afterwards, so later
    relayouts only put the same words back.

    MAKE_JAL retargets the jal word only, so each site's delay slot still runs
    exactly as it shipped.
*/
void HOOK_RaceDisplay_relayout(void* display, int hint)
{
    ((void (*)(void*, int))ADDR_RaceDisplay_relayout)(display, hint);

    PutFields((char*)display);
#if RACEHUD_LAYOUT
    NoteDisplay(display);
    ReapplyOverrides(display);
#endif
}

/* ---- the adhoc surface ----------------------------------------------------

   NOTHING IN THE VM CHECKS ARITY, so every one of these checks argc itself
   before touching argv, and checks the type of everything it reads.
*/

/*
    MOption::RaceHudPing() -> 42

    Trivial on purpose. It proves the registration hook fired, that the name
    resolves from script, that the function ABI is right and that the handle
    dance around a return value works - without touching the HUD. If the game
    boots and this answers 42 from both a menu script and a race script, every
    other function in this file is reachable. If it crashes at boot, the crash
    is in the registration and nothing else here is implicated: that is the
    bisection MStorage.c had to do the hard way.
*/
void f_RaceHudPing(HObject* return_value, int argc, hObject** argv)
{
    (void)argc;
    (void)argv;

    ReturnInt(return_value, 42);
}

/*
    MOption::RaceHudSetColor(slot, argb)          -> 1 or 0
    MOption::RaceHudSetColor(slot, vertex, argb)  -> 1 or 0

    argb is 0xAARRGGBB as a human writes it, alpha 0..255. vertex matters only
    to "needle", whose six vertices can be set one at a time (0..5) or all
    together (-1, or leaving it out); any other slot ignores it. A fourth
    argument - the player the old form took - is ignored: no colour here
    belongs to one display.

    On a texture the colour multiplies it, so 0xFFFFFFFF leaves it as the game
    draws it and anything else darkens or tints it. Text and flat shapes take
    the colour as it is. Alpha is the colour's own opacity, which the element's
    fade then scales; a tint slot (see SetTint) takes no alpha.
*/
void f_RaceHudSetColor(HObject* return_value, int argc, hObject** argv)
{
    int          vertex = -1;
    unsigned int argb;

    if (!g_live || argc < 2 || !AdhocIsString(argv + 0) || !AdhocIsNumber(argv + 1))
    {
        ReturnInt(return_value, 0);
        return;
    }

    if (argc >= 3 && AdhocIsNumber(argv + 2))
    {
        vertex = AdhocToInt(argv + 1);
        argb   = (unsigned int)AdhocToInt(argv + 2);
    }
    else
        argb = (unsigned int)AdhocToInt(argv + 1);

    ReturnInt(return_value, SetSlot(AdhocCStr(argv + 0), vertex, argb) > 0);
}

/*
    MOption::RaceHudGetColor(slot [, vertex]) -> 0xAARRGGBB, or nil

    What the slot's first site holds now: the colour script set, or the game's
    own. vertex picks one of the needle's six, 0 if it is left out. nil means
    no such slot. An opaque colour reads back negative - the VM's integers are
    signed.
*/
void f_RaceHudGetColor(HObject* return_value, int argc, hObject** argv)
{
    int          vertex = 0;
    unsigned int argb;

    if (!g_live || argc < 1 || !AdhocIsString(argv + 0))
    {
        ReturnNil(return_value);
        return;
    }

    if (argc >= 2 && AdhocIsNumber(argv + 1))
        vertex = AdhocToInt(argv + 1);

    if (GetSlot(AdhocCStr(argv + 0), vertex, &argb))
        ReturnInt(return_value, (int)argb);
    else
        ReturnNil(return_value);
}

/* ---- registration --------------------------------------------------------- */

int RaceHud_WordIs(unsigned int addr, unsigned int expect)
{
    unsigned int got = *(volatile unsigned int*)addr;

    if (got == expect)
        return 1;

    LOG("racehud: %x holds %x, expected %x\n", addr, got, expect);
    return 0;
}

#define WordIs RaceHud_WordIs

/*
    Add the plugin's functions to MOption.

    This replaces the CALL that registers "save", the last member registration
    in the module's InitClass, so we re-register save ourselves and then add
    our own. Being last means ours land after every stock member and the game's
    own HValue_dtor at 0x16380C accounts for whichever ran last.

    tempHValue is passed straight through rather than by address: the caller
    does `daddu $a0, $sp, $zero` at 0x1637F4, so it already IS the slot address.

    NO HValue_dtor BETWEEN ANY OF THESE. MStorage.c bisected this three ways on
    its own module: redirecting the call and registering nothing boots;
    registering an extra member without the dtor boots; registering it WITH the
    dtor crashes at boot, every time. The mechanism is still unexplained. The
    cost of leaving it out is at most one reference on a handle whose module
    lives for the whole session, which is a trade worth making against a boot
    crash. MCarGarage.c does release between its two calls and works, so the
    difference is not simply "one dtor too many" - do not generalise from
    either file without testing.
*/
void HOOK_ExtendMOption(void* tempHValue, hModule* module, char* name,
                        Adhoc_method_cb method)
{
    /* The registration we displaced. */
    hModule_defineMethod(tempHValue, module, name, method);

    /* Functions, not methods: nothing here has an adhoc receiver, and this
       way any script can call them without holding an object. */
    hModule_defineFunction(tempHValue, module, "RaceHudPing",     &f_RaceHudPing);
    hModule_defineFunction(tempHValue, module, "RaceHudGetColor", &f_RaceHudGetColor);
    hModule_defineFunction(tempHValue, module, "RaceHudSetColor", &f_RaceHudSetColor);
    hModule_defineFunction(tempHValue, module, "RaceAspectSetMaxWidth", &f_RaceAspectSetMaxWidth);
    hModule_defineFunction(tempHValue, module, "RaceAspectGetMaxWidth", &f_RaceAspectGetMaxWidth);
    hModule_defineFunction(tempHValue, module, "RaceHudSetGpb", &f_RaceHudSetGpb);
    hModule_defineFunction(tempHValue, module, "RaceHudGetGpb", &f_RaceHudGetGpb);
#if RACEHUD_LAYOUT
    hModule_defineFunction(tempHValue, module, "RaceHudGet",      &f_RaceHudGet);
    hModule_defineFunction(tempHValue, module, "RaceHudSet",      &f_RaceHudSet);
#endif
}

/* Every PAIR, TINT and ARG site must hold exactly what the target header says
   it shipped with, and every TINT and ARG row must name a stub that exists. */
static int ColoursVerified(void)
{
    int i;

    for (i = 0; i < HC_COUNT; i++)
    {
        const HudColourSite* s = &g_sites[i];

        if (s->kind == HC_PAIR && (!WordIs(s->a, s->aw) || !WordIs(s->b, s->bw)))
            return 0;
        if (s->kind == HC_TINT && (s->b >= HUD_TINT_STUBS || !WordIs(s->a, s->aw)))
            return 0;
        if (s->kind == HC_ARG && (s->b >= HUD_ARG_STUBS || !WordIs(s->a, s->aw)))
            return 0;
    }

    return WordIs(ADDR_FlushCache, INSN_FLUSHCACHE_FIRST);
}

void RaceHud_InstallHooks(void)
{
    int i;

    /*
        All-or-nothing, as MakerList.c does it: a mismatch anywhere means the
        target header does not describe this executable, and none of the rest
        of it should be trusted either.
    */
    if (!WordIs(ADDR_MOption_save_register, INSN_JAL_hModule_defineMethod) ||
        !WordIs(ADDR_RaceDisplay_relayout_JAL_build,  INSN_JAL_RaceDisplay_relayout) ||
        !WordIs(ADDR_RaceDisplay_relayout_JAL_update, INSN_JAL_RaceDisplay_relayout) ||
        !ColoursVerified())
    {
        Sio_Puts("racehud: this is not the executable those addresses describe;"
                 " not patching\n");
        return;
    }

    MAKE_JAL(ADDR_MOption_save_register, &HOOK_ExtendMOption);
    MAKE_JAL(ADDR_RaceDisplay_relayout_JAL_build,  &HOOK_RaceDisplay_relayout);
    MAKE_JAL(ADDR_RaceDisplay_relayout_JAL_update, &HOOK_RaceDisplay_relayout);

    /* No colour changes here. Each ARG call goes to its stub from now on,
       loaded with the colour it shipped with, so the two elements it parts
       look as they always did until script sets one of them; every other
       site stays as it shipped. No race has drawn yet, and init() flushes
       the caches after the last patch. */
    for (i = 0; i < HC_COUNT; i++)
        if (g_sites[i].kind == HC_ARG)
        {
            g_arg[g_sites[i].b] = g_sites[i].bw;
            *(volatile unsigned int*)g_sites[i].a = JalTo(g_argStub[g_sites[i].b]);
        }

    g_live = 1;
}
