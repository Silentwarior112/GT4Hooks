#include <stdbool.h>
#include <stdio.h>

#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"
#include "core/Target.h"
#include "core/game/IO.h"

#include "HOutput.h"

/* Hook: HOutput
   Purpose: Restores the engine's own debug output, which retail builds stub out.
   How:     ADDR_HOutput_Handler is a global function POINTER, not a function.
            In a release build it holds a one-instruction `jr $ra` stub, so
            everything the engine tries to print is silently dropped. Pointing
            it at our own handler makes those messages appear.

            ADDR_ADHOC_printf is a second stub, in the adhoc scripting runtime.
            It is redirected to the game's real printf, which has the matching
            varargs signature.

   Note:    This is a different thing from LOG() in core/Log.h. LOG is how the
            plugin prints its own messages; HOutput un-stubs something the GAME
            calls. Both end up on the same console.
*/

void HOutput_InstallHooks(void)
{
    HOOK_FUNC_ADDR((void*)ADDR_HOutput_Handler, &HOOK_HOutput_Handler);

    /* ADDR_ADHOC_debug is deliberately left alone - it is extremely chatty and
       drowns out everything else. Uncomment if you need it.

       HOOK(ADDR_ADHOC_debug, (void*)ADDR_print); */
    HOOK(ADDR_ADHOC_printf, (void*)ADDR_print);
}

void HOOK_HOutput_Handler(char* text)
{
    /*
        Straight out the SIO port rather than through the game's printf.

        The engine hands us an already-formatted string, so there is nothing to
        format; passing it to a printf would also make game-supplied text the
        format string, and would cap it at the log buffer size. Sio_Puts has
        neither problem, and unlike the game's printf it keeps working after
        startup - see the note in core/Log.h.
    */
    Sio_Puts(text);
}
