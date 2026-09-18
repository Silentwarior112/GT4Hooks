#pragma once

/*
    Memory diagnostics for dev RAM mode - see MemDiag.c. Lives in the dev RAM
    reserve, so it is installed only when PCSX2's 128 MB option is on.
*/
void MemDiag_Install(void);
