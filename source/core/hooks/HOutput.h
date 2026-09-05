#pragma once

/*
    HOutput - the engine's own debug output class.

    Shared by every game: the implementation is entirely address-driven, and the
    addresses come from the selected build's target header.
*/

void HOutput_InstallHooks(void);
void HOOK_HOutput_Handler(char* text);
