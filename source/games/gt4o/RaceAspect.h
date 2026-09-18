#pragma once

#include "RaceHud.h"    /* the adhoc types */

/* ===========================================================================
   RaceAspect - extra aspect-ratio modes, chosen from script and kept in the
   saved game.

   HOW SCRIPT USES IT

       main::game.option.wide_mode = 0;   // 4:3   - the game's own
       main::game.option.wide_mode = 1;   // 16:9  - the game's own
       main::game.option.wide_mode = 3;   // 21:9  - with the shipped list

   The numbers are the order of WIDE_MODES in RaceAspect.c, and each mode is
   one #define there: its name, its ratio and how far the side HUD may spread.
   Reading wide_mode back returns the number that was set, and a saved game
   keeps it.

   How far the side HUD spreads can also be set from script, for every wide
   mode at once, and read back:

       main::menu::MOption::RaceAspectSetMaxWidth(854);  // frame it as 16:9
       main::menu::MOption::RaceAspectSetMaxWidth(0);    // out to the edges
       main::menu::MOption::RaceAspectSetMaxWidth(-1);   // each mode's own
       var w = main::menu::MOption::RaceAspectGetMaxWidth();

   It takes effect at once, in a menu or a race, and is not saved.

   WHAT THE GAME DOES WITH wide_mode

   Nothing but a bool. The attribute callback at 0x157AB0 squashes whatever
   script gives it with `sltu $a1, $zero, $v0` in the delay slot at 0x157B04,
   then calls the native setter 0x313260, which stores that bool as the word
   at options+0x14 and re-copies the TV geometry block. Every aspect-dependent
   constant sits on one side or the other of a test of the flag that follows
   from it, at 0x6655A0: a narrow arm and a wide arm. So natively 2 was simply
   16:9 again.

   The word itself is roomier than that. The save copies the options block
   verbatim (0x3135F8 to save, 0x313758 to load), that word included, and
   every native reader of it only tests it against zero.

   WHAT THIS DOES

   The delay slot passes the raw value through and the call goes to
   HOOK_WideModeSet, which rewrites the wide arm's instruction immediates for
   that mode, calls the game's own setter with the bool it has always had, and
   then stores the number itself in the word. So the game sees plain
   widescreen for any mode from 1 up, script reads the number back through the
   game's own getter, and a save keeps it. The retail executable reads such a
   save as 16:9.

   When the game puts options into effect - at boot, after loading a saved
   game, and when script applies options - its call to ratioIsWide at 0x3125B0
   goes through HOOK_WideModeApply first, which reads the word and brings that
   mode's constants back. An options file loaded from script takes effect when
   script applies it, which is also when the game's own 4:3/16:9 does.

   Rewriting instructions while the game runs is only safe with the caches
   dealt with. The new words go out through the data cache, which is written
   back with FlushCache(0), and any stale copies in the instruction cache are
   dropped with FlushCache(2) - the game's own syscall stub at 0x5DA2E0, which
   it calls from 58 places itself.

   0 and 1 are the game's own. 0 is never patched: the game takes the narrow
   arm and the wide constants are not read. 1 puts the exact shipped words
   back. So a script that only ever uses 0 and 1 runs retail code - except for
   the menu pointer, which is squashed in 16:9 too.

   WHAT IT COVERS

   The HUD: its projection, the widescreen anchor shift, the two-player and
   overlay projections, its 2D x squashes and the two-player banner. The text
   advance, in all nine text paths. The menus' 3D car models - ModelStudio,
   mCarModelPS2 and the mModel render contexts, all through 0x353600. The
   photo screens: the frame grab, photo fit, pause preview, slide show and
   photo view. The license and mission result trophy, which has its own
   perspective. The menu pointer, squashed after its tilt so it keeps its
   shape. The race's 3D field of view is deliberately left alone - script
   already controls it through main::game.option.physical_monitor_size's
   aspect.

   SET IT OUTSIDE A RACE

   The HUD's projection is computed once, when a race display is built
   (RaceDisplay vtable slot 9, 0x1C0A48), and cached at display+112. A change
   mid-race reaches the anchor shift, the squashes and the text at once but
   not that projection until the next race display is built.
   =========================================================================== */

void RaceAspect_InstallHooks(void);

/* The wide_mode attribute's set, and apply's ratioIsWide call. See
   RaceAspect.c. */
void HOOK_WideModeSet(void* options, int value);
int  HOOK_WideModeApply(void* geometry);

/* Script's max width, registered on MOption alongside RaceHud's functions. */
void f_RaceAspectSetMaxWidth(HObject* return_value, int argc, hObject** argv);
void f_RaceAspectGetMaxWidth(HObject* return_value, int argc, hObject** argv);

/* The menu pointer's rotate call: squashes after the tilt. See RaceAspect.c.
   Written in assembly and entered with the GAME's calling convention - a0 =
   matrix, $f12 = degrees - so it is declared without parameters: only its
   address is ever used. Never call it from C. */
void  HOOK_CursorRotate(void);
