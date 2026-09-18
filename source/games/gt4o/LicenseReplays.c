#include "core/Target.h"
#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"

#include "LicenseReplays.h"

/*
    License demo replays, in pcsx2 builds.

    The game reads them from replay/license/: 0x1A8BE8 appends "license/%s"
    and the replay's name to the demo directory, so replay/license/usl0a0001.
    pcsx2 builds read them from replay/license_pcsx2/ instead - the same names,
    in a folder of their own beside the console one, in the volume or in
    HostFS's host folder. The format's lui/addiu pair is pointed at this
    string; nothing else reads the game's own, and a console build leaves it
    alone.
*/
static const char s_format[] = "license_pcsx2/%s";

void LicenseReplays_InstallHooks(void)
{
    unsigned int addr = (unsigned int)s_format;

    if (*(volatile unsigned int*)ADDR_LicenseReplay_FormatLui != INSN_LicenseReplay_FormatLui ||
        *(volatile unsigned int*)ADDR_LicenseReplay_FormatAddiu != INSN_LicenseReplay_FormatAddiu)
    {
        Sio_Puts("[gt4hooks] license replays: not the shipped code;"
                 " they stay in replay/license\n");
        return;
    }

    /* addiu sign-extends its immediate, so the upper half takes the carry. */
    PATCH_INT(ADDR_LicenseReplay_FormatLui,
              (INSN_LicenseReplay_FormatLui & 0xFFFF0000u) | ((addr + 0x8000u) >> 16));
    PATCH_INT(ADDR_LicenseReplay_FormatAddiu,
              (INSN_LicenseReplay_FormatAddiu & 0xFFFF0000u) | (addr & 0xFFFFu));
    Sio_Puts("[gt4hooks] license replays: from replay/license_pcsx2\n");
}
