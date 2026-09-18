#include "core/hooks/MakerList.h"

/*
    Manufacturers added to Tourist Trophy.

    Empty, so the maker tables are left exactly as they shipped. Add names here
    the same way Gran Turismo 4 Online does in source/games/gt4o/MakerNames.c -
    the mechanism is identical, because the two games carry byte-identical maker
    tables. Only the names should differ, which is the whole reason these lists
    are per game rather than shared.

    Worth checking core/hooks/MakerList.h before adding anything: the retail
    table already holds yamaha, kawasaki, aprilia, mvagusta, ducati, buell,
    yoshimura, moriwaki and the rest, so most of what a motorcycle game wants is
    there already. What is missing is marques like KTM, Bimota, Moto Guzzi,
    Benelli, Husqvarna and Norton.

    Lowercase code names, no spaces, 25 characters or fewer.
*/
const char* const MAKER_EXTRA[] =
{
 /* "ktm", */
    NULL
};

const char* const TUNER_EXTRA[] =
{
 /* "someTuner", */
    NULL
};
