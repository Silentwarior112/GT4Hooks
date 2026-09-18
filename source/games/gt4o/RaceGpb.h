#pragma once

#include "RaceHud.h"    /* the adhoc types */

/*
    The race display's texture file, chosen from script.

        main::menu::MOption::RaceHudSetGpb("display_dark");  // -> 1, or 0 if refused
        main::menu::MOption::RaceHudGetGpb();                // -> "display_dark"

    The game reads the in-race HUD's textures from
    /race_display/<set>/<region>/display.gpb - gt4/US/display.gpb here - once,
    when the first race display of a race is built, and lets the file go when
    the last one dies. This makes the file name script's: the game builds that
    path with sprintf from a format string, and the plugin owns the format
    instead of the game, with whatever name script set where display was. Set
    from a menu, it holds for every race after, until the console is switched
    off; "display" is the game's own file. A name is 1 to 31 characters of
    A-Z, a-z, 0-9, _ and -: no folders, no dots - a file beside display.gpb.

    Nothing here checks that the file exists: a name that is not on the disc
    is the game's own missing-file behaviour at race start.
*/
void RaceGpb_InstallHooks(void);

void f_RaceHudSetGpb(HObject* return_value, int argc, hObject** argv);
void f_RaceHudGetGpb(HObject* return_value, int argc, hObject** argv);
