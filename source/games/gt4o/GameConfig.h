#pragma once

/*
    Per-game configuration for Gran Turismo 4 Online.

    Shared code in source/core includes "GameConfig.h" unqualified; the
    makefile adds -Isource/games/<game> so it lands on the right one.
*/

#define GAME_NAME     "gt4o"
#define GAME_LONGNAME "Gran Turismo 4 Online"

/*
    Path prefixes whose files must keep streaming from the disc rather than the
    host filesystem - see IsStreamedFile in core/hooks/HostFs.c.

    Gran Turismo 4 Online picks its engine-sound folder from a two-entry table
    { "motosound", "carsound" } indexed by a vehicle-class predicate, but that
    predicate is a stub returning zero, so only "carsound/" is ever reachable
    here. Tourist Trophy's predicate is real and needs both.
*/
#define GAME_HOSTFS_PREFIXES { "carsound/" }
