#include <stddef.h>

#include "core/Target.h"
#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"
#include "core/game/String.h"      /* __strlen, for STD_STRING */
#include "Adhoc.h"                 /* STD_STRING */
#include "GameFunctions/Adhoc.h"

#include "RaceGpb.h"

/*
    Hook: RaceGpb
    Purpose: Let script choose which .gpb the in-race HUD's textures come from.
    How: The game builds the file's path with sprintf(buf, FORMAT, set,
         region) at ADDR_RaceGpb_FormatAddiu's call, FORMAT being
         "/race_display/%s/%s/display.gpb" in its own data. The lui/addiu pair
         that loads FORMAT is pointed at s_gpbFormat below instead - the same
         text, with the file name script last set in place of "display" - the
         way LicenseReplays.c moves the license replays. Nothing else reads
         the game's own string, and the game's own code does all the loading.

    The file is read when the first RaceDisplay of a race is built and let go
    when the last one dies (see the target header), so a name set in a menu
    reaches the next race whole, and the previous race's file is gone by then.
*/

#define RACEGPB_PREFIX   "/race_display/%s/%s/"
#define RACEGPB_SUFFIX   ".gpb"
#define RACEGPB_NAME_MAX 31

/* Explicitly initialised, so both land in .data and in the injected image. */
static char s_gpbName[RACEGPB_NAME_MAX + 1] = "display";
static char s_gpbFormat[sizeof(RACEGPB_PREFIX) - 1 + RACEGPB_NAME_MAX + sizeof(RACEGPB_SUFFIX)] =
    RACEGPB_PREFIX "display" RACEGPB_SUFFIX;
static int  s_live = 0;

/* A file name: 1 to RACEGPB_NAME_MAX of A-Z, a-z, 0-9, _ and -. No slash, so
   it cannot leave the folder, and no dot, so it cannot change the extension
   or name a hidden file. Returns its length, or 0. */
static int NameLength(const char* s)
{
    int n = 0;

    if (s == NULL)
        return 0;

    for (; s[n] != 0; n++)
    {
        char c = s[n];

        if (n >= RACEGPB_NAME_MAX)
            return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }

    return n;
}

/* s_gpbFormat = PREFIX + s_gpbName + SUFFIX. Sizes are static, so this cannot
   overrun. */
static void BuildFormat(void)
{
    const char* p;
    char*       out = s_gpbFormat;

    for (p = RACEGPB_PREFIX; *p; p++) *out++ = *p;
    for (p = s_gpbName;      *p; p++) *out++ = *p;
    for (p = RACEGPB_SUFFIX; *p; p++) *out++ = *p;
    *out = 0;
}

/*
    MOption::RaceHudSetGpb(name) -> 1 or 0

    0 for anything but a string that NameLength accepts, and when the install
    found other code where the format is loaded. The name is copied, so
    script's string may go.
*/
void f_RaceHudSetGpb(HObject* return_value, int argc, hObject** argv)
{
    const char* name;
    int         n, i;

    if (!s_live || argc < 1 || !RaceHud_IsString(argv + 0))
    {
        RaceHud_ReturnInt(return_value, 0);
        return;
    }

    name = RaceHud_CStr(argv + 0);
    n    = NameLength(name);
    if (n == 0)
    {
        Sio_Puts("[gt4hooks] race display textures: a file name is 1 to 31 of"
                 " A-Z a-z 0-9 _ - ; refused\n");
        RaceHud_ReturnInt(return_value, 0);
        return;
    }

    for (i = 0; i < n; i++)
        s_gpbName[i] = name[i];
    s_gpbName[n] = 0;
    BuildFormat();

    Sio_Puts("[gt4hooks] race display textures: ");
    Sio_Puts(s_gpbFormat);
    Sio_Puts("\n");
    RaceHud_ReturnInt(return_value, 1);
}

/* MOption::RaceHudGetGpb() -> the name in force, "display" until one is set. */
void f_RaceHudGetGpb(HObject* return_value, int argc, hObject** argv)
{
    HString* result;

    (void)argc;
    (void)argv;

    /* A std::string from the C text, then the HString handle script gets; our
       reference on the std::string goes once the handle holds its own, as
       MStorage.c's Adhoc_MakeString does it. */
    {
        STD_STRING(s_gpbName);
        HString_HString((HString*)&result, &unk);
        {
            void* rep  = (char*)unk - 16;
            int   refs = *((int*)unk - 2) - 1;

            *((int*)unk - 2) = refs;
            if (refs == 0)
                ADHOC_Deallocate(rep, (void*)(*((int*)rep + 1) + 16), 4);
        }
    }

    ADHOC_MakeRef(return_value, (HObject*)result);
    HValue_dtor((void*)&result, 2);
}

/*
    Point the game's format load at s_gpbFormat. Both words must be exactly what
    shipped; otherwise the game keeps its own string and script's function
    answers 0. The name starts as "display", so until script sets one the
    game reads the file it always did.
*/
void RaceGpb_InstallHooks(void)
{
    unsigned int addr = (unsigned int)s_gpbFormat;

    if (!RaceHud_WordIs(ADDR_RaceGpb_FormatLui,   INSN_RaceGpb_FormatLui) ||
        !RaceHud_WordIs(ADDR_RaceGpb_FormatAddiu, INSN_RaceGpb_FormatAddiu))
    {
        Sio_Puts("[gt4hooks] race display textures: not the shipped code;"
                 " the file stays display.gpb\n");
        return;
    }

    /* addiu sign-extends its immediate, so the upper half takes the carry. */
    PATCH_INT(ADDR_RaceGpb_FormatLui,
              (INSN_RaceGpb_FormatLui & 0xFFFF0000u) | ((addr + 0x8000u) >> 16));
    PATCH_INT(ADDR_RaceGpb_FormatAddiu,
              (INSN_RaceGpb_FormatAddiu & 0xFFFF0000u) | (addr & 0xFFFFu));

    s_live = 1;
}
