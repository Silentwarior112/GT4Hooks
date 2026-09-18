#include <stddef.h>
#include <stdio.h>

#include "RaceAspect.h"
#include "RaceHud.h"               /* RaceHud_WordIs */
#include "core/ps2/Memory.h"       /* MAKE_JAL, PATCH_INT */
#include "core/ps2/Sio.h"

/*
    This file alone is built for size. The repo compiles at -O0 and the plugin
    has only the 0x10310 bytes of dead Omron IME space to live in; at -O0 the
    runtime modes ran the build 368 bytes past the end of it, into live game
    data. -Os here changes nothing about any other hook - main.c sets its own
    optimisation on INVOKER the same way - and the mode tables are folded at
    compile time either way.

    It also turns on strict aliasing, which is why Immediate() reads a float's
    bits through a union rather than a pointer cast.
*/
#pragma GCC optimize ("Os")

/* See RaceAspect.h for how script drives this and what it deliberately
   leaves alone. */

/* ---- the modes ------------------------------------------------------------

   A mode is a ratio and a HUD width. Every number the game needs is derived
   from them by MODE_VALUES below - at COMPILE time, for every mode in the
   list, so switching between them at run time is a table copy and none of
   this math exists in the plugin. It only has to fit in the 0x10310 bytes of
   dead Omron IME space at 0x68A480.

   THE MATH. The HUD is authored in a 640 x 480 space, and every
   aspect-dependent constant it uses follows from keeping its height at 480
   and letting its width grow with the display ratio R = num / den. Each
   formula reproduces BOTH shipped values - 4:3 and 16:9 - and was checked
   against the image before it was trusted:

                     formula          4:3       16:9       21:9
     half-width      240 R            320       426.667    560
     2P ortho left   320 - 240 R      -         -106.667   -240
     overlay right   480 R            640       853.333    1120
     x squash        320 / half       1.0       0.75       0.5714 (1)
     glyph advance   (16/15) squash   1.0       0.8        0.6095 (2)
     banner x        320 - half       0         -107       -240
     banner width    2 half, up       640       854        1120
     anchor shift    see HUD MAX WIDTH -        96         229
     trophy aspect   (10/7) half/320  1.4286    1.9048     2.5 (3)

   Checked word for word against the image, with float arithmetic rounding
   toward zero as the EE's FPU does, the formulas land on the shipped 16:9
   words exactly except for the last bit of three float words, where Polyphony
   evidently derived the constant from an already-rounded intermediate: their
   853.33 matches 640 * 4/3 computed that way rather than 2 * 426.67, and
   their 0.8 is simply 0.8. That is the evidence these are the game's own
   formulas rather than a fit. Mode 1 puts the shipped words back rather than
   computing them, so the difference never reaches 16:9.

   (1) The x squash sites are lone luis, which carry only the top 16 bits of a
       float, so 4/7 lands on 0.5703 - about one pixel across the screen.
       64:27 gives 9/16, which a lone lui holds exactly.
   (2) A judgement call rather than geometry. Retail squashes text to 0.8 in
       16:9 where the geometry alone says 0.75, keeping it a little wider for
       legibility. This keeps that same 16/15 margin at every ratio.
   (3) The license and mission result trophy's own perspective. 10/7 is its
       4:3 aspect - the 640 x 448 frame's 320 half-width over a 224-line
       field - and the shipped 16:9 word is exactly 10/7 times 4/3 as the FPU
       rounds it, so that product, rather than 15R/14 directly, is what
       reproduces it to the bit. On its other path the same widening,
       half / 320, multiplies the display's own width / height: V_YSTRETCH.
*/

/*
    A mode is its name, its ratio as num : den, and how wide its side-anchored
    HUD may spread - see HUD MAX WIDTH below. 0 there means no cap. The name is
    only what the console prints.

                    name     num den  max width */
#define MODE_4_3    "4:3",    4,  3,     640
#define MODE_16_9   "16:9",  16,  9,     854
#define MODE_16_10  "16:10", 16,  10,    854
#define MODE_21_9   "21:9",  21,  9,     908 // 1120 is anchored, 908 centers more

/* Most panels sold as 21:9 are not 21:9. 2560x1080 and 5120x2160 are 64:27,
   and 3440x1440 is 43:18. 32:9 is the double-wide 5120x1440 class; its HUD
   has to be capped, and 1170 is the widest the shift allows. */
#define MODE_64_27  "64:27", 64, 27,   908 // 1120 is anchored, 908 centers more
#define MODE_43_18  "43:18", 43, 18,   908 // 1120 is anchored, 908 centers more
#define MODE_32_9   "32:9",  32,  9,   936

/*
    WHICH wide_mode NUMBER IS WHICH MODE: the order of this list. Script sets
    it exactly as it always has -

        main::game.option.wide_mode = 2;   // 16:10, with the list below

    0 and 1 are the game's own 4:3 and 16:9 and must stay first and second.
    Anything from 2 up is ours: add, remove or reorder freely.
*/
#define WIDE_MODES(X)                                                       \
    X(MODE_4_3)     /* wide_mode 0 - the game's own, never patched       */ \
    X(MODE_16_9)    /* wide_mode 1 - the game's own                      */ \
    X(MODE_16_10)   /* wide_mode 2                                       */ \
    X(MODE_21_9)    /* wide_mode 3                                       */ \
    X(MODE_32_9)    /* wide_mode 4                                       */
/*
    HUD MAX WIDTH - the fourth field of a mode. How far the side-anchored HUD
    may spread, as a width in HUD units. 0 means no cap: the side elements
    follow the screen edges however wide it is.

    HUD units are the HUD's own coordinate space, which is 480 tall, so a
    screen of ratio W:H is 480 * W / H units wide:

        640  4:3        853  16:9       1120  21:9
       1138  64:27     1147  43:18      1707  32:9

    Elements whose align byte has the widescreen bit - the map, the lap and
    position counters, the clocks and the like - are pushed outward from their
    4:3 positions by the anchor shift. Retail pushes them 96 in 16:9 although
    the edge moved 106.67, keeping them 10.67 units in from the edge, and that
    same inset is kept here at every width:

        shift = min(screen half-width, max width / 2) - 320 - 10.67

    So 0 puts them at the screen edges the way 16:9 does; 853 keeps the whole
    HUD framed exactly as it is in 16:9, centred on the wider screen; 640 keeps
    it framed as 4:3 frames it; anything in between is in between. The HUD's
    projection and the elements anchored to the centre are unaffected - this
    only limits how far the side elements stray.

    It must be a whole number. The shift it produces is carried by two addiu
    immediates and two lone luis, which agree exactly up to 255 - so a 32:9
    screen, whose uncapped shift would be 523, has to cap it: 1170 or less.
    A build that breaks this fails to compile rather than misplacing things.
*/

/* Pulling one field out of a mode. MODE_FIELD is variadic so a mode's
   comma-separated fields arrive as separate arguments after expansion. */
#define MODE_FIELD(f, ...)      f(__VA_ARGS__)
#define MODE_NAME(n, a, b, m)   n
#define MODE_NUM(n, a, b, m)    (a)
#define MODE_DEN(n, a, b, m)    (b)
#define MODE_MAXW(n, a, b, m)   (m)

/*
    The integer form of the half-width the side HUD spreads to, for the check
    below - the preprocessor cannot evaluate floats. 585 is 320 + 10.67 + 255,
    rounded down: the widest a half can be before the shift passes 255.
*/
#define HUD_HALF_I(NUM, DEN, MAXW)                                            \
    ((MAXW) > 0 && (MAXW) / 2 < (240 * (NUM)) / (DEN)                         \
        ? (MAXW) / 2 : (240 * (NUM)) / (DEN))

#define MODE_FITS(mode)                                                       \
    && HUD_HALF_I(MODE_FIELD(MODE_NUM, mode), MODE_FIELD(MODE_DEN, mode),     \
                  MODE_FIELD(MODE_MAXW, mode)) <= 585                         \
    && MODE_FIELD(MODE_MAXW, mode) >= 0

typedef char RaceAspect_SomeModesShiftPasses255_LowerItsMaxWidth
    [(1 WIDE_MODES(MODE_FITS)) ? 1 : -1];

/* 16:9 as listed is retail when its cap is no tighter than 16:9's own 853,
   which still gives retail's shift of 96. Then mode 1 restores the shipped
   words exactly; otherwise it is computed like any other mode. */
#define RETAIL_16_9                                                           \
    (MODE_FIELD(MODE_NUM, MODE_16_9) == 16 &&                                 \
     MODE_FIELD(MODE_DEN, MODE_16_9) == 9 &&                                  \
     (MODE_FIELD(MODE_MAXW, MODE_16_9) == 0 ||                                \
      MODE_FIELD(MODE_MAXW, MODE_16_9) >= 853))

/* ---- the numbers ------------------------------------------------------------ */

#define V_HALF      0
#define V_NEGHALF   1
#define V_LEFT2P    2
#define V_OVERLAY   3
#define V_XSCALE    4
#define V_GLYPH     5
#define V_SHIFT     6
#define V_NEGSHIFT  7
#define V_BANNERX   8
#define V_BANNERW   9
#define V_YSTRETCH  10
#define V_PREVIEWX  11
#define V_TROPHY    12
#define V_COUNT     13

/* The products are formed before the division, so a ratio with a small
   denominator comes out exact: 21:9 gives 5040 / 9 = 560, not
   240 * 2.3333333. Everything here is float, and this compiler folds single
   precision rounding toward zero, the way the EE's FPU computes. */
#define HALF_OF(NUM, DEN)  ((240.0f * (NUM)) / (DEN))

/* The half-width the side HUD actually spreads to: the screen's, or the cap's
   if that is narrower. */
#define HUD_HALF(NUM, DEN, MAXW)                                              \
    ((MAXW) > 0 && (MAXW) * 0.5f < HALF_OF(NUM, DEN)                          \
        ? (MAXW) * 0.5f : HALF_OF(NUM, DEN))

/* Retail's rule, rounded to a whole number so all four sites agree, and never
   negative: a cap narrower than 4:3 plus the inset just means no shift. */
#define SHIFT_RAW(NUM, DEN, MAXW)  (HUD_HALF(NUM, DEN, MAXW) - 320.0f - 32.0f / 3.0f)
#define SHIFT_OF(NUM, DEN, MAXW)                                              \
    (SHIFT_RAW(NUM, DEN, MAXW) > 0.0f                                         \
        ? (float)(int)(SHIFT_RAW(NUM, DEN, MAXW) + 0.5f) : 0.0f)

#define MODE_VALUES(NUM, DEN, MAXW)                                     \
    {                                                                   \
         HALF_OF(NUM, DEN),                            /* V_HALF     */ \
        -HALF_OF(NUM, DEN),                            /* V_NEGHALF  */ \
        320.0f - HALF_OF(NUM, DEN),                    /* V_LEFT2P   */ \
        2.0f * HALF_OF(NUM, DEN),                      /* V_OVERLAY  */ \
        320.0f / HALF_OF(NUM, DEN),                    /* V_XSCALE   */ \
        (16.0f * 320.0f) / (15.0f * HALF_OF(NUM, DEN)),/* V_GLYPH    */ \
         SHIFT_OF(NUM, DEN, MAXW),                     /* V_SHIFT    */ \
        -SHIFT_OF(NUM, DEN, MAXW),                     /* V_NEGSHIFT */ \
        320.0f - HALF_OF(NUM, DEN),                    /* V_BANNERX  */ \
        /* rounded UP by the P_INT rounding: 853.33 has to become 854,  */ \
        /* as retail has it, and a whole 1120 has to stay 1120         */ \
        2.0f * HALF_OF(NUM, DEN) + 0.4999f,            /* V_BANNERW  */ \
        /* the slide show stretches y rather than squashing x: the     */ \
        /* squash's inverse, 4/3 at 16:9                               */ \
        HALF_OF(NUM, DEN) / 320.0f,                    /* V_YSTRETCH */ \
        /* the portrait photo preview's centre: 174 + 154 * squash     */ \
        174.0f + (154.0f * 320.0f) / HALF_OF(NUM, DEN),/* V_PREVIEWX */ \
        /* the result trophy's perspective aspect: its 4:3 one, 10/7,  */ \
        /* times the widening - the product the shipped 40/21 is       */ \
        (320.0f / 224.0f) * (HALF_OF(NUM, DEN) / 320.0f), /* V_TROPHY */ \
    }

#define VALUES_OF(mode)                                                       \
    MODE_VALUES(MODE_FIELD(MODE_NUM, mode), MODE_FIELD(MODE_DEN, mode),       \
                MODE_FIELD(MODE_MAXW, mode)),
#define NAME_OF(mode)  MODE_FIELD(MODE_NAME, mode),
#define MAXW_OF(mode)  MODE_FIELD(MODE_MAXW, mode),

/* One row per wide_mode number. Row 0 is never used - 4:3 patches nothing -
   but keeping it lets the mode number index the table directly. g_maxw is
   each mode's own max width, for script to read back. */
static const float       g_values[][V_COUNT] = { WIDE_MODES(VALUES_OF) };
static const char* const g_names[]           = { WIDE_MODES(NAME_OF) };
static const short       g_maxw[]            = { WIDE_MODES(MAXW_OF) };

#define MODE_COUNT ((int)(sizeof(g_names) / sizeof(g_names[0])))

/* ---- where the numbers go ------------------------------------------------

   Every instruction on the wide arm that a mode changes, what it must hold,
   and which value it carries. The shipped words were read out of the image by
   a script and each one decoded back to the constant it builds - nothing in
   this table was typed from a disassembly listing.

   PART says how the immediate carries the value:

     P_HI   the top 16 bits of a float, in the lui of a lui/ori pair
     P_LO   the bottom 16 bits, in the ori. ori zero-extends, so unlike an
            addiu pair the top half needs no carry adjustment
     P_LUI  a float in a lone lui, rounded to the nearest value it can hold
     P_INT  an integer, in an addiu immediate, rounded to nearest

   Deliberately NOT here:
     * the race's 3D. Script already controls the field of view through
       main::game.option.physical_monitor_size's aspect, which reaches the
       race cameras through 0x26BD10 -> 0x26BDB0 -> 0x3154E0. (The 40/21 at
       0x2B09D0 and the x 4/3 at 0x2B0DF8 were once listed here as part of
       it. They are the result trophy's own view, and in the table below.)
     * the lens flare's pixel-aspect factor at 0x1D912C: its wide value is
       loaded but never read.
     * the split-screen zoom and rear-mirror rectangle at 0x1B77B4..0x1B77FC,
       which size things inside a half-screen rather than squash them.
     * the narrow arm. 4:3 is the game's own and nothing asked to change it.
     * the split-screen layout ids at 0x1C8D54 and 0x1C8D5C. The split
       VIEWPORT orientation is chosen by the same flag at 0x1C8DD4,
       independently of the id, so an id changed on its own would pair the
       side-by-side layout table with a stacked viewport or the reverse.
*/
#define P_HI  0
#define P_LO  1
#define P_LUI 2
#define P_INT 3

typedef struct AspectSite
{
    unsigned int  addr;
    unsigned int  shipped;
    unsigned char value;
    unsigned char part;
} AspectSite;

static const AspectSite g_sites[] =
{
    /* the HUD's 2D projection */
    { ADDR_Ortho_WideLeft_Lui,          0x3C01C3D5, V_NEGHALF,  P_HI  },
    { ADDR_Ortho_WideLeft_Ori,          0x34215555, V_NEGHALF,  P_LO  },
    { ADDR_Ortho_WideRight_Lui,         0x3C0143D5, V_HALF,     P_HI  },
    { ADDR_Ortho_WideRight_Ori,         0x34215555, V_HALF,     P_LO  },

    /* the anchor shift: the two integer helpers and the two float ones must
       all carry the same magnitude */
    { ADDR_Anchor_ShiftPlus,            0x24E20060, V_SHIFT,    P_INT },
    { ADDR_Anchor_ShiftMinus,           0x24E4FFA0, V_NEGSHIFT, P_INT },
    { ADDR_Anchor_ShiftFloat,           0x3C0142C0, V_SHIFT,    P_LUI },
    { ADDR_Anchor_EdgeFloat,            0x3C0142C0, V_SHIFT,    P_LUI },

    /* two-player split and the full-screen overlay projections */
    { ADDR_Ortho2P_WideLeft_Lui,        0x3C01C2D5, V_LEFT2P,   P_HI  },
    { ADDR_Ortho2P_WideLeft_Ori,        0x34215554, V_LEFT2P,   P_LO  },
    { ADDR_OrthoOverlay_WideRight_Lui,  0x3C014455, V_OVERLAY,  P_HI  },
    { ADDR_OrthoOverlay_WideRight_Ori,  0x34215554, V_OVERLAY,  P_LO  },

    /* the 2D x squash, all lone luis */
    { ADDR_XScale_A,                    0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_B,                    0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_C,                    0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_D,                    0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_E,                    0x3C013F40, V_XSCALE,   P_LUI },

    /* the same squash in the menus' 3D model views and the photo screens.
       Most of these gate on the options copy of the flag, *(0x664EDC)+0x694,
       which the native setter writes alongside the main one */
    { ADDR_XScale_F,                    0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_FrameGrab,            0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_PhotoFit,             0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_PhotoTile,            0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_PreviewTall,          0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_PreviewWide,          0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_ImageFit,             0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_ImageFitBack,         0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_SlideShowY,           0x3C013F40, V_XSCALE,   P_LUI },
    { ADDR_XScale_PhotoView,            0x3C013F40, V_XSCALE,   P_LUI },

    /* and the two that move with it: the portrait preview's centre, and the
       slide show's y stretch */
    { ADDR_PreviewTall_X_Lui,           0x3C014390, V_PREVIEWX, P_HI  },
    { ADDR_PreviewTall_X_Ori,           0x3421C000, V_PREVIEWX, P_LO  },
    { ADDR_SlideShow_Y_Lui,             0x3C013FAA, V_YSTRETCH, P_HI  },
    { ADDR_SlideShow_Y_Ori,             0x3421AAA7, V_YSTRETCH, P_LO  },

    /* text */
    { ADDR_GlyphAdvance_A_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_A_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    { ADDR_GlyphAdvance_B_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_B_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    /* the same squash in four more text paths, the menu text among them -
       found by sweeping the text mirror 0x6651B8 */
    { ADDR_GlyphAdvance_C_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_C_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    { ADDR_GlyphAdvance_D_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_D_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    { ADDR_GlyphAdvance_E_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_E_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    { ADDR_GlyphAdvance_F_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_F_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    /* and three more that sweep missed */
    { ADDR_GlyphAdvance_G_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_G_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    { ADDR_GlyphAdvance_H_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_H_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },
    { ADDR_GlyphAdvance_I_Lui,          0x3C013F4C, V_GLYPH,    P_HI  },
    { ADDR_GlyphAdvance_I_Ori,          0x3421CCCC, V_GLYPH,    P_LO  },

    /* the two-player banner */
    { ADDR_Banner_X,                    0x2405FF95, V_BANNERX,  P_INT },
    { ADDR_Banner_W,                    0x24050356, V_BANNERW,  P_INT },
    { ADDR_Banner_W2,                   0x24050356, V_BANNERW,  P_INT },

    /* the license and mission result trophy that spins in the middle of the
       screen, in its own perspective, drawn by 0x2B0730 for ResultLicense and
       ResultChallenge alone. The field path (0x2B08E8) loads the wide aspect,
       which the 4:3 pair at 0x2B09E4 overwrites when the flag is clear; the
       other display modes' path (0x2B0CD8) multiplies width / height by the
       widening, and skips it in 4:3 */
    { ADDR_Aspect3D_Wide_Lui,           0x3C013FF3, V_TROPHY,   P_HI  },
    { ADDR_Aspect3D_Wide_Ori,           0x3421CF3B, V_TROPHY,   P_LO  },
    { ADDR_Aspect3D_WideMul_Lui,        0x3C013FAA, V_YSTRETCH, P_HI  },
    { ADDR_Aspect3D_WideMul_Ori,        0x3421AAAA, V_YSTRETCH, P_LO  },
};

#define ASPECT_SITES ((int)(sizeof(g_sites) / sizeof(g_sites[0])))

/*
    The 16-bit immediate a site should hold. Only the immediate is ever
    replaced; the opcode and both register fields stay exactly as they shipped,
    which is what lets one table cover luis, oris and addius on different
    registers.
*/
static unsigned int Immediate(float value, int part)
{
    union { float f; unsigned int u; } pun;
    unsigned int bits;

    pun.f = value;
    bits  = pun.u;

    switch (part)
    {
        case P_HI:  return bits >> 16;
        case P_LO:  return bits & 0xFFFFu;

        /* Round to nearest rather than truncate. Float bits are sign and
           magnitude, so adding half an lui step to the magnitude rounds
           negative values correctly too. */
        case P_LUI: return ((bits + 0x8000u) >> 16) & 0xFFFFu;

        /* Float only - anything that promotes to double would pull in the
           soft-float library, since the EE's FPU is single precision. */
        default:    return (unsigned int)(int)(value < 0.0f ? value - 0.5f
                                                            : value + 0.5f) & 0xFFFFu;
    }
}

/* ---- switching modes -------------------------------------------------------- */

/*
    The wide mode the wide arm's constants currently hold. They ship as 16:9,
    so this starts at 1. Explicitly initialised so it lands in .data - the
    build passes -fno-zero-initialized-in-bss and .bss is not in the injected
    image.
*/
static int g_wide = 1;

/*
    Script's max width, or -1 for each mode's own (see RaceAspectSetMaxWidth
    below). g_live says the sites checked out at install, so script cannot
    rewrite words this build never verified.
*/
static int g_maxWidth = -1;
static int g_live     = 0;

/*
    The anchor shift for a mode, as the site table takes it. With no script max
    width it is the mode's own, worked out at compile time with everything
    else. With one it is the same rule worked out here, from the half-width the
    table already carries, and held at 255 - the most all four sites can carry
    and still agree - where the compile-time check refuses the build instead.
*/
static float ShiftFor(int mode)
{
    float half = g_values[mode][V_HALF];
    float raw;
    int   shift;

    if (g_maxWidth < 0)
        return g_values[mode][V_SHIFT];

    if (g_maxWidth > 0 && (float)g_maxWidth * 0.5f < half)
        half = (float)g_maxWidth * 0.5f;

    raw   = half - 320.0f - 32.0f / 3.0f;
    shift = raw > 0.0f ? (int)(raw + 0.5f) : 0;
    if (shift > 255)
        shift = 255;

    return (float)shift;
}

/* The game's own FlushCache syscall stub: 0 writes the data cache back to
   memory, 2 invalidates the instruction cache. */
#define GameFlushCache(mode) ((void (*)(int))ADDR_FlushCache)(mode)

/*
    Rewrite the wide arm for a mode, 1 or above.

    This rewrites instructions while the game is running, which is only safe
    because both caches are dealt with afterwards: the new words go out through
    the data cache, so it is written back first, and the instruction cache may
    still hold the old ones, so it is invalidated. Without the second step the
    CPU can keep executing the stale copy of a patched line indefinitely.

    Nothing patched here is on the call stack when this runs - it is reached
    from the wide_mode attribute, in the script VM, not from any of the
    drawing code it rewrites.
*/
static void ApplyWide(int mode)
{
    int i;

    for (i = 0; i < ASPECT_SITES; i++)
    {
        const AspectSite* s = &g_sites[i];
        unsigned int      imm;
        unsigned int      w;

        if (mode == 1 && RETAIL_16_9 && g_maxWidth < 0)
            imm = s->shipped & 0xFFFFu;
        else if (s->value == V_SHIFT)
            imm = Immediate(ShiftFor(mode), s->part);
        else if (s->value == V_NEGSHIFT)
            imm = Immediate(-ShiftFor(mode), s->part);
        else
            imm = Immediate(g_values[mode][s->value], s->part);

        w = *(volatile unsigned int*)s->addr;
        *(volatile unsigned int*)s->addr = (w & 0xFFFF0000u) | imm;
    }

    GameFlushCache(0);
    GameFlushCache(2);

    g_wide = mode;

    Sio_Puts("aspect: widescreen is ");
    Sio_Puts(g_names[mode]);
    Sio_Puts("\n");
}

/*
    main::game.option.wide_mode = value

    The attribute callback at 0x157AB0 used to squash the value to a bool in
    the delay slot at 0x157B04 before calling the native setter. That slot now
    passes it through untouched and the call lands here, so the plugin sees the
    number script wrote.

    The game's setter still gets the bool: it stores it as the word at
    options+0x14 and re-copies the TV geometry block, and its change guard
    (0x315454) compares what it is given with a 0 or 1, so any other number
    would re-copy on every set. Then the number itself goes into that word.
    Every native reader of it only tests it against zero, so the game sees
    plain widescreen for any mode from 1 up; the save copies the options block
    verbatim, word and all, so the mode survives a save and a reload
    (HOOK_WideModeApply picks it back up); and reading wide_mode back is the
    game's own getter returning the word.

    Out of range means 16:9, which is what the game itself makes of any
    non-zero value.
*/
void HOOK_WideModeSet(void* options, int value)
{
    int mode = value;

    if (mode < 0 || mode >= MODE_COUNT)
    {
        Sio_Puts("aspect: no such wide_mode - using 16:9\n");
        mode = 1;
    }

    /* 4:3 reads the narrow arm, so the wide constants can stay as they are
       and a later switch back to the same wide mode costs nothing. */
    if (mode != 0 && mode != g_wide)
        ApplyWide(mode);

    ((void (*)(void*, int))ADDR_WideMode_NativeSet)(options, mode != 0);
    *(volatile int*)((char*)options + OPTIONS_WIDE_MODE) = mode;
}

/*
    The options being put into effect.

    Apply (0x312578) runs whenever the game makes an options object live - at
    boot, after a saved game is loaded, from script's apply, and after script
    copies options in - before anything is drawn with them, and it never reads
    wide_mode itself. Its call to ratioIsWide at 0x3125B0, which sets the HUD
    flag from the TV geometry block at options+0x24, comes here first: the
    wide_mode word is right beside that block, so a mode loaded from a save
    gets its constants back. A number out of range - a save from a build with
    more modes - is put back to 16:9, which the geometry block already
    describes for any non-zero wide_mode.
*/
int HOOK_WideModeApply(void* geometry)
{
    volatile int* word = (volatile int*)((char*)geometry - OPTIONS_TV_GEOMETRY +
                                         OPTIONS_WIDE_MODE);
    int mode = *word;

    if (mode < 0 || mode >= MODE_COUNT)
    {
        Sio_Puts("aspect: saved wide_mode out of range - using 16:9\n");
        mode = 1;
        *word = 1;
    }
    if (mode != 0 && mode != g_wide)
        ApplyWide(mode);

    return ((int (*)(void*))ADDR_ratioIsWide)(geometry);
}

/*
    MOption::RaceAspectSetMaxWidth(width) -> 1 or 0
    MOption::RaceAspectGetMaxWidth()      -> the max width in force

    A mode's fourth field, from script instead of the build: how wide the
    side-anchored HUD may spread, in HUD units (see HUD MAX WIDTH above). It
    holds for every wide mode until it is set again, and -1 gives each mode its
    own back. 0 means no cap, as in the table. A width whose shift would pass
    255 gets 255, the widest the four sites agree on - about 1170.

    It takes effect at once, through the same rewrite a mode switch uses, and
    needs no race: the anchor shift is read while the HUD draws. It is not
    saved - a script that wants it after a reboot sets it again.
*/
void f_RaceAspectSetMaxWidth(HObject* return_value, int argc, hObject** argv)
{
    int width;

    if (!g_live || argc < 1 || !RaceHud_IsNumber(argv + 0))
    {
        RaceHud_ReturnInt(return_value, 0);
        return;
    }

    width = RaceHud_ToInt(argv + 0);
    if (width < -1)
    {
        RaceHud_ReturnInt(return_value, 0);
        return;
    }

    g_maxWidth = width;
    ApplyWide(g_wide);
    RaceHud_ReturnInt(return_value, 1);
}

void f_RaceAspectGetMaxWidth(HObject* return_value, int argc, hObject** argv)
{
    (void)argc;
    (void)argv;

    RaceHud_ReturnInt(return_value, g_maxWidth >= 0 ? g_maxWidth : g_maxw[g_wide]);
}

/*
    The menu pointer.

    The cursor update rebuilds the pointer's matrix every frame - identity,
    rotate by the aim angle, translate by -w/2 - in the 640-wide menu space,
    and the display stretches that space afterwards. So on a wide screen a
    tilted pointer comes out sheared. Squashing the image itself cannot fix
    that: the squash rotates with it.

    The rotate call comes here instead, and a squash of (X, 1) goes in first.
    The helpers pre-multiply, so the one applied first to the fresh identity is
    the last thing that happens to the pointer: after the tilt, along the
    screen's x axis - the one axis the display stretches. The pointer keeps its
    .mproject width; this replaces the script-side width squash.

    IT IS ASSEMBLY BECAUSE THE ABIS DISAGREE (see ADHOC_MakeFloat in Adhoc.h):
    the game passes a float after a pointer in $f12, this toolchain in $f13.
    The call site hands over a0 = matrix and $f12 = degrees, and both game
    helpers read their floats from $f12 up. A C function would read the angle
    from the wrong register and hand both helpers the wrong ones. So the entry
    and both calls are written out by hand, and only the choice of X is C -
    a float RETURN is $f0 on both sides.
*/
#define RA_STR2(x) #x
#define RA_STR(x)  RA_STR2(x)

/* X for the pointer: the mode's squash, or 1.0 when the game is in 4:3. */
static float CursorSquash(void) __attribute__((used));
static float CursorSquash(void)
{
    if (*(volatile int*)ADDR_HudWideFlag)
        return g_values[g_wide][V_XSCALE];
    return 1.0f;
}

__asm__(
    ".pushsection .text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".align 2\n"
    ".globl HOOK_CursorRotate\n"
    ".type HOOK_CursorRotate, @function\n"
    "HOOK_CursorRotate:\n"
    "    addiu $sp, $sp, -32\n"
    "    sd    $ra, 0($sp)\n"
    "    sd    $a0, 8($sp)\n"            /* the matrix                         */
    "    jal   CursorSquash\n"           /* $f0 = X                            */
    "    swc1  $f12, 16($sp)\n"          /* delay slot: keep the angle         */
    "    mov.s $f12, $f0\n"              /* scale(matrix, X, 1.0)              */
    "    lui   $at, 0x3F80\n"
    "    mtc1  $at, $f13\n"
    "    li    $t9, " RA_STR(ADDR_Matrix_Scale) "\n"
    "    jalr  $t9\n"
    "    ld    $a0, 8($sp)\n"
    "    ld    $a0, 8($sp)\n"            /* rotate(matrix, angle), as a tail   */
    "    lwc1  $f12, 16($sp)\n"          /* call, returning to the game        */
    "    ld    $ra, 0($sp)\n"
    "    li    $t9, " RA_STR(ADDR_Matrix_Rotate) "\n"
    "    jr    $t9\n"
    "    addiu $sp, $sp, 32\n"
    ".size HOOK_CursorRotate, .-HOOK_CursorRotate\n"
    ".set pop\n"
    ".popsection\n"
);

/* ---- installing ------------------------------------------------------------- */

/* All or nothing, as MakerList.c does it: every site and every word the hooks
   touch or call must be exactly what this build shipped with. */
static int Verified(void)
{
    int i;

    for (i = 0; i < ASPECT_SITES; i++)
        if (!RaceHud_WordIs(g_sites[i].addr, g_sites[i].shipped))
            return 0;

    return RaceHud_WordIs(ADDR_WideMode_SetCall,  INSN_JAL_WideModeNativeSet) &&
           RaceHud_WordIs(ADDR_WideMode_SetValue, INSN_SLTU_A1_ZERO_V0)       &&
           RaceHud_WordIs(ADDR_ratioIsWide_JAL_apply, INSN_JAL_ratioIsWide)   &&
           RaceHud_WordIs(ADDR_FlushCache,        INSN_FLUSHCACHE_FIRST)      &&
           RaceHud_WordIs(ADDR_CursorMatrix_RotateCall, INSN_JAL_MatrixRotate) &&
           RaceHud_WordIs(ADDR_Matrix_Scale,      INSN_MATRIX_SCALE_FIRST)    &&
           RaceHud_WordIs(ADDR_Matrix_Rotate,     INSN_MATRIX_ROTATE_FIRST);
}

void RaceAspect_InstallHooks(void)
{
    if (!Verified())
    {
        Sio_Puts("aspect: not the executable these addresses describe;"
                 " not patching\n");
        return;
    }

    /* Nothing on the wide arm changes here: it ships as 16:9, which is mode 1,
       and stays that way until script or a saved game asks for something
       else. These words are in the attribute callback and in apply, neither
       of which has run yet, so they need no cache maintenance of their own. */
    MAKE_JAL(ADDR_WideMode_SetCall, &HOOK_WideModeSet);
    PATCH_INT(ADDR_WideMode_SetValue, INSN_DADDU_A1_V0);
    MAKE_JAL(ADDR_ratioIsWide_JAL_apply, &HOOK_WideModeApply);

    /* The menu pointer's rotate call - no menu has drawn yet either. */
    MAKE_JAL(ADDR_CursorMatrix_RotateCall, &HOOK_CursorRotate);

    g_live = 1;
}
