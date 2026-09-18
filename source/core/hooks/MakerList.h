#pragma once

/* For NULL, which every MakerNames.c needs to terminate its lists. */
#include <stddef.h>

/*
    Extend the game's manufacturer list.

    The mechanism is shared - the two tables are byte identical in all four
    retail executables - but the names are not, since Gran Turismo 4 and Tourist
    Trophy want different marques. So this header and MakerList.c hold the
    machinery, and each game supplies its own lists:

      source/games/gt4o/MakerNames.c
      source/games/tt/MakerNames.c

    Add names there and rebuild. With a game's lists empty this does nothing at
    all - no table is copied and no instruction is patched - so it is safe to
    leave installed.

    WHAT THIS GIVES YOU is a manufacturer the ENGINE can name: it resolves the
    code name to an id and back, the adhoc tuner list enumerates it, and the
    maker lookups report it. What it does NOT give you is a showroom hall with
    cars in it, an emblem, or a displayed name. Those live in the game data:

      * which cars carry the badge - the maker byte of the car's row in the spec
        DB inside the VOL. The id to put there is the TABLE B id, which
        MakerList.c prints to the console for every name it adds.
      * the emblem - a sprite named "mlogo_<codename>" inside an already-packed
        UI container.
      * the displayed name - localised game text. No capitalised marque name
        exists anywhere in any of these executables; the engine only ever handles
        the lowercase code name.

    Call MakerList_InstallHooks() from init(), before the game's static
    constructors run.
*/
void MakerList_InstallHooks(void);

/*
    Supplied per game by MakerNames.c. Both are NUL-pointer terminated.

    MAKER_EXTRA lands in BOTH tables, which is what a car marque needs.
    TUNER_EXTRA lands in table B only, the way hks and spoon do - they carry an
    id like anything else, they simply do not appear in the shorter table A.
*/
extern const char* const MAKER_EXTRA[];
extern const char* const TUNER_EXTRA[];

/* ---- the retail list ------------------------------------------------------

   Every manufacturer the shipped executables know, with the id a car's spec DB
   row uses. Identical in Gran Turismo 4 Online and all three Tourist Trophy
   builds. Read straight out of the executable, not transcribed.

   These are TABLE B ids. Table B is the full list, tuners included, and it is
   the id space the spec DB indexes - checked against four real rows: spoon 37,
   alfaromeo 3, chaparral 93, and the Spirra's 96, which is "proto".

   Table A carries the same names minus the tuners, so its ids match these up to
   "lotus" and drift apart after it. An A id is NOT what goes in the spec DB -
   chaparral is 79 there and 93 here. An entry marked * is one table A lacks.

   Ids run 0 then 2..109. Id 1 is absent, which is a shipped quirk.

     0 nothing               38 subaru                75 cizeta
     2 acura                 39 suzuki                76 hks *
     3 alfaromeo             40 tickford              77 pescarolo *
     4 astonmartin           41 tommykaira            78 fpv
     5 audi                  42 toms *                79 opera *
     6 bmw                   43 toyota                80 caterham
     7 chevrolet             44 tvr                   81 ac
     8 chrysler              45 vauxhall              82 bentley
     9 citroen               46 volkswagen            83 seat
    10 daihatsu              47 asl                   84 landrover
    11 dodge                 48 dome                  85 holden
    12 fiat                  49 infiniti              86 alpine *
    13 ford                  50 lexus                 87 amemiya *
    14 gillet                51 mini                  88 nike
    15 honda                 52 pontiac               89 toyotamodellista *
    16 hyundai               53 spyker                90 trial *
    17 jaguar                54 cadillac              91 au_ford
    18 lancia                55 plymouth              92 trd *
    19 lister                56 isuzu                 93 chaparral
    20 lotus                 57 autobianchi           94 hpa *
    21 mazda                 58 ginetta               95 autounion
    22 mercedes              59 amuse *               96 proto
    23 mg_mini               60 saleen                97 yamaha
    24 mines *               61 vemac                 98 kawasaki
    25 mitsubishi            62 jayleno               99 aprilia
    26 mugen *               63 buick                100 mvagusta
    27 nismo *               64 callaway             101 yoshimura
    28 nissan                65 dmc                  102 moriwaki
    29 opel                  66 eagle                103 buell
    30 pagani                67 mercury              104 ducati
    31 panoz                 68 triumph              105 _7honda
    32 peugeot               69 volvo                106 ysp_presto
    33 polyphony             70 hommell              107 trickstar
    34 renault               71 jensen               108 yoshimura_suzuki *
    35 ruf                   72 marcos               109 moriwaki_motul *
    36 shelby                73 scion
    37 spoon *               74 blitz *
*/
