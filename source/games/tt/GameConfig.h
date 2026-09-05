#pragma once

/*
    Per-game configuration for Tourist Trophy.

    Shared code in source/core includes "GameConfig.h" unqualified; the
    makefile adds -Isource/games/<game> so it lands on the right one.
*/

#define GAME_NAME     "tt"
#define GAME_LONGNAME "Tourist Trophy"

/*
    Path prefixes whose files must keep streaming from the disc rather than the
    host filesystem - see IsStreamedFile in core/hooks/HostFs.c.

    Tourist Trophy needs BOTH, where Gran Turismo 4 Online needs only "carsound/".
    Both games build the engine-sound folder from the same two-entry table
    indexed by a vehicle-class predicate; in GT4 that predicate is a stub, but
    in Tourist Trophy it is real, despite only ever containing "motosound/".
    Therefore hinting that the engine was built with the intention of supporting both
    cars AND bikes.
*/
#define GAME_HOSTFS_PREFIXES { "motosound/", "carsound/" }
