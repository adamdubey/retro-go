#ifndef _GFX_H_
#define _GFX_H_

#include "sprite.h"
#include "osd.h"

// The extra 32's and 64's are linked to the way the sprite are blitted on screen, which can overlap to near memory
// If only one pixel is drawn in the screen, the whole sprite is written, which can eventually overlap unexpected area
// A sharper way of doing would probably reduce the amount of needed data from 220kb to 128kb (eventually smaller if restricting
// games with hi res to be launched)
// XBUF_WIDTH = 536 + 32 + 32
// XBUG_HEIGHT = 240 + 64 + 64

#define XBUF_WIDTH 	(360 + 32 + 32)
#define	XBUF_HEIGHT	(240 + 64 + 64)

typedef struct {
	int16 scroll_x;
	int16 scroll_y;
	int16 scroll_y_diff;
	int16 control;
} gfx_context;

extern bool gfx_need_video_mode_change;

int  gfx_init();
void gfx_term();
void gfx_save_context(char slot_number);
void gfx_load_context(char slot_number);
char gfx_loop();

void gfx_save_state(void *buffer, size_t len);
void gfx_load_state(void *buffer, size_t len);

extern int UCount;
extern int UPeriod;

#endif
