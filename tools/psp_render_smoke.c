#include <pspkernel.h>
#include <pspgu.h>
#include <pspdisplay.h>

PSP_MODULE_INFO("P4 depth mask test", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

typedef struct { float u, v; unsigned colour; float x, y, z; } GuVertex;
enum { RS_ZWRITE };
static unsigned sky_state[4], render_state[1], pixel_mask;
static unsigned __attribute__((aligned(16))) commands[32768];

#include "p4_draw_2d_tested.h"

static void rectangle(float x0, float y0, float x1, float y1, float depth, unsigned colour)
{
    GuVertex *v = sceGuGetMemory(2 * sizeof *v);
    v[0] = (GuVertex){0, 0, colour, x0, y0, depth};
    v[1] = (GuVertex){0, 0, colour, x1, y1, depth};
    draw_2d_tested(GU_SPRITES, 2, v, 0);
}

int main(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, commands);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, 512);
    sceGuDispBuffer(480, 272, (void *)(512 * 272 * 4), 512);
    sceGuDepthBuffer((void *)(512 * 272 * 8), 512);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuPixelMask(0);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuClearDepth(0);
    sceGuClearColor(0xff0000ff);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    render_state[RS_ZWRITE] = 1;

    /* The command panel and results screen submit this GS state. */
    sky_state[3] = 0x32801;
    p4_gs_alpha_test = 0;
    rectangle(40, 40, 200, 110, 35000, 0x3000ff00);
    p4_gs_alpha_test = 1;
    rectangle(280, 40, 440, 110, 35000, 0x3000ff00);
    rectangle(280, 160, 340, 230, 35000, 0x00000000);
    rectangle(380, 160, 440, 230, 35000, 0x00ffffff);

    /* A colour draw behind the rectangles shows where depth was written. */
    sky_state[3] = 0;
    rectangle(0, 85, 480, 100, 25000, 0xffff0000);
    rectangle(0, 200, 480, 215, 25000, 0xffff0000);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceGuSwapBuffers();
    sceGuDisplay(GU_TRUE);
    for (;;)
        sceDisplayWaitVblankStart();
}
