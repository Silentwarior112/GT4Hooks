#include <stdbool.h>
#include <stdio.h>

#include "core/hooks/HostFs.h"
#include "core/hooks/HOutput.h"
#include "CameraSys.h"
#include "Adhoc.h"
#include "MStorage.h"
#include "MCarGarage.h"

#include "GameFunctions/PolyphonyGL.h"
#include "GameStructs/PolyphonyGL.h"

#include "GameFunctions/FontManagerClass.h"
#include "GameStructs/FontManagerClass.h"

#include "GameFunctions/Misc.h"

#include "core/game/IO.h"
#include "core/ps2/Memory.h"

/* GLOBAL_MEMSET_OFFSET and MEMORY_SIZE_OFFSET now live in the target header
   alongside every other address, so a second GT4 region only needs a new one
   of those. See gt4o/Targets/SCUS-97436.h. */

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480


void init()
{
    // See: makefile comment for base address. clear memset so that it doesn't override our code
    NOP(GLOBAL_MEMSET_OFFSET);

#ifdef USE_DEV_RAM
    PATCH_INT(MEMORY_SIZE_OFFSET, (128 * 0x100000) - 0x8000);
    // Maybe later on adjust func buffer sizes for cars/models:
    // - 0x298A50 (VehicleModel::allocateBuffer)
    // - 0x2987C0 (`anonymous namespace'::setModelBuffer)
    // - 0x298BE0 (VehicleModel::getDataSizeMax) for race & race model buffer sizes
    // - 0x1BCF10 (RaceCarModel::loadMenuModel)
    // - anything that calls 0x2E94B8 (AllocateRaceBuffer) & sets 0x664720 (course_buffer_size)
#endif

    HostFs_InstallHooks();
    HOutput_InstallHooks();
    CameraSys_InstallHooks();
    ADHOC_InjectNewModules();
    MStorage_InstallHooks();
    MCarGarage_InstallHooks();

    //HOOK(ADDR_GranTurismo4_RenderManager_call, HOOK__GranTurismo4_RenderManager_call);
}

void __attribute__((optimize("O3"))) INVOKER()
{
    asm("ei\n");
    asm("addiu $ra, -4\n");

    init();
}

int main()
{
    return 0;
}

// This was just an attempt at an overlay
void HOOK__GranTurismo4_RenderManager_call(void* a1)
{
    //_print("Render tick\n");

    ((void(*)(void*, void*))ADDR_GranTurismo4_GameObjectManager_call)(a1, (void*)ADDR_GranTurismo4_GameObjectPS2_render); // Called in the original

    /*
    pglMatrixMode(0);
    pglPushMatrix();
    pglLoadIdentity();
    pglColor4f(1, 0, 0, 1);
    static MathVector3 vertices[] = (MathVector3[]){
            {-320,  224, 0 },
            { 320,  224, 0 },
            {-320, -224, 0 },
            { 320, -224, 0 }
    };

    static MathVector3 texCoords[] = (MathVector3[]){
            { -32,  32, 0 },
            {  32,  32, 0 },
            { -32, -32, 0 },
            {  32, -32, 0 },
    };

    pgluTriangleStrip3fv(sizeof(vertices), vertices, texCoords, NULL, NULL);
    pglPopMatrix();
    */

    /* This kinda worked, rectangle glitches out though
    static int counter = 0;

    // Create a transparent rectangle
    pglColor(0x40808080);
    pglDisable(PGL_TEST_DepthTest);
    pglEnable(PGL_PrimABE_AlphaBlend);
    pglBlendFunc(1 << 6 | 0 << 4 | 1 << 2 | 0 << 0, 1.0); // In order: D = 1, C = 0, B = 1, A = 0
    pglWcRect2i(4, 4, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 8);

    if (*((int*)0x6651BC) != 0)
    {
        FontManagerClass* class_ = *(FontManagerClass**)0x6651BC;
        class_->Color = 0x80A0A0A0;
        class_->IsAligned = 1;
        class_->AlignMode = 0;
        PDISTD_FontManagerClass_setFont(class_, "hn_14");
        PDISTD_FontManagerClass_setRegion(class_, 4, 4, SCREEN_WIDTH / 4, SCREEN_HEIGHT / 8);
        PDISTD_FontManagerClass_printf(class_, "Counter: %d", counter++);
    }
    */
    
}
