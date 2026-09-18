#pragma once

#include "GameFunctions/Adhoc.h"
#include "GameStructs/Adhoc.h"
#include "Adhoc/Decl.h"

/* ===========================================================================
   RaceHud - HUD colours from adhoc, and the plugin's MOption functions.

   WHAT THIS GIVES A SCRIPT

     main::menu::MOption::RaceHudPing()                         -> 42
     main::menu::MOption::RaceHudSetColor(slot, argb)           -> 1 or 0
     main::menu::MOption::RaceHudSetColor("needle", vtx, argb)  -> 1 or 0
     main::menu::MOption::RaceHudGetColor(slot [, vtx])         -> argb or nil
     main::menu::MOption::RaceAspectSetMaxWidth(width)          -> 1 or 0
     main::menu::MOption::RaceAspectGetMaxWidth()               -> width
     main::menu::MOption::RaceHudSetGpb(name)                   -> 1 or 0
     main::menu::MOption::RaceHudGetGpb()                       -> name

   A colour is set from a MENU - adhoc runs only outside races - and every
   race after it uses it, because it goes where the HUD reads it rather than
   into a live HUD. The slots, and where each one lives, are HUD_COLOUR_SITES
   in the target header; RaceHud.c says how each kind of site is written. The
   max width is RaceAspect's (RaceAspect.h) and the texture file RaceGpb's
   (RaceGpb.h); they register here because this is where MOption's
   registration is displaced.

   They are FUNCTIONS, not methods: a HUD element has no adhoc receiver to hang
   a method on, and MStorage.c already established that a function can be
   registered through a displaced defineMethod call. Note the capital M -
   MOption is the script-visible module name, mOption is the C++ type, the same
   split MCarGarage.h describes.

   MOption was chosen because it already owns every HUD-adjacent member the
   game ships: the ten race_display_* attributes and apply, applyRaceDisplayMode
   and applyMenuDisplayMode all live in one InitClass at 0x161F38.

   THE LAYOUT FUNCTIONS - RaceHudGet and RaceHudSet, which moved elements -
   are shelved: see RACEHUD_LAYOUT below. The notes on them follow.

   ELEMENTS ARE NAMED, NOT NUMBERED. An earlier draft of this let a script pass
   a raw byte offset into RaceDisplay. That is a scriptable arbitrary-jump
   primitive on a machine with no memory protection: the offset feeds a load of
   the element vptr, and the next step is a jalr through a word the script
   effectively chose. The names below are the whole addressable surface, and
   every offset in the table is a compile-time constant checked against the
   member map. If something is missing from the table, add it to the table.

   WHY NOT AN INDEX INTO THE GAME OWN TABLES: the layout tables are not a
   registry. The same element appears at index 0, 1 and 11 in three different
   tables, and about thirty of the members appear in no table at all. Class name
   is no better - there are five RaceTimeDisplay, nine RaceMessageDisplay and
   four RaceTexturePanel. The member offset is the only unique key, so the names
   here are a plugin-side alias for it.
   =========================================================================== */

/*
    RACEHUD_LAYOUT - the layout functions, RaceHudGet and RaceHudSet, which
    moved HUD elements and could keep them in place across relayouts. Off in
    both builds, and kept for later: adhoc runs only outside races, where there
    is no HUD to move, so they could only reach a race display no script is
    running beside - and they took a large share of the space the plugin has.
    1 compiles them again, as they were; RaceHud.o then comes to about 15 KB,
    which no longer fits .cave2 beside RaceAspect, so move it to .cave2b in
    the linkfile too.
*/
#ifndef RACEHUD_LAYOUT
#define RACEHUD_LAYOUT 0
#endif

void RaceHud_InstallHooks(void);

/* The displaced MOption registration. See ADDR_MOption_save_register. */
void HOOK_ExtendMOption(void* tempHValue, hModule* module, char* name,
                        Adhoc_method_cb method);

/* Both callers of RaceDisplay::relayout land here, the one that builds a new
   display among them: FIELD colours a script has set go into the display
   there. The shelved layout code also re-applies its sticky overrides here. */
void HOOK_RaceDisplay_relayout(void* display, int hint);

/*
    Shared with RaceAspect.c rather than duplicated into it.

    MStorage.c and MCarGarage.c each carry their own copies of helpers like
    these and both explain why: lifting three of eight into a common header
    would leave a worse split than either whole one. That reasoning does not
    apply here. RaceHud and RaceAspect are two halves of one feature, they
    register through one hook, and the plugin has 0x10310 bytes of dead Omron
    IME space to live in - a second copy of four functions is real bytes out of
    a budget that is already tight at -O0.
*/
int   RaceHud_IsNumber(hObject** slot);
int   RaceHud_ToInt(hObject** slot);
int   RaceHud_IsString(hObject** slot);
char* RaceHud_CStr(hObject** slot);
void  RaceHud_ReturnInt(HObject* return_value, int value);
int   RaceHud_WordIs(unsigned int addr, unsigned int expect);

void f_RaceHudPing(HObject* return_value, int argc, hObject** argv);
void f_RaceHudGetColor(HObject* return_value, int argc, hObject** argv);
void f_RaceHudSetColor(HObject* return_value, int argc, hObject** argv);
#if RACEHUD_LAYOUT
void f_RaceHudGet(HObject* return_value, int argc, hObject** argv);
void f_RaceHudSet(HObject* return_value, int argc, hObject** argv);
#endif

/* ---------------------------------------------------------------------------
   PROPERTY IDS for RaceHudGet / RaceHudSet (shelved, see RACEHUD_LAYOUT).

   0..7 apply to a named element. 100 and up apply only to the pseudo-element
   "screen", which is the RaceDisplay itself rather than one of its children.
   --------------------------------------------------------------------------- */
#define RHP_X       0   /* s16 at +0x04, written through vtable slot 7      */
#define RHP_Y       1   /* s16 at +0x06, written through vtable slot 7      */
#define RHP_W       2   /* s16 at +0x08, written through vtable slot 8      */
#define RHP_H       3   /* s16 at +0x0A, written through vtable slot 8      */
#define RHP_ALIGNH  4   /* u8  at +0x01, written through vtable slot 9      */
#define RHP_ALIGNV  5   /* u8  at +0x02, written through vtable slot 9      */
#define RHP_STICKY  6   /* plugin-side only: survive the next relayout      */

#define RHP_LAYOUT_MODE 100  /* RaceDisplay+0x1C, read only                 */
#define RHP_SUB_MODE    101  /* RaceDisplay+0x20, read only                 */
#define RHP_METER_HINT  102  /* RaceDisplay+0x24, read only                 */
#define RHP_MASK        103  /* the element mask; set goes through 0x1C8308 */
#define RHP_HUD_TYPE    104  /* RaceDisplay+0x33 - GATED, see RaceHud.c     */

/*
    POSITION AND SIZE GO THROUGH THE VTABLE, NEVER THROUGH THE FIELD, and that
    is not stylistic. Four classes override the setters and recompute child
    geometry inside them - RaceOnboardPanel (0x1CECC8), RaceSimplePanel
    (0x1CD778) and RacePriusPanel (0x1CFC08) override setPos; RaceMessageDisplay
    and RaceReplayModeDisplay (0x1CA930) override setSize and also zero +0x28.
    Writing the s16 field directly would move the parent and leave its children
    behind.

    NOT EXPOSED, and all three for the same reason - the plugin has to fit in
    the 0x10310 bytes of dead Omron IME space at 0x68A480, and at the repo-wide
    -O0 it does not fit with them:

      element alpha, the float at +0x0C. It has no vtable setter, so it would
        be a direct store - and being the only float in the API it drags in
        ADHOC_MakeFloat and the TOFLOAT conversion slot, which is the largest
        single thing in here for the least of the ask. It is also neither
        position nor colour.
      the second mask word at +0x60 (0x1C8308 with which = 2).
      the elements that cannot be displayed in this build: all four MTR members
        and all three RaceTT*Meter members. MAP 2 explains why they are dead;
        an API that moves something invisible is worse than not having one.

    All of it is a few lines to restore. See the note at the end of this file.
*/

/* ===========================================================================
   MAP 1 - HUD LAYOUT BY GAME STATE

   The walker at 0x1C8970 takes a table of 36-byte records and applies each to
   one element. A record is:

     +00 u32  member offset PLUS ONE   (0 terminates the table; the +1 is what
                                        lets offset 0 be a real member and 0
                                        still mean "end")
     +04 u32  child table              always 0 in all 85 shipped records
     +08 s32  x       +0x0C s32 y      -> vtable slot 7
     +10 s32  w       +0x14 s32 h      -> vtable slot 8
     +18 u32  alignH  +0x1C u32 alignV -> vtable slot 9  (read with lbu)
     +20 u32  reserved                 never read

   Nine tables exist. Which one runs is decided in 0x1C8570 from two numbers -
   RaceDisplay+0x1C (layout mode) and RaceDisplay+0x20 (sub-mode) - through two
   jump tables, 0x6ED5B0 for the generic path and 0x6ED590 for mode 19:

     layout mode 8   -> table F  0x6ECDE8  13 recs  split screen, stacked
                                                    640x224 halves (4:3)
     layout mode 9   -> table G  0x6ECFE0  14 recs  split screen, side by side
                                                    320x448 halves (16:9)
     layout mode 19  -> table I  0x6ED368  13 recs  when *(*(this+4)+0x60) < 2
                     -> table H  0x6ED200   9 recs  otherwise; the only table
                                                    that carries RaceSignBoard
     anything else, then by sub-mode:
       sub 0, 2, 3   -> table C  0x6EC988  14 recs  the main HUD
       sub 1         -> table D  0x6ECBA8  11 recs
       sub 4         -> table E  0x6ECD58   3 recs  minimal
     and independently, a clock pair chosen by 0x1C8120:
       0x1C8120 == 0 -> table A  0x6EC818   4 recs
       0x1C8120 != 0 -> table B  0x6EC8D0   4 recs   (layout mode 2, or the
                                                      byte at display+0x39)

   HONEST LIMIT, because it is the one thing this map cannot give you: there is
   no arcade / GT-mode / time-trial enum inside the HUD. Layout mode is a small
   integer set by whatever built the display, and only three values can be
   pinned from inside RaceDisplay - 0 (the ctor default), 2 (tested at 0x1C8124,
   selects the total-time texture-panel pair) and 19 (RaceChallenge). 8 and 9
   are the two split-screen modes. The rest are named by their caller, not here.

   A SECOND PASS RUNS AFTER THE TABLE. 0x1C77C0 positions the cluster panel and
   its sub-elements from 16-byte records built on the stack at 0x1C7804..0x1C7D04,
   relative to the SELECTED PANEL at display+0x14 rather than to the display. So
   cluster sub-elements are not nameable by a display-relative offset and are
   deliberately absent from the element table below.
   =========================================================================== */

/* ===========================================================================
   MAP 2 - THE FOUR HUD TYPES

   Two independent selectors, and only two of the four families are reachable
   in this executable.

   SELECTOR A - TEXTURE SET, the byte at RaceDisplay+0x33.
     0x1C1290 reads it: zero picks "gt4" (0x6EC440), non-zero picks "tt"
     (0x6EC438), and 0x1C12F8 builds "/race_display/%s/%s/display.gpb" from it
     plus the RaceDisplayVersion config key. The .gpb holds TEXTURES ONLY.
     Nothing in SCUS-97436 ever writes +0x33 and the ctor zeroes the whole word
     at 0x1C0128, so the entire TT branch is dead code in retail: the tt jump
     table 0x6EC790, all three RaceTT*Meter classes, the RaceSignBoard draw
     gated at 0x1C5C94, and the "tt default meter kind = 1" at 0x1C0BC4.

   SELECTOR B - CLUSTER, 0x1C76E0 getMeterKind(display, hint), consumed by
   0x1C77C0. The kind then indexes one of two jump tables:

     kind  chosen when                          gt4 (0x6EC7B0)      tt (0x6EC790)
     0     hint 0, the cockpit camera           OnboardPanel  +280  TTCenterMeter  +13248
     1     hint 1, or steering on, or
           carParam+0x32 == 2                   SimplePanel   +3380 TTCombiMeter   +11916
     2     hint 2, camera view 6                SimplePanel   +3380 TTSimpleMeter  +16764
     3     sub-mode == 1                        SimplePanel   +3380 (none)
     4     layout mode 8 and tt                 (none)              TTSimpleMeter
     5     layout mode 9                        SimplePanel   +3380 TTSimpleMeter
     6     PRIUS, see below                     PriusPanel    +20424 -
     7     reachable only via hint              SimplePanel, empty table

   The hint at display+0x24 comes from the camera view through slot 12
   (0x1C2648, jump table 0x6EC580: view 0 -> 0, views 1..5 -> 1, view 6 -> 2).
   The chosen panel is stored at display+0x14.

   PRIUS is selected by the powertrain, and the GEAR-record hypothesis is
   confirmed with a refinement. 0x22C718 is
     lw $a0,0x10($a0) / addiu $a0,$a0,576 / j 0x22C708
   and 0x22C708 is lbu $v0,0x12($a0) / xori 3 / sltiu 1 - so the test is
   *(u8*)(carParam + 576 + 0x12) == 3. carParam+576 IS the GEAR record: the
   transmission code at 0x212C3C reads a float gear-ratio array at record+0x40
   indexed by the current gear. So it is not "which GEAR part is fitted" but a
   POWERTRAIN TYPE byte inside that record - 1 and 2 geared, 3 hybrid, 4 a
   further special case that skips the gear clamp at 0x213C24.
   The Prius segment bar is NOT vector geometry: slot 11 (0x1D04B0) writes a
   PSMCT32 palette through the pointer at obj+0x1C, with the index swizzle at
   0x1D044C, using #FAE300 lit and #202020 unlit. A palette write has to be
   repeated every frame, so it is not in the colour table below.

   MTR is Motor Triathlon Race, the Toyota MTRC concept. CONSTRUCTED BUT
   UNREACHABLE. RaceMTRMeterPanel (+21024), RaceMTRGravityMeter (+21504),
   RaceMTRSpeedMeterPanel (+21552) and RaceMTRMultiFunctionDisplay (+21608)
   appear only in the RaceDisplay ctor at 0x1C00A0 and in the dtor. They are in
   no layout table, not in the hide-all sweep at 0x1C74A0 (which does include
   Prius), in neither cluster jump table, and their four accessor thunks at
   0x602FA0, 0x602FB8, 0x602FC0 and 0x602FC8 have zero callers. The face table
   0x6618B0 - mtr_meter_fl, _fr, _rl, _rr, mtr_accel, brake, asm, tcs, grip -
   has no reference of any kind in the image.
   Reviving MTR is a real feature, not a flag: it needs a new cluster kind
   routed through the 8-entry table at 0x6EC7B0 (kind 7, currently SimplePanel
   with an all-zero element table, is the natural victim) plus a predicate in
   0x1C76E0. Both are instruction patches into hot code, and neither is here.

   The `live` column in g_hudTypes is what stops a script author filing a bug
   when a write to an MTR element succeeds and nothing appears on screen.
   =========================================================================== */

typedef struct HudType
{
    const char*    name;
    unsigned short panelOffset;
    unsigned char  textureSet;  /* 0 gt4, 1 tt                             */
    unsigned char  meterKind;   /* 255 = no selector reaches it            */
    unsigned char  live;        /* 0 = present in memory, never displayed  */
} HudType;

/* ===========================================================================
   MAP 3 - THE ELEMENT MASK

   Eleven bits, each an adhoc option key, each gating a group of elements. The
   default word is 1018 (0x3FA), written at 0x312360. Set through 0x1C8308,
   which stores to +0x5C or +0x60 depending on its `which` argument and then
   re-runs vtable slot 36 to recompute the effective mask at +0x58.

     bit 0  0x001  replay_display_enable        picks display kind 0 or 1
     bit 1  0x002  race_display_map             RaceMiniMap
     bit 2  0x004  race_display_steering        RaceSteeringDisplay
     bit 3  0x008  race_display_meter           the whole cluster
     bit 4  0x010  race_display_time            time4, laptimes
     bit 5  0x020  race_display_lap             lap, panel1
     bit 6  0x040  race_display_position        rank
     bit 7  0x080  race_display_car_name        message2, message7
     bit 8  0x100  race_display_suggested_gear  cluster panel vtbl+184
     bit 9  0x200  race_display_g_meter         cluster panel vtbl+208
     bit 10 0x400  race_display_config          cluster panel byte +0x1D
                                                (INVERTED - set means off)

   Visibility is per-GROUP only. There is no per-element visibility field: for a
   textured element the gate is the texture handle at +0x10, and zeroing that
   loses the handle rather than hiding the element, so this plugin does not
   offer it.
   =========================================================================== */
