#include <stdbool.h>
#include <stdio.h>

#include "MakerList.h"

#include "core/Target.h"
#include "core/ps2/Memory.h"
#include "core/Log.h"

/* ---- how this works -------------------------------------------------------

   The names come from the game's own MakerNames.c; everything here is the
   machinery, which is shared because the tables are BYTE IDENTICAL in all four
   retail executables. Gran Turismo 4 Online and all three Tourist Trophy regions
   carry the same records, in the same order, with the same ids - GT4 Online
   already holds the motorcycle marques even though it never shows them.

     table A   92 records, ids 0 and 2..92
     table B  109 records, ids 0 and 2..109

   TABLE B IS THE ONE THAT MATTERS. It is the full list - the tuners interleaved
   alphabetically among the car marques, the motorcycle marques appended - and it
   is the id space a car's row in the spec DB indexes. Confirmed against four
   real rows: spoon 37, alfaromeo 3, chaparral 93, and the Spirra's 96, which is
   "proto".

   Table A is the same list with eighteen names taken out and "manufacturer" put
   in. Mostly they are tuners, though Alpine, Pescarolo and Toyota Modellista are
   in there too; MakerList.h marks all eighteen with a *. So A's ids agree with
   B's up to "lotus" and drift apart after it. Never put an A id in the spec DB:
   chaparral is 79 there and 93 in B.

   Id 1 is absent from both, which is a shipped quirk rather than anything here.

   A record is { unsigned int id; const char* name; }, and the names live in two
   packed string pools, each sitting immediately before a table. Pool A holds all
   92 of table A's names; pool B holds only the eighteen extra ones, so table B's
   records point into BOTH. Lookup is a LINEAR SCAN in both
   directions - no sort, no hash - so appending cannot disturb anything already
   in the table, and the ids of existing entries never move.

   There is no room to grow either table where it stands: table A butts straight
   into the string "mines" and table B into the country code "DE". So both are
   copied into plugin memory, extended, and the places that materialise a table
   address are repointed. Exactly eight instruction words are involved. An
   exhaustive sweep of both loadable segments found three code references, zero
   data pointers into either table, and no $gp-relative addressing anywhere in
   the image that could hide a fourth.

   The shipped records are copied AT RUN TIME rather than transcribed into this
   file. Two hundred and one hand-typed ids would be the largest avoidable risk
   in the whole feature, and copying is provably identical on every build.

   One shipped bug worth knowing about, because it explains a surprise below:
   table A has no terminator. The engine's own count walks off its end into table
   B's string pool, reads four junk records built from the bytes of "mines",
   "mugen", "nismo" and "spoon", and stops on "toms" - so the game already
   believes table A holds 96 records. The tables built here are terminated
   properly, which incidentally fixes that.
*/

typedef struct MakerRecord
{
    unsigned int id;
    const char*  name;
} MakerRecord;

/* What the retail tables hold. Table A cannot be sized by walking - see the
   missing terminator above - so this is stated, not discovered. */
#define MAKER_A_SHIPPED 92
#define MAKER_B_SHIPPED 109

/*
    How many names may be added in total, and it is the engine's own limit rather
    than a number chosen here.

    A car's row in the spec DB identifies its maker with a single byte, read with
    lbu, so no id can exceed 255. Table B's highest shipped id is 109, which
    leaves 146 free. Table A has 163 free, but a car marque needs a row in BOTH
    tables, so B's headroom is what actually bounds the total and A's spare
    capacity is never reachable.

    Sizing the arrays for the full 146 costs under 4 KB, so there is no reason to
    allow less than the game does.
*/
#define MAKER_ID_MAX    255
#define MAKER_EXTRA_MAX (MAKER_ID_MAX - MAKER_B_SHIPPED)

/*
    Explicitly initialised so they land in .data.

    The build passes -fno-zero-initialized-in-bss and the injector copies only
    the plugin's text segment into the executable, so an uninitialised static
    would not be present in the injected image at all. The linkfile folds
    *(.data) and *(.rodata) into .text at BASE_ADDRESS, which is also what puts
    the name string literals somewhere the game can read.
*/
static MakerRecord g_makerA[MAKER_A_SHIPPED + MAKER_EXTRA_MAX + 1] = { { 0, 0 } };
static MakerRecord g_makerB[MAKER_B_SHIPPED + MAKER_EXTRA_MAX + 1] = { { 0, 0 } };

/*
    An address reaches a register as lui + addiu, and THE ADDIU IMMEDIATE IS
    SIGNED. So 0x006F7A68 is lui 0x6F / addiu 0x7A68, but 0x005FC3E8 is
    lui 0x60 / addiu -0x3C18 - not lui 0x5F. Getting this backwards is the
    classic way to write a patch that lands 0x10000 bytes from where it should.
*/
#define MIPS_LO(a) ((unsigned int)(a) & 0xFFFF)
#define MIPS_HI(a) (((((unsigned int)(a)) >> 16) \
                    + ((((unsigned int)(a)) & 0x8000) ? 1 : 0)) & 0xFFFF)

static int CountNames(const char* const* list)
{
    int n = 0;

    while (list[n] != NULL)
        n++;

    return n;
}

/*
    Refuse to touch anything unless every word is exactly what this build is
    supposed to have. A mismatch means the addresses describe some other
    executable, and patching it blind would plant a pointer in the middle of
    unrelated code - which would not fault here, but somewhere else entirely.
*/
static int WordIs(unsigned int addr, unsigned int expect)
{
    unsigned int got = *(volatile unsigned int*)addr;

    if (got == expect)
        return 1;

    LOG("maker list: %x holds %x, expected %x\n", addr, got, expect);
    return 0;
}

void MakerList_InstallHooks(void)
{
    const MakerRecord* shippedA = (const MakerRecord*)ADDR_MakerTableA;
    const MakerRecord* shippedB = (const MakerRecord*)ADDR_MakerTableB;

    int makers = CountNames(MAKER_EXTRA);
    int tuners = CountNames(TUNER_EXTRA);
    int a = 0;
    int b = 0;
    int i;

    unsigned int nextA;
    unsigned int nextB;
    unsigned int firstB;

    /* Nothing to add: leave the game exactly as it shipped. This early return is
       load bearing rather than an optimisation - the count patch below changes
       behaviour on its own, so it must not run when no name was added. */
    if (makers == 0 && tuners == 0)
        return;

    /* One test covers both tables. Every maker takes a row in each, so
       makers + tuners is exactly what table B has to hold, and table A - which
       receives only the makers - has more headroom than that and cannot be the
       one to run out. */
    if (makers + tuners > MAKER_EXTRA_MAX)
    {
        LOG("maker list: %d makers and %d tuners is %d, and only %d ids are free\n",
            makers, tuners, makers + tuners, MAKER_EXTRA_MAX);
        return;
    }

    /* All eight, even the ones this run may not patch. A mismatch anywhere means
       the target header does not describe this executable, and none of it should
       be trusted. */
    if (!WordIs(ADDR_MakerCtorA_Lui,    0x3C050000 | MIPS_HI(ADDR_MakerTableA)) ||
        !WordIs(ADDR_MakerCtorA_Addiu,  0x24A50000 | MIPS_LO(ADDR_MakerTableA)) ||
        !WordIs(ADDR_MakerCtorB_Lui,    0x3C050000 | MIPS_HI(ADDR_MakerTableB)) ||
        !WordIs(ADDR_MakerCtorB_Addiu,  0x24A50000 | MIPS_LO(ADDR_MakerTableB)) ||
        !WordIs(ADDR_MakerGetter_Lui,   0x3C020000 | MIPS_HI(ADDR_MakerTableB)) ||
        !WordIs(ADDR_MakerGetter_Addiu, 0x24420000 | MIPS_LO(ADDR_MakerTableB)) ||
        !WordIs(ADDR_MakerCountA,       0x24050000 | MAKER_A_SHIPPED) ||
        !WordIs(ADDR_MakerCountB,       0x24030000 | MAKER_B_SHIPPED))
    {
        LOG("maker list: this is not the executable those addresses describe; not patching\n");
        return;
    }

    for (b = 0; b < MAKER_B_SHIPPED; b++)
        g_makerB[b] = shippedB[b];

    nextB  = g_makerB[MAKER_B_SHIPPED - 1].id + 1;
    firstB = nextB;

    /* A car marque wants an entry in both tables, and the two number things
       independently - the same name legitimately holds a different id in each,
       exactly as chaparral is 79 in A and 93 in B today. The id that reaches the
       spec DB is B's; A's is for whatever reads the shorter list. */
    if (makers > 0)
    {
        for (a = 0; a < MAKER_A_SHIPPED; a++)
            g_makerA[a] = shippedA[a];

        nextA = g_makerA[MAKER_A_SHIPPED - 1].id + 1;

        for (i = 0; i < makers; i++)
        {
            g_makerA[a].id   = nextA++;
            g_makerA[a].name = MAKER_EXTRA[i];
            a++;

            g_makerB[b].id   = nextB++;
            g_makerB[b].name = MAKER_EXTRA[i];
            b++;
        }

        g_makerA[a].id   = 0;
        g_makerA[a].name = NULL;
    }

    for (i = 0; i < tuners; i++)
    {
        g_makerB[b].id   = nextB++;
        g_makerB[b].name = TUNER_EXTRA[i];
        b++;
    }

    /* Not optional: the engine sizes a table itself, walking at stride 8 until
       it meets a record whose NAME word is zero. Without this it would run into
       whatever follows in plugin memory. */
    g_makerB[b].id   = 0;
    g_makerB[b].name = NULL;

    if (makers > 0)
    {
        PATCH_INT(ADDR_MakerCtorA_Lui,   0x3C050000 | MIPS_HI((unsigned int)g_makerA));
        PATCH_INT(ADDR_MakerCtorA_Addiu, 0x24A50000 | MIPS_LO((unsigned int)g_makerA));

        /*
            The two counts do NOT mean the same thing, which is not obvious and
            matters a great deal.

            This one bounds an ID. The adhoc maker count is handed straight to
            script, and the name lookup passes its argument to the id search
            rather than using it as an array index - so it has to be the highest
            id plus one.

            Note the shipped value is 92 while the highest shipped id is also 92,
            which is why "trickstar" is unreachable in a retail game. Raising it
            makes that entry visible for the first time, and in GT4 Online that
            is a motorcycle marque turning up in a car list. There is no way to
            expose a new id without also exposing that one, because the ids are
            contiguous and the lookup is a scan. Filter it script-side if it
            shows up somewhere it should not.
        */
        PATCH_INT(ADDR_MakerCountA,
                  0x24050000 | (unsigned int)(g_makerA[a - 1].id + 1));
    }

    PATCH_INT(ADDR_MakerCtorB_Lui,    0x3C050000 | MIPS_HI((unsigned int)g_makerB));
    PATCH_INT(ADDR_MakerCtorB_Addiu,  0x24A50000 | MIPS_LO((unsigned int)g_makerB));
    PATCH_INT(ADDR_MakerGetter_Lui,   0x3C020000 | MIPS_HI((unsigned int)g_makerB));
    PATCH_INT(ADDR_MakerGetter_Addiu, 0x24420000 | MIPS_LO((unsigned int)g_makerB));

    /* Whereas this one bounds an INDEX: the tuner list walks the array as
       for (i = 0; i < count; i++, p += 8), so it is a record count. */
    PATCH_INT(ADDR_MakerCountB, 0x24030000 | (unsigned int)b);

    LOG("maker list: table A %d records, table B %d records\n", a, b);

    /* The number each new name needs in the spec DB. Printed because it is the
       one thing that cannot be read off the source - it depends on how many
       names are in the lists and in what order. */
    for (i = 0; i < makers; i++)
        LOG("maker list: %s = id %d\n", MAKER_EXTRA[i], (int)(firstB + i));

    for (i = 0; i < tuners; i++)
        LOG("maker list: %s = id %d\n", TUNER_EXTRA[i], (int)(firstB + makers + i));
}
