#include <stdbool.h>

#include "core/ps2/Memory.h"

#include "CameraSys.h"
#include "GameFunctions/CameraSys.h"
#include "core/game/IO.h"

/* Hook: CameraSys
   Purpose: Adds more switchable gameplay cameras (CameraOnboard).
   How: Adds more cameras to CameraSys::_mtTable, which defines the available cams to switch through.
        The extra functions overriden simply readjust the table size.
*/

void CameraSys_InstallHooks()
{
    MAKE_JAL(ADDR_CameraSys_GetCameraMountIndex_JAL, &HOOK__CameraSys_CameraOnBoard_GetCameraMountIndex);
    MAKE_JAL(ADDR_CameraSys_GetCamOffset_JAL, &HOOK__CameraSys_CameraOnBoard_GetCamOffset);
}

static CameraOnboardMount CameraSys__mtTable[] = {
    CameraOnboardMount_DRIVER,   // 0
    CameraOnboardMount_CHASE,    // 1
    CameraOnboardMount_BONNET,   // 6
    CameraOnboardMount_ROOF,     // 7
    CameraOnboardMount_OPTION_1, // 18
    CameraOnboardMount_METER,    // 20
};

int HOOK__CameraSys_CameraOnBoard_GetCameraMountIndex(CameraOnboardMount nextCamera)
{
    _print("HOOK__CameraSys_CameraOnBoard_GetCameraMountIndex: %d\n", nextCamera);
    const int numCams = sizeof(CameraSys__mtTable) / sizeof(CameraSys__mtTable[0]);

    for (int i = 0; i < numCams; i++)
    {
        if (CameraSys__mtTable[i] == nextCamera)
            return i;
    }

    return -1;
}

int HOOK__CameraSys_CameraOnBoard_GetCamOffset(int currentCamMountIndex, CameraOnboardMount currentCamMount)
{
    const int numCams = sizeof(CameraSys__mtTable) / sizeof(CameraSys__mtTable[0]);
    return CameraSys__mtTable[(currentCamMountIndex + currentCamMount) % numCams];
}