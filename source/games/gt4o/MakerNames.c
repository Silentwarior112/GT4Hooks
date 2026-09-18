#include "core/hooks/MakerList.h"

/*
    Manufacturers added to Gran Turismo 4 Online.

    Lowercase code names, no spaces. Keep them to 25 characters: the engine
    builds an emblem sprite name by prefixing "mlogo_" into a 32-byte buffer, so
    anything longer would not have a reachable emblem. (That bound is read off
    the function's frame layout rather than from a check in the code, so treat it
    as the safe limit rather than a proven one.)

    MAKER_EXTRA goes into both tables - what a car marque needs. TUNER_EXTRA goes
    into table B only, the way hks and spoon do. Either way the id you put in the
    spec DB is the table B one, and MakerList.c prints it for every name at boot.
    The comments below are those ids for the list as it stands; they shift if you
    reorder or insert, so trust the console over the comment.

    The retail names and their ids are listed in core/hooks/MakerList.h. Check
    there before adding - GT4 Online already ships several marques it never
    shows, including the whole Tourist Trophy motorcycle set.
*/
const char* const MAKER_EXTRA[] =
{
    "ariel",        /* 110 */
    "brabus",       /* 111 */
    "bugatti",      /* 112 */
    "datsun",       /* 113 */
    "ferrari",      /* 114 */
    "gemballa",     /* 115 */
    "gmc",          /* 116 */
    "hennessey",    /* 117 */
    "holden_hsv",   /* 118 */
    "jeep",         /* 119 */
    "kia",          /* 120 */
    "koenigsegg",   /* 121 */
    "lada",         /* 122 */
    "lola",         /* 123 */
    "lamborghini",  /* 124 */
    "lincoln",      /* 125 */
    "maserati",     /* 126 */
    "mclaren",      /* 127 */
    "oldsmobile",   /* 128 */
    "porsche",      /* 129 */
    "rollsroyce",   /* 130 */
    "roush",        /* 131 */
    "rwb",          /* 132 */
    "saab",         /* 133 */
    "saturn",       /* 134 */
    "skoda",        /* 135 */
    "topsecret",    /* 136 */
    "vector",       /* 137 */
    "venturi",      /* 138 */
    "williams",     /* 139 */
    NULL
};

const char* const TUNER_EXTRA[] =
{
 /* "someTuner", */
    NULL
};
